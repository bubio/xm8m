#include "ra_http_win.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <condition_variable>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Xm8Ra {

namespace {

struct WinHttpRequestState;

struct WinHttpOwnerState {
	std::mutex mutex;
	std::condition_variable closed_condition;
	HINTERNET session = nullptr;
	std::wstring user_agent;
	std::map<uint64_t, std::shared_ptr<WinHttpRequestState>> active;
	std::vector<RaHttpResponse> completed;
	size_t open_request_handles = 0;
	bool shutting_down = false;
};

struct WinHttpRequestState {
	std::mutex mutex;
	std::weak_ptr<WinHttpOwnerState> owner;
	RaHttpRequest request;
	std::string current_url;
	std::vector<uint8_t> body;
	std::vector<uint8_t> read_buffer;
	std::string response_content_type;
	HINTERNET connection = nullptr;
	HINTERNET request_handle = nullptr;
	DWORD_PTR transport_id = 0;
	int http_status = 0;
	uint32_t redirects = 0;
	ULONGLONG started_at = 0;
	bool canceled = false;
	bool completed = false;
	bool close_started = false;
};

std::mutex g_registry_mutex;
std::unordered_map<DWORD_PTR, std::shared_ptr<WinHttpRequestState>> g_registry;
std::atomic<uintptr_t> g_next_transport_id(1);

std::wstring Utf8ToWide(const std::string& value)
{
	if (value.empty() ||
		value.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
		return std::wstring();
	}
	const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
		value.data(), static_cast<int>(value.size()), nullptr, 0);
	if (length <= 0) {
		return std::wstring();
	}
	std::wstring result(static_cast<size_t>(length), L'\0');
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
		static_cast<int>(value.size()), &result[0], length) != length) {
		return std::wstring();
	}
	return result;
}

std::string WideToUtf8(const std::wstring& value)
{
	if (value.empty() ||
		value.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
		return std::string();
	}
	const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
		value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr,
		nullptr);
	if (length <= 0) {
		return std::string();
	}
	std::string result(static_cast<size_t>(length), '\0');
	if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
		static_cast<int>(value.size()), &result[0], length, nullptr,
		nullptr) != length) {
		return std::string();
	}
	return result;
}

std::string WinHttpErrorMessage(DWORD error)
{
	wchar_t *message = nullptr;
	const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
	const DWORD length = FormatMessageW(flags, nullptr, error, 0,
		reinterpret_cast<wchar_t *>(&message), 0, nullptr);
	std::string result = length == 0 || message == nullptr ?
		("WinHTTP error " + std::to_string(error)) :
		WideToUtf8(std::wstring(message, length));
	if (message != nullptr) {
		LocalFree(message);
	}
	while (!result.empty() &&
		(result.back() == '\r' || result.back() == '\n')) {
		result.pop_back();
	}
	return result;
}

RaHttpTransportResult ClassifyWinHttpError(
	const WinHttpRequestState& state, DWORD error)
{
	if (state.canceled || error == ERROR_WINHTTP_OPERATION_CANCELLED) {
		return RaHttpTransportResult::Canceled;
	}
	if (error == ERROR_WINHTTP_TIMEOUT) {
		return RaHttpTransportResult::Timeout;
	}
	switch (error) {
	case ERROR_WINHTTP_CANNOT_CONNECT:
	case ERROR_WINHTTP_CONNECTION_ERROR:
	case ERROR_WINHTTP_NAME_NOT_RESOLVED:
	case ERROR_WINHTTP_RESEND_REQUEST:
	case ERROR_WINHTTP_SECURE_FAILURE:
		return RaHttpTransportResult::RetryableClientError;
	default:
		return RaHttpTransportResult::ClientError;
	}
}

std::shared_ptr<WinHttpRequestState> FindTransport(DWORD_PTR transport_id)
{
	std::lock_guard<std::mutex> lock(g_registry_mutex);
	const auto it = g_registry.find(transport_id);
	return it == g_registry.end() ? nullptr : it->second;
}

void UnregisterTransport(DWORD_PTR transport_id)
{
	std::lock_guard<std::mutex> lock(g_registry_mutex);
	g_registry.erase(transport_id);
}

void CloseRequestHandle(const std::shared_ptr<WinHttpRequestState>& state)
{
	HINTERNET handle = nullptr;
	{
		std::lock_guard<std::mutex> lock(state->mutex);
		if (state->close_started || state->request_handle == nullptr) {
			return;
		}
		state->close_started = true;
		handle = state->request_handle;
	}
	WinHttpCloseHandle(handle);
}

void CompleteRequest(const std::shared_ptr<WinHttpRequestState>& state,
	RaHttpTransportResult result, const std::string& error)
{
	RaHttpResponse response;
	{
		std::lock_guard<std::mutex> lock(state->mutex);
		if (state->completed) {
			return;
		}
		state->completed = true;
		response.request_id = state->request.request_id;
		response.transport_result = result;
		response.http_status = state->http_status;
		response.content_type = state->response_content_type;
		response.body.swap(state->body);
		response.error = error;
	}

	const auto owner = state->owner.lock();
	if (owner != nullptr) {
		std::lock_guard<std::mutex> lock(owner->mutex);
		const auto active = owner->active.find(response.request_id);
		if (active != owner->active.end() && active->second == state) {
			owner->active.erase(active);
		}
		if (!owner->shutting_down) {
			owner->completed.push_back(std::move(response));
		}
	}
	CloseRequestHandle(state);
}

void CompleteImmediate(const std::shared_ptr<WinHttpOwnerState>& owner,
	const RaHttpRequest& request, RaHttpTransportResult result,
	const std::string& error)
{
	RaHttpResponse response;
	response.request_id = request.request_id;
	response.transport_result = result;
	response.error = error;
	std::lock_guard<std::mutex> lock(owner->mutex);
	if (!owner->shutting_down) {
		owner->completed.push_back(std::move(response));
	}
}

bool QueryHeaderString(HINTERNET request, DWORD query,
	std::string *output)
{
	DWORD bytes = 0;
	WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX,
		nullptr, &bytes, WINHTTP_NO_HEADER_INDEX);
	if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes < sizeof(wchar_t)) {
		return false;
	}
	std::vector<wchar_t> buffer(bytes / sizeof(wchar_t));
	if (!WinHttpQueryHeaders(request, query, WINHTTP_HEADER_NAME_BY_INDEX,
		buffer.data(), &bytes, WINHTTP_NO_HEADER_INDEX)) {
		return false;
	}
	*output = WideToUtf8(std::wstring(buffer.data()));
	return true;
}

bool IsHttpRedirectStatus(DWORD status)
{
	return status == 301 || status == 302 || status == 303 ||
		status == 307 || status == 308;
}

bool StartsWithHttps(const std::string& url)
{
	if (url.size() < 8) return false;
	const char expected[] = "https://";
	for (size_t index = 0; index < 8; ++index) {
		char actual = url[index];
		if (actual >= 'A' && actual <= 'Z') actual += 'a' - 'A';
		if (actual != expected[index]) return false;
	}
	return true;
}

bool ResolveImageRedirect(const std::string& current_url,
	const std::string& location, std::string *resolved)
{
	if (resolved == nullptr || location.empty()) return false;
	if (StartsWithHttps(location)) {
		*resolved = location;
		return true;
	}
	if (location.compare(0, 2, "//") == 0) {
		*resolved = "https:" + location;
		return true;
	}

	const size_t authority = current_url.find("//");
	if (authority == std::string::npos) return false;
	const size_t path_start = current_url.find_first_of("/?#", authority + 2);
	const std::string origin = path_start == std::string::npos ?
		current_url : current_url.substr(0, path_start);
	if (!StartsWithHttps(origin)) return false;
	if (location[0] == '/') {
		*resolved = origin + location;
		return true;
	}
	const std::string current_path = path_start == std::string::npos ||
		current_url[path_start] != '/' ? "/" : current_url.substr(path_start);
	const size_t query = current_path.find_first_of("?#");
	const std::string path_only = current_path.substr(0, query);
	const size_t slash = path_only.find_last_of('/');
	*resolved = origin + (slash == std::string::npos ? "/" :
		path_only.substr(0, slash + 1)) + location;
	return true;
}

bool StartRequest(const std::shared_ptr<WinHttpOwnerState>& owner,
	const RaHttpRequest& request, const std::string& url,
	uint32_t redirects, ULONGLONG started_at);

void FailAsyncCall(const std::shared_ptr<WinHttpRequestState>& state,
	DWORD error)
{
	RaHttpTransportResult result;
	{
		std::lock_guard<std::mutex> lock(state->mutex);
		result = ClassifyWinHttpError(*state, error);
	}
	CompleteRequest(state, result, WinHttpErrorMessage(error));
}

void BeginReceive(const std::shared_ptr<WinHttpRequestState>& state)
{
	HINTERNET handle;
	{
		std::lock_guard<std::mutex> lock(state->mutex);
		if (state->completed) {
			return;
		}
		handle = state->request_handle;
	}
	if (!WinHttpReceiveResponse(handle, nullptr)) {
		const DWORD error = GetLastError();
		if (error != ERROR_IO_PENDING) FailAsyncCall(state, error);
	}
}

void BeginQueryData(const std::shared_ptr<WinHttpRequestState>& state)
{
	HINTERNET handle;
	{
		std::lock_guard<std::mutex> lock(state->mutex);
		if (state->completed) {
			return;
		}
		handle = state->request_handle;
	}
	if (!WinHttpQueryDataAvailable(handle, nullptr)) {
		const DWORD error = GetLastError();
		if (error != ERROR_IO_PENDING) FailAsyncCall(state, error);
	}
}

void HandleHeaders(const std::shared_ptr<WinHttpRequestState>& state)
{
	HINTERNET handle;
	RaHttpPurpose purpose;
	bool has_post_data;
	std::string current_url;
	uint32_t redirects;
	{
		std::lock_guard<std::mutex> lock(state->mutex);
		if (state->completed) {
			return;
		}
		handle = state->request_handle;
		purpose = state->request.purpose;
		has_post_data = state->request.has_post_data;
		current_url = state->current_url;
		redirects = state->redirects;
	}

	DWORD status = 0;
	DWORD status_size = sizeof(status);
	if (!WinHttpQueryHeaders(handle,
		WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
		WINHTTP_NO_HEADER_INDEX)) {
		FailAsyncCall(state, GetLastError());
		return;
	}
	std::string content_type;
	QueryHeaderString(handle, WINHTTP_QUERY_CONTENT_TYPE, &content_type);
	{
		std::lock_guard<std::mutex> lock(state->mutex);
		state->http_status = static_cast<int>(status);
		state->response_content_type = content_type;
	}

	if (IsHttpRedirectStatus(status) && purpose == RaHttpPurpose::Image &&
		!has_post_data) {
		std::string location;
		std::string resolved_location;
		if (redirects >= 5 ||
			!QueryHeaderString(handle, WINHTTP_QUERY_LOCATION, &location) ||
			!ResolveImageRedirect(current_url, location, &resolved_location)) {
			CompleteRequest(state, RaHttpTransportResult::ClientError,
				"image redirect rejected");
			return;
		}
		const auto owner = state->owner.lock();
		bool followed = false;
		{
			// Retire the old hop under its lock so a polling deadline cannot
			// also enqueue a completion after the redirect is transferred.
			std::lock_guard<std::mutex> lock(state->mutex);
			if (state->completed || state->canceled) return;
			followed = owner != nullptr &&
				StartRequest(owner, state->request, resolved_location,
					redirects + 1, state->started_at);
			if (followed) state->completed = true;
		}
		if (!followed) {
			CompleteRequest(state, RaHttpTransportResult::ClientError,
				"could not follow image redirect");
			return;
		}
		CloseRequestHandle(state);
		return;
	}

	BeginQueryData(state);
}

void HandleDataAvailable(const std::shared_ptr<WinHttpRequestState>& state,
	DWORD available)
{
	if (available == 0) {
		CompleteRequest(state, RaHttpTransportResult::Success, std::string());
		return;
	}
	HINTERNET handle;
	void *buffer;
	{
		std::lock_guard<std::mutex> lock(state->mutex);
		if (state->completed) {
			return;
		}
		if (available > state->request.max_response_bytes -
			state->body.size()) {
			state->completed = true;
			RaHttpResponse response;
			response.request_id = state->request.request_id;
			response.transport_result = RaHttpTransportResult::Oversize;
			response.http_status = state->http_status;
			response.content_type = state->response_content_type;
			response.error = "response exceeds size limit";
			const auto owner = state->owner.lock();
			if (owner != nullptr) {
				std::lock_guard<std::mutex> owner_lock(owner->mutex);
				const auto active = owner->active.find(response.request_id);
				if (active != owner->active.end() && active->second == state) {
					owner->active.erase(active);
				}
				if (!owner->shutting_down) {
					owner->completed.push_back(std::move(response));
				}
			}
			buffer = nullptr;
			handle = nullptr;
		} else {
			state->read_buffer.resize(available);
			buffer = state->read_buffer.data();
			handle = state->request_handle;
		}
	}
	if (handle == nullptr) {
		CloseRequestHandle(state);
		return;
	}
	if (!WinHttpReadData(handle, buffer, available, nullptr)) {
		const DWORD error = GetLastError();
		if (error != ERROR_IO_PENDING) FailAsyncCall(state, error);
	}
}

void CALLBACK WinHttpCallback(HINTERNET, DWORD_PTR context, DWORD status,
	void *status_info, DWORD status_info_length)
{
	const auto state = FindTransport(context);
	if (state == nullptr) {
		return;
	}
	switch (status) {
	case WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE:
		BeginReceive(state);
		break;
	case WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE:
		HandleHeaders(state);
		break;
	case WINHTTP_CALLBACK_STATUS_DATA_AVAILABLE:
		if (status_info_length == sizeof(DWORD) && status_info != nullptr) {
			HandleDataAvailable(state, *static_cast<DWORD *>(status_info));
		}
		break;
	case WINHTTP_CALLBACK_STATUS_READ_COMPLETE:
		{
			std::lock_guard<std::mutex> lock(state->mutex);
			if (!state->completed && status_info_length > 0 &&
				status_info != nullptr) {
				const uint8_t *bytes = static_cast<const uint8_t *>(status_info);
				state->body.insert(state->body.end(), bytes,
					bytes + status_info_length);
			}
		}
		BeginQueryData(state);
		break;
	case WINHTTP_CALLBACK_STATUS_REQUEST_ERROR:
		if (status_info_length == sizeof(WINHTTP_ASYNC_RESULT) &&
			status_info != nullptr) {
			const WINHTTP_ASYNC_RESULT *result =
				static_cast<const WINHTTP_ASYNC_RESULT *>(status_info);
			FailAsyncCall(state, result->dwError);
		}
		break;
	case WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING:
		{
			HINTERNET connection = nullptr;
			{
				std::lock_guard<std::mutex> lock(state->mutex);
				state->request_handle = nullptr;
				connection = state->connection;
				state->connection = nullptr;
			}
			if (connection != nullptr) {
				WinHttpCloseHandle(connection);
			}
			UnregisterTransport(context);
			const auto owner = state->owner.lock();
			if (owner != nullptr) {
				std::lock_guard<std::mutex> lock(owner->mutex);
				if (owner->open_request_handles > 0) {
					--owner->open_request_handles;
				}
				owner->closed_condition.notify_all();
			}
		}
		break;
	default:
		break;
	}
}

bool CrackHttpsUrl(const std::string& url, std::wstring *host,
	std::wstring *path, INTERNET_PORT *port)
{
	std::wstring wide_url = Utf8ToWide(url);
	if (wide_url.empty()) {
		return false;
	}
	URL_COMPONENTS parts = {};
	parts.dwStructSize = sizeof(parts);
	parts.dwSchemeLength = static_cast<DWORD>(-1);
	parts.dwHostNameLength = static_cast<DWORD>(-1);
	parts.dwUrlPathLength = static_cast<DWORD>(-1);
	parts.dwExtraInfoLength = static_cast<DWORD>(-1);
	parts.dwUserNameLength = static_cast<DWORD>(-1);
	parts.dwPasswordLength = static_cast<DWORD>(-1);
	if (!WinHttpCrackUrl(wide_url.c_str(),
		static_cast<DWORD>(wide_url.size()), 0, &parts) ||
		parts.nScheme != INTERNET_SCHEME_HTTPS ||
		parts.dwHostNameLength == 0 || parts.dwUserNameLength != 0 ||
		parts.dwPasswordLength != 0) {
		return false;
	}
	host->assign(parts.lpszHostName, parts.dwHostNameLength);
	path->assign(parts.lpszUrlPath, parts.dwUrlPathLength);
	path->append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
	if (path->empty()) {
		*path = L"/";
	}
	*port = parts.nPort;
	return true;
}

bool StartRequest(const std::shared_ptr<WinHttpOwnerState>& owner,
	const RaHttpRequest& request, const std::string& url,
	uint32_t redirects, ULONGLONG started_at)
{
	std::wstring host;
	std::wstring path;
	INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
	if (!CrackHttpsUrl(url, &host, &path, &port) ||
		request.post_data.size() > (std::numeric_limits<DWORD>::max)()) {
		return false;
	}

	const auto state = std::make_shared<WinHttpRequestState>();
	state->owner = owner;
	state->request = request;
	state->current_url = url;
	state->redirects = redirects;
	state->started_at = started_at;
	state->transport_id = static_cast<DWORD_PTR>(
		g_next_transport_id.fetch_add(1));
	state->connection = WinHttpConnect(owner->session, host.c_str(), port, 0);
	if (state->connection == nullptr) {
		return false;
	}
	const wchar_t *verb = request.has_post_data ? L"POST" : L"GET";
	state->request_handle = WinHttpOpenRequest(state->connection, verb,
		path.c_str(), nullptr, WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
	if (state->request_handle == nullptr) {
		WinHttpCloseHandle(state->connection);
		state->connection = nullptr;
		return false;
	}

	DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
	DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_GZIP |
		WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
	DWORD reject_credentials = TRUE;
	const int connect_timeout = static_cast<int>(std::min<uint32_t>(
		request.connect_timeout_ms, static_cast<uint32_t>(INT_MAX)));
	const int total_timeout = static_cast<int>(std::min<uint32_t>(
		request.total_timeout_ms, static_cast<uint32_t>(INT_MAX)));
	if (!WinHttpSetOption(state->request_handle,
			WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy,
			sizeof(redirect_policy)) ||
		!WinHttpSetOption(state->request_handle,
			WINHTTP_OPTION_DECOMPRESSION, &decompression,
			sizeof(decompression)) ||
		!WinHttpSetOption(state->request_handle,
			WINHTTP_OPTION_REJECT_USERPWD_IN_URL, &reject_credentials,
			sizeof(reject_credentials)) ||
		!WinHttpSetTimeouts(state->request_handle, connect_timeout,
			connect_timeout, total_timeout, total_timeout)) {
		WinHttpCloseHandle(state->request_handle);
		WinHttpCloseHandle(state->connection);
		state->request_handle = nullptr;
		state->connection = nullptr;
		return false;
	}

	const DWORD callback_flags = WINHTTP_CALLBACK_FLAG_SENDREQUEST_COMPLETE |
		WINHTTP_CALLBACK_FLAG_HEADERS_AVAILABLE |
		WINHTTP_CALLBACK_FLAG_DATA_AVAILABLE |
		WINHTTP_CALLBACK_FLAG_READ_COMPLETE |
		WINHTTP_CALLBACK_FLAG_REQUEST_ERROR |
		WINHTTP_CALLBACK_FLAG_HANDLES;
	if (WinHttpSetStatusCallback(state->request_handle, WinHttpCallback,
		callback_flags, 0) == WINHTTP_INVALID_STATUS_CALLBACK) {
		WinHttpCloseHandle(state->request_handle);
		WinHttpCloseHandle(state->connection);
		state->request_handle = nullptr;
		state->connection = nullptr;
		return false;
	}

	{
		std::lock_guard<std::mutex> registry_lock(g_registry_mutex);
		g_registry[state->transport_id] = state;
	}
	bool shutting_down = false;
	{
		std::lock_guard<std::mutex> owner_lock(owner->mutex);
		shutting_down = owner->shutting_down;
		if (!shutting_down) {
			owner->active[request.request_id] = state;
			++owner->open_request_handles;
		}
	}
	if (shutting_down) {
		UnregisterTransport(state->transport_id);
		WinHttpCloseHandle(state->request_handle);
		WinHttpCloseHandle(state->connection);
		state->request_handle = nullptr;
		state->connection = nullptr;
		return false;
	}

	std::wstring headers;
	if (!owner->user_agent.empty()) {
		headers = L"User-Agent: " + owner->user_agent + L"\r\n";
	}
	if (request.has_post_data && !request.content_type.empty()) {
		const std::wstring content_type = Utf8ToWide(request.content_type);
		if (content_type.empty()) {
			CompleteRequest(state, RaHttpTransportResult::ClientError,
				"invalid Content-Type encoding");
			return true;
		}
		headers += L"Content-Type: " + content_type + L"\r\n";
	}
	void *optional_data = request.has_post_data && !request.post_data.empty() ?
		const_cast<char *>(state->request.post_data.data()) :
		WINHTTP_NO_REQUEST_DATA;
	const DWORD optional_size = request.has_post_data ?
		static_cast<DWORD>(request.post_data.size()) : 0;
	if (!WinHttpSendRequest(state->request_handle,
		headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
		headers.empty() ? 0 : static_cast<DWORD>(headers.size()),
		optional_data, optional_size, optional_size, state->transport_id)) {
		const DWORD error = GetLastError();
		if (error != ERROR_IO_PENDING) FailAsyncCall(state, error);
	}
	return true;
}

class RaWinHttpClient final : public RaHttpClient {
public:
	explicit RaWinHttpClient(const std::string& user_agent)
		: owner_(std::make_shared<WinHttpOwnerState>())
	{
		owner_->user_agent = Utf8ToWide(user_agent);
		owner_->session = WinHttpOpen(owner_->user_agent.empty() ?
			L"XM8" : owner_->user_agent.c_str(),
			WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
			WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
		if (owner_->session == nullptr) {
			owner_->session = WinHttpOpen(owner_->user_agent.empty() ?
				L"XM8" : owner_->user_agent.c_str(),
				WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
				WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
		}
	}

	~RaWinHttpClient() override
	{
		std::vector<std::shared_ptr<WinHttpRequestState>> requests;
		{
			std::lock_guard<std::mutex> lock(owner_->mutex);
			owner_->shutting_down = true;
			for (const auto& entry : owner_->active) {
				requests.push_back(entry.second);
			}
			owner_->active.clear();
		}
		for (const auto& state : requests) {
			{
				std::lock_guard<std::mutex> lock(state->mutex);
				state->canceled = true;
			}
			CloseRequestHandle(state);
		}
		{
			std::unique_lock<std::mutex> lock(owner_->mutex);
			owner_->closed_condition.wait(lock, [this]() {
				return owner_->open_request_handles == 0;
			});
		}
		if (owner_->session != nullptr) {
			WinHttpCloseHandle(owner_->session);
			owner_->session = nullptr;
		}
	}

	bool Ready() const { return owner_->session != nullptr; }

	void Send(const RaHttpRequest& request) override
	{
		Cancel(request.request_id);
		if (!StartRequest(owner_, request, request.url, 0, GetTickCount64())) {
			CompleteImmediate(owner_, request,
				RaHttpTransportResult::ClientError,
				"invalid HTTPS URL or WinHTTP request setup failed");
		}
	}

	void Cancel(uint64_t request_id) override
	{
		std::shared_ptr<WinHttpRequestState> state;
		{
			std::lock_guard<std::mutex> lock(owner_->mutex);
			const auto it = owner_->active.find(request_id);
			if (it == owner_->active.end()) {
				return;
			}
			state = it->second;
		}
		{
			std::lock_guard<std::mutex> lock(state->mutex);
			state->canceled = true;
		}
		CompleteRequest(state, RaHttpTransportResult::Canceled, "canceled");
	}

	void CancelAll() override
	{
		std::vector<uint64_t> request_ids;
		{
			std::lock_guard<std::mutex> lock(owner_->mutex);
			for (const auto& entry : owner_->active) {
				request_ids.push_back(entry.first);
			}
		}
		for (const uint64_t request_id : request_ids) {
			Cancel(request_id);
		}
	}

	void DrainCompleted(std::vector<RaHttpResponse> *output) override
	{
		// WinHTTP's phase timeouts do not bound the whole request (including
		// proxy discovery, TLS retries and redirects). Enforce that budget
		// when the application polls, without completing under the owner lock.
		std::vector<std::shared_ptr<WinHttpRequestState>> expired;
		const ULONGLONG now = GetTickCount64();
		{
			std::lock_guard<std::mutex> lock(owner_->mutex);
			for (const auto& entry : owner_->active) {
				const auto& state = entry.second;
				if (state->request.total_timeout_ms != 0 &&
					now - state->started_at >= state->request.total_timeout_ms) {
					expired.push_back(state);
				}
			}
		}
		for (const auto& state : expired) {
			CompleteRequest(state, RaHttpTransportResult::Timeout,
				"request exceeded total timeout");
		}
		std::lock_guard<std::mutex> lock(owner_->mutex);
		if (output == nullptr) {
			owner_->completed.clear();
			return;
		}
		output->insert(output->end(),
			std::make_move_iterator(owner_->completed.begin()),
			std::make_move_iterator(owner_->completed.end()));
		owner_->completed.clear();
	}

private:
	std::shared_ptr<WinHttpOwnerState> owner_;
};

} // namespace

std::unique_ptr<RaHttpClient> CreateWinRaHttpClient(
	const std::string& user_agent)
{
	std::unique_ptr<RaWinHttpClient> client(new RaWinHttpClient(user_agent));
	if (!client->Ready()) {
		return nullptr;
	}
	return std::unique_ptr<RaHttpClient>(client.release());
}

} // namespace Xm8Ra

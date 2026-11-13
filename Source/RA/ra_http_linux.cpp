#include "ra_http_linux.h"

#include <curl/curl.h>

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Xm8Ra {

namespace {

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

RaHttpTransportResult ClassifyCurlError(CURLcode code)
{
	switch (code) {
	case CURLE_OPERATION_TIMEDOUT:
		return RaHttpTransportResult::Timeout;
	case CURLE_COULDNT_RESOLVE_PROXY:
	case CURLE_COULDNT_RESOLVE_HOST:
	case CURLE_COULDNT_CONNECT:
	case CURLE_SEND_ERROR:
	case CURLE_RECV_ERROR:
	case CURLE_GOT_NOTHING:
	case CURLE_SSL_CONNECT_ERROR:
	case CURLE_HTTP2:
		return RaHttpTransportResult::RetryableClientError;
	default:
		return RaHttpTransportResult::ClientError;
	}
}

struct LinuxRequestState {
	RaHttpRequest request;
	CURL *easy = nullptr;
	curl_slist *headers = nullptr;
	std::vector<uint8_t> body;
	char error[CURL_ERROR_SIZE] = {};
	bool oversize = false;
};

size_t WriteResponse(char *data, size_t size, size_t count, void *context)
{
	LinuxRequestState *state = static_cast<LinuxRequestState *>(context);
	if (state == nullptr || size != 0 && count > static_cast<size_t>(-1) / size) {
		return 0;
	}
	const size_t bytes = size * count;
	if (bytes > state->request.max_response_bytes -
		std::min(state->request.max_response_bytes, state->body.size())) {
		state->oversize = true;
		return 0;
	}
	state->body.insert(state->body.end(),
		reinterpret_cast<uint8_t *>(data),
		reinterpret_cast<uint8_t *>(data) + bytes);
	return bytes;
}

class RaLinuxHttpClient final : public RaHttpClient {
public:
	explicit RaLinuxHttpClient(const std::string& user_agent) :
		user_agent_(user_agent)
	{
		worker_ = std::thread(&RaLinuxHttpClient::WorkerMain, this);
	}

	~RaLinuxHttpClient() override
	{
		{
			std::lock_guard<std::mutex> lock(mutex_);
			stopping_ = true;
			cancel_all_ = true;
		}
		condition_.notify_one();
		if (worker_.joinable()) worker_.join();
	}

	void Send(const RaHttpRequest& request) override
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (stopping_) return;
		if (known_ids_.find(request.request_id) != known_ids_.end()) {
			RaHttpResponse response;
			response.request_id = request.request_id;
			response.transport_result = RaHttpTransportResult::ClientError;
			response.error = "duplicate request id";
			completed_.push_back(std::move(response));
			return;
		}
		known_ids_.insert(request.request_id);
		pending_.push_back(request);
		condition_.notify_one();
	}

	void Cancel(uint64_t request_id) override
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!stopping_ && known_ids_.find(request_id) != known_ids_.end()) {
			canceled_ids_.insert(request_id);
			condition_.notify_one();
		}
	}

	void CancelAll() override
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!stopping_) {
			cancel_all_ = true;
			condition_.notify_one();
		}
	}

	void DrainCompleted(std::vector<RaHttpResponse> *output) override
	{
		if (output == nullptr) return;
		std::lock_guard<std::mutex> lock(mutex_);
		output->insert(output->end(),
			std::make_move_iterator(completed_.begin()),
			std::make_move_iterator(completed_.end()));
		completed_.clear();
	}

private:
	void Complete(uint64_t request_id, RaHttpTransportResult result,
		int status, const std::string& content_type,
		std::vector<uint8_t>&& body, const std::string& error)
	{
		RaHttpResponse response;
		response.request_id = request_id;
		response.transport_result = result;
		response.http_status = status;
		response.content_type = content_type;
		response.body = std::move(body);
		response.error = error;
		std::lock_guard<std::mutex> lock(mutex_);
		known_ids_.erase(request_id);
		completed_.push_back(std::move(response));
	}

	void CompleteCanceled(uint64_t request_id)
	{
		Complete(request_id, RaHttpTransportResult::Canceled, 0,
			std::string(), std::vector<uint8_t>(), "request canceled");
	}

	bool AddRequest(CURLM *multi, const RaHttpRequest& request,
		std::unordered_map<CURL *, std::unique_ptr<LinuxRequestState>>& active,
		std::unordered_map<uint64_t, CURL *>& active_by_id)
	{
		if (!StartsWithHttps(request.url)) {
			Complete(request.request_id, RaHttpTransportResult::ClientError, 0,
				std::string(), std::vector<uint8_t>(), "HTTPS URL required");
			return false;
		}

		std::unique_ptr<LinuxRequestState> state(new LinuxRequestState());
		state->request = request;
		state->easy = curl_easy_init();
		if (state->easy == nullptr) {
			Complete(request.request_id, RaHttpTransportResult::ClientError, 0,
				std::string(), std::vector<uint8_t>(), "curl_easy_init failed");
			return false;
		}

		CURL *easy = state->easy;
		curl_easy_setopt(easy, CURLOPT_URL, state->request.url.c_str());
		curl_easy_setopt(easy, CURLOPT_USERAGENT, user_agent_.c_str());
		curl_easy_setopt(easy, CURLOPT_ERRORBUFFER, state->error);
		curl_easy_setopt(easy, CURLOPT_PRIVATE, state.get());
		curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, WriteResponse);
		curl_easy_setopt(easy, CURLOPT_WRITEDATA, state.get());
		curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
		curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS,
			static_cast<long>(request.connect_timeout_ms));
		curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS,
			static_cast<long>(request.total_timeout_ms));
		if (request.purpose == RaHttpPurpose::Api) {
			// A connection retained across a guest interface outage can keep
			// timing out after reachability returns. API/outbox requests must
			// not inherit that connection; image downloads may still reuse it.
			curl_easy_setopt(easy, CURLOPT_FRESH_CONNECT, 1L);
			curl_easy_setopt(easy, CURLOPT_FORBID_REUSE, 1L);
		}
		curl_easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, "");
		curl_easy_setopt(easy, CURLOPT_SSL_VERIFYPEER, 1L);
		curl_easy_setopt(easy, CURLOPT_SSL_VERIFYHOST, 2L);
		curl_easy_setopt(easy, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
		curl_easy_setopt(easy, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);

		if (request.purpose == RaHttpPurpose::Image) {
			curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
			curl_easy_setopt(easy, CURLOPT_MAXREDIRS, 5L);
			curl_easy_setopt(easy, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
		} else {
			curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 0L);
		}

		if (request.has_post_data) {
			curl_easy_setopt(easy, CURLOPT_POST, 1L);
			curl_easy_setopt(easy, CURLOPT_POSTFIELDS,
				state->request.post_data.c_str());
			curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE_LARGE,
				static_cast<curl_off_t>(state->request.post_data.size()));
			if (!request.content_type.empty()) {
				const std::string header = "Content-Type: " + request.content_type;
				state->headers = curl_slist_append(state->headers, header.c_str());
				curl_easy_setopt(easy, CURLOPT_HTTPHEADER, state->headers);
			}
		}

		const CURLMcode added = curl_multi_add_handle(multi, easy);
		if (added != CURLM_OK) {
			if (state->headers != nullptr) curl_slist_free_all(state->headers);
			curl_easy_cleanup(easy);
			state->easy = nullptr;
			Complete(request.request_id, RaHttpTransportResult::ClientError, 0,
				std::string(), std::vector<uint8_t>(), curl_multi_strerror(added));
			return false;
		}
		active_by_id[request.request_id] = easy;
		active[easy] = std::move(state);
		return true;
	}

	void RemoveActive(CURLM *multi, CURL *easy,
		std::unordered_map<CURL *, std::unique_ptr<LinuxRequestState>>& active,
		std::unordered_map<uint64_t, CURL *>& active_by_id,
		bool canceled, CURLcode result = CURLE_OK)
	{
		const auto found = active.find(easy);
		if (found == active.end()) return;
		std::unique_ptr<LinuxRequestState> state = std::move(found->second);
		active.erase(found);
		active_by_id.erase(state->request.request_id);
		curl_multi_remove_handle(multi, easy);

		long status = 0;
		char *content_type = nullptr;
		curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &status);
		curl_easy_getinfo(easy, CURLINFO_CONTENT_TYPE, &content_type);
		const std::string response_content_type =
			content_type == nullptr ? "" : content_type;
		if (state->headers != nullptr) curl_slist_free_all(state->headers);
		curl_easy_cleanup(easy);
		state->easy = nullptr;

		if (canceled) {
			CompleteCanceled(state->request.request_id);
			return;
		}
		if (state->oversize) {
			Complete(state->request.request_id, RaHttpTransportResult::Oversize,
				static_cast<int>(status), response_content_type,
				std::vector<uint8_t>(), "response exceeds size limit");
			return;
		}
		const RaHttpTransportResult transport = result == CURLE_OK ?
			RaHttpTransportResult::Success : ClassifyCurlError(result);
		std::string error;
		if (result != CURLE_OK) {
			error = state->error[0] != '\0' ? state->error : curl_easy_strerror(result);
		}
		Complete(state->request.request_id, transport, static_cast<int>(status),
			response_content_type,
			std::move(state->body), error);
	}

	void WorkerMain()
	{
		CURLM *multi = curl_multi_init();
		if (multi == nullptr) {
			std::deque<RaHttpRequest> failed;
			{
				std::unique_lock<std::mutex> lock(mutex_);
				condition_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
				failed.swap(pending_);
			}
			for (const RaHttpRequest& request : failed) {
				Complete(request.request_id, RaHttpTransportResult::ClientError, 0,
					std::string(), std::vector<uint8_t>(), "curl_multi_init failed");
			}
			return;
		}

		std::unordered_map<CURL *, std::unique_ptr<LinuxRequestState>> active;
		std::unordered_map<uint64_t, CURL *> active_by_id;
		for (;;) {
			std::deque<RaHttpRequest> pending;
			std::unordered_set<uint64_t> canceled;
			bool cancel_all = false;
			bool stopping = false;
			{
				std::unique_lock<std::mutex> lock(mutex_);
				if (active.empty() && pending_.empty() && canceled_ids_.empty() &&
					!cancel_all_ && !stopping_) {
					condition_.wait(lock);
				}
				pending.swap(pending_);
				canceled.swap(canceled_ids_);
				cancel_all = cancel_all_;
				cancel_all_ = false;
				stopping = stopping_;
			}

			for (const RaHttpRequest& request : pending) {
				if (stopping || cancel_all ||
					canceled.erase(request.request_id) != 0) {
					CompleteCanceled(request.request_id);
				} else {
					AddRequest(multi, request, active, active_by_id);
				}
			}

			if (cancel_all || stopping) {
				std::vector<CURL *> handles;
				for (const auto& item : active) handles.push_back(item.first);
				for (CURL *easy : handles) {
					RemoveActive(multi, easy, active, active_by_id, true);
				}
			} else {
				for (uint64_t request_id : canceled) {
					const auto found = active_by_id.find(request_id);
					if (found != active_by_id.end()) {
						RemoveActive(multi, found->second, active, active_by_id, true);
					}
				}
			}

			if (stopping) break;

			int running = 0;
			curl_multi_perform(multi, &running);
			int messages = 0;
			while (CURLMsg *message = curl_multi_info_read(multi, &messages)) {
				if (message->msg == CURLMSG_DONE) {
					RemoveActive(multi, message->easy_handle, active,
						active_by_id, false, message->data.result);
				}
			}

			if (!active.empty()) {
				int descriptors = 0;
				curl_multi_poll(multi, nullptr, 0, 50, &descriptors);
			}
		}
		curl_multi_cleanup(multi);
	}

	std::string user_agent_;
	std::mutex mutex_;
	std::condition_variable condition_;
	std::deque<RaHttpRequest> pending_;
	std::unordered_set<uint64_t> canceled_ids_;
	std::unordered_set<uint64_t> known_ids_;
	std::vector<RaHttpResponse> completed_;
	bool cancel_all_ = false;
	bool stopping_ = false;
	std::thread worker_;
};

} // namespace

std::unique_ptr<RaHttpClient> CreateLinuxRaHttpClient(
	const std::string& user_agent)
{
	static const CURLcode initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
	if (initialized != CURLE_OK) return nullptr;
	return std::unique_ptr<RaHttpClient>(new RaLinuxHttpClient(user_agent));
}

} // namespace Xm8Ra

#include "ra_http_android.h"

#include "ra_callback_lifetime.h"

#include "xm8jni.h"

#include <jni.h>

#include <algorithm>
#include <atomic>
#include <iterator>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Xm8Ra {
namespace {

class RaAndroidHttpClient;
std::mutex g_client_mutex;
std::atomic<uint64_t> g_next_transport_id(1);

struct AndroidRequestOwner {
	std::shared_ptr<RaCallbackLifetime> lifetime;
	uint64_t request_id = 0;
};
std::unordered_map<uint64_t, AndroidRequestOwner> g_requests;

class RaAndroidHttpClient final : public RaHttpClient {
public:
	explicit RaAndroidHttpClient(const std::string& user_agent) :
		user_agent_(user_agent), lifetime_(new RaCallbackLifetime(this))
	{
	}

	~RaAndroidHttpClient() override
	{
		CancelAll();
		{
			std::lock_guard<std::mutex> lock(g_client_mutex);
			for (auto it = g_requests.begin(); it != g_requests.end();) {
				it = it->second.lifetime == lifetime_ ?
					g_requests.erase(it) : std::next(it);
			}
		}
		lifetime_->CloseAndWait();
	}

	void Send(const RaHttpRequest& request) override
	{
		if (request.url.compare(0, 8, "https://") != 0) {
			Complete(request.request_id, RaHttpTransportResult::ClientError, 0,
				std::string(), std::vector<uint8_t>(), "HTTPS URL required");
			return;
		}
		{
			std::lock_guard<std::mutex> lock(mutex_);
			if (!known_.insert(request.request_id).second) {
				CompleteLocked(request.request_id, RaHttpTransportResult::ClientError, 0,
					std::string(), std::vector<uint8_t>(), "duplicate request id");
				return;
			}
		}
		const uint64_t transport_id = g_next_transport_id.fetch_add(1);
		{
			std::lock_guard<std::mutex> lock(g_client_mutex);
			g_requests[transport_id] = { lifetime_, request.request_id };
		}
		{
			std::lock_guard<std::mutex> lock(mutex_);
			transport_by_request_[request.request_id] = transport_id;
		}
		const char *post = request.has_post_data ? request.post_data.c_str() : nullptr;
		const char *type = request.content_type.empty() ? nullptr : request.content_type.c_str();
		if (!Android_RaHttpSend(transport_id, request.url.c_str(), post, type,
			user_agent_.c_str(),
			static_cast<int>(request.connect_timeout_ms),
			static_cast<int>(request.total_timeout_ms),
			static_cast<int>(std::min<size_t>(request.max_response_bytes, 0x7fffffff)))) {
			Complete(request.request_id, RaHttpTransportResult::ClientError, 0,
				std::string(), std::vector<uint8_t>(), "Android HTTP bridge unavailable");
		}
	}

	void Cancel(uint64_t request_id) override
	{
		uint64_t transport_id = 0;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			const auto found = transport_by_request_.find(request_id);
			if (found != transport_by_request_.end()) transport_id = found->second;
		}
		if (transport_id != 0) Android_RaHttpCancel(transport_id);
	}
	void CancelAll() override
	{
		std::vector<uint64_t> transport_ids;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			for (const auto& item : transport_by_request_) transport_ids.push_back(item.second);
		}
		for (uint64_t transport_id : transport_ids) Android_RaHttpCancel(transport_id);
	}

	void DrainCompleted(std::vector<RaHttpResponse> *output) override
	{
		if (output == nullptr) return;
		std::lock_guard<std::mutex> lock(mutex_);
		output->insert(output->end(), std::make_move_iterator(completed_.begin()),
			std::make_move_iterator(completed_.end()));
		completed_.clear();
	}

	void Complete(uint64_t request_id, RaHttpTransportResult result, int status,
		const std::string& content_type, std::vector<uint8_t>&& body,
		const std::string& error)
	{
		{
			std::lock_guard<std::mutex> lock(mutex_);
			CompleteLocked(request_id, result, status, content_type, std::move(body), error);
		}
		std::lock_guard<std::mutex> client_lock(g_client_mutex);
		for (auto it = g_requests.begin(); it != g_requests.end();) {
			it = it->second.lifetime == lifetime_ &&
				it->second.request_id == request_id ?
				g_requests.erase(it) : std::next(it);
		}
	}

private:
	void CompleteLocked(uint64_t request_id, RaHttpTransportResult result, int status,
		const std::string& content_type, std::vector<uint8_t>&& body,
		const std::string& error)
	{
		if (known_.erase(request_id) == 0) return;
		transport_by_request_.erase(request_id);
		RaHttpResponse response;
		response.request_id = request_id;
		response.transport_result = result;
		response.http_status = status;
		response.content_type = content_type;
		response.body = std::move(body);
		response.error = error;
		completed_.push_back(std::move(response));
	}

	std::string user_agent_;
	std::shared_ptr<RaCallbackLifetime> lifetime_;
	std::mutex mutex_;
	std::unordered_set<uint64_t> known_;
	std::unordered_map<uint64_t, uint64_t> transport_by_request_;
	std::vector<RaHttpResponse> completed_;
};

} // namespace

std::unique_ptr<RaHttpClient> CreateAndroidRaHttpClient(const std::string& user_agent)
{
	return std::unique_ptr<RaHttpClient>(new RaAndroidHttpClient(user_agent));
}

} // namespace Xm8Ra

extern "C" JNIEXPORT void JNICALL Java_net_retropc_pi_XM8_nativeRaHttpComplete(
	JNIEnv *env, jclass, jlong request_id, jint result, jint status,
	jstring content_type, jbyteArray body, jstring error)
{
	Xm8Ra::AndroidRequestOwner owner;
	{
		std::lock_guard<std::mutex> client_lock(Xm8Ra::g_client_mutex);
		const auto found = Xm8Ra::g_requests.find(static_cast<uint64_t>(request_id));
		if (found == Xm8Ra::g_requests.end()) return;
		owner = found->second;
		Xm8Ra::g_requests.erase(found);
	}
	if (!owner.lifetime) return;
	Xm8Ra::RaCallbackLifetime::Lease lease = owner.lifetime->TryAcquire();
	Xm8Ra::RaAndroidHttpClient *client =
		lease.Owner<Xm8Ra::RaAndroidHttpClient>();
	if (client == nullptr) return;
	std::string type;
	std::string message;
	if (content_type != nullptr) {
		const char *text = env->GetStringUTFChars(content_type, nullptr);
		if (text != nullptr) { type = text; env->ReleaseStringUTFChars(content_type, text); }
	}
	if (error != nullptr) {
		const char *text = env->GetStringUTFChars(error, nullptr);
		if (text != nullptr) { message = text; env->ReleaseStringUTFChars(error, text); }
	}
	std::vector<uint8_t> bytes;
	if (body != nullptr) {
		const jsize size = env->GetArrayLength(body);
		if (size > 0) { bytes.resize(static_cast<size_t>(size)); env->GetByteArrayRegion(body, 0, size, reinterpret_cast<jbyte *>(bytes.data())); }
	}
	const int value = static_cast<int>(result);
	const Xm8Ra::RaHttpTransportResult transport = value >= 0 && value <= 5 ?
		static_cast<Xm8Ra::RaHttpTransportResult>(value) : Xm8Ra::RaHttpTransportResult::ClientError;
	client->Complete(owner.request_id, transport, static_cast<int>(status),
		type, std::move(bytes), message);
}

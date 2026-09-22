#include "ra_rc_client_http.h"

#include <vector>

namespace Xm8Ra {

RaRcClientHttpBridge::RaRcClientHttpBridge(RaHttpClient *http_client)
	: http_client_(http_client)
{
}

RaRcClientHttpBridge::~RaRcClientHttpBridge()
{
	AbortAllWithoutCallbacks();
}

uint64_t RaRcClientHttpBridge::BeginServerCall(const rc_api_request_t *request,
	rc_client_server_callback_t callback, void *callback_data)
{
	if (callback == nullptr) {
		return 0;
	}

	if (http_client_ == nullptr || request == nullptr || request->url == nullptr) {
		rc_api_server_response_t server_response = {};
		server_response.http_status_code = RC_API_SERVER_RESPONSE_CLIENT_ERROR;
		callback(&server_response, callback_data);
		return 0;
	}

	RaHttpRequest http_request;
	http_request.request_id = next_request_id_++;
	http_request.purpose = RaHttpPurpose::Api;
	http_request.url = request->url;
	if (request->post_data != nullptr) {
		http_request.has_post_data = true;
		http_request.post_data = request->post_data;
	}
	if (request->content_type != nullptr) {
		http_request.content_type = request->content_type;
	}

	PendingCall pending;
	pending.callback = callback;
	pending.callback_data = callback_data;
	pending.generation = current_generation_;
	pending_[http_request.request_id] = pending;
	last_issued_request_id_ = http_request.request_id;

	http_client_->Send(http_request);
	return http_request.request_id;
}

void RaRcClientHttpBridge::DrainCompleted()
{
	if (http_client_ == nullptr) {
		return;
	}

	std::vector<RaHttpResponse> completed;
	http_client_->DrainCompleted(&completed);
	for (const auto& response : completed) {
		auto pending = pending_.find(response.request_id);
		if (pending == pending_.end()) {
			continue;
		}

		const PendingCall call = pending->second;
		pending_.erase(pending);
		if (call.generation != current_generation_) {
			continue;
		}

		// Several rcheevos JSON helpers honor body_length while locating
		// fields, then pass a field pointer to libc string functions. Keep a
		// NUL sentinel immediately after the transport bytes for that API
		// contract without including it in body_length.
		std::vector<char> terminated_body;
		if (!response.body.empty()) {
			terminated_body.assign(response.body.begin(), response.body.end());
			terminated_body.push_back('\0');
		}
		rc_api_server_response_t server_response = {};
		server_response.body = terminated_body.empty() ?
			nullptr : terminated_body.data();
		server_response.body_length = response.body.size();
		server_response.http_status_code = HttpStatusForTransportResult(
			response.transport_result, response.http_status);
		call.callback(&server_response, call.callback_data);
	}
}

void RaRcClientHttpBridge::Cancel(uint64_t request_id)
{
	if (http_client_ != nullptr) {
		http_client_->Cancel(request_id);
	}
}

bool RaRcClientHttpBridge::Abandon(uint64_t request_id)
{
	auto pending = pending_.find(request_id);
	if (pending == pending_.end()) {
		return false;
	}
	pending_.erase(pending);
	if (http_client_ != nullptr) {
		http_client_->Cancel(request_id);
	}
	return true;
}

void RaRcClientHttpBridge::CancelAll()
{
	if (http_client_ != nullptr) {
		http_client_->CancelAll();
	}
}

void RaRcClientHttpBridge::AbortAllWithoutCallbacks()
{
	pending_.clear();
	if (http_client_ != nullptr) {
		http_client_->DrainCompleted(nullptr);
	}
}

void RaRcClientHttpBridge::AdvanceGeneration()
{
	current_generation_++;
}

size_t RaRcClientHttpBridge::PendingCount() const
{
	return pending_.size();
}

uint64_t RaRcClientHttpBridge::CurrentGeneration() const
{
	return current_generation_;
}

uint64_t RaRcClientHttpBridge::LastIssuedRequestId() const
{
	return last_issued_request_id_;
}

void RC_CCONV RaRcClientHttpBridge::ServerCall(const rc_api_request_t *request,
	rc_client_server_callback_t callback, void *callback_data,
	rc_client_t *client)
{
	RaRcClientHttpBridge *bridge = client == nullptr ? nullptr :
		static_cast<RaRcClientHttpBridge *>(rc_client_get_userdata(client));
	if (bridge == nullptr) {
		if (callback != nullptr) {
			rc_api_server_response_t server_response = {};
			server_response.http_status_code = RC_API_SERVER_RESPONSE_CLIENT_ERROR;
			callback(&server_response, callback_data);
		}
		return;
	}

	bridge->BeginServerCall(request, callback, callback_data);
}

int RaRcClientHttpBridge::HttpStatusForTransportResult(
	RaHttpTransportResult result, int http_status)
{
	switch (result) {
	case RaHttpTransportResult::Success:
		return http_status;
	case RaHttpTransportResult::RetryableClientError:
	case RaHttpTransportResult::Timeout:
		return RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
	case RaHttpTransportResult::ClientError:
	case RaHttpTransportResult::Canceled:
	case RaHttpTransportResult::Oversize:
	default:
		return RC_API_SERVER_RESPONSE_CLIENT_ERROR;
	}
}

} // namespace Xm8Ra

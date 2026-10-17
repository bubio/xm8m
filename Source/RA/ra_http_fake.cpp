#include "ra_http_fake.h"

namespace Xm8Ra {

void FakeRaHttpClient::Send(const RaHttpRequest& request)
{
	sent_requests_.push_back(request);
}

void FakeRaHttpClient::Cancel(uint64_t request_id)
{
	canceled_.insert(request_id);

	RaHttpResponse response;
	response.request_id = request_id;
	response.transport_result = RaHttpTransportResult::Canceled;
	response.error = "canceled";
	completed_.push_back(response);
}

void FakeRaHttpClient::CancelAll()
{
	cancel_all_ = true;
	for (const auto& request : sent_requests_) {
		if (!IsCanceled(request.request_id)) {
			Cancel(request.request_id);
		}
	}
}

void FakeRaHttpClient::DrainCompleted(std::vector<RaHttpResponse> *output)
{
	if (output == nullptr) {
		completed_.clear();
		return;
	}
	output->insert(output->end(), completed_.begin(), completed_.end());
	completed_.clear();
}

void FakeRaHttpClient::Complete(const RaHttpResponse& response)
{
	if (cancel_all_ || IsCanceled(response.request_id)) {
		return;
	}
	InjectCompletion(response);
}

void FakeRaHttpClient::InjectCompletion(const RaHttpResponse& response)
{
	completed_.push_back(response);
}

bool FakeRaHttpClient::IsCanceled(uint64_t request_id) const
{
	return canceled_.find(request_id) != canceled_.end();
}

} // namespace Xm8Ra

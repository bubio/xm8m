#ifndef XM8_RA_HTTP_FAKE_H
#define XM8_RA_HTTP_FAKE_H

#include "ra_http_client.h"

#include <set>

namespace Xm8Ra {

class FakeRaHttpClient : public RaHttpClient {
public:
	void Send(const RaHttpRequest& request) override;
	void Cancel(uint64_t request_id) override;
	void CancelAll() override;
	void DrainCompleted(std::vector<RaHttpResponse> *output) override;

	const std::vector<RaHttpRequest>& SentRequests() const
	{
		return sent_requests_;
	}

	void Complete(const RaHttpResponse& response);
	// Deliver even after cancellation, modeling a response already in flight.
	void InjectCompletion(const RaHttpResponse& response);
	bool IsCanceled(uint64_t request_id) const;

private:
	std::vector<RaHttpRequest> sent_requests_;
	std::vector<RaHttpResponse> completed_;
	std::set<uint64_t> canceled_;
	bool cancel_all_ = false;
};

} // namespace Xm8Ra

#endif

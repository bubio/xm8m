#ifndef XM8_RA_RC_CLIENT_HTTP_H
#define XM8_RA_RC_CLIENT_HTTP_H

#include "ra_http_client.h"

#include "rc_client.h"

#include <cstdint>
#include <map>

namespace Xm8Ra {

class RaRcClientHttpBridge {
public:
	explicit RaRcClientHttpBridge(RaHttpClient *http_client);
	~RaRcClientHttpBridge();

	RaRcClientHttpBridge(const RaRcClientHttpBridge&) = delete;
	RaRcClientHttpBridge& operator=(const RaRcClientHttpBridge&) = delete;

	uint64_t BeginServerCall(const rc_api_request_t *request,
		rc_client_server_callback_t callback, void *callback_data);
	void DrainCompleted();
	void Cancel(uint64_t request_id);
	bool Abandon(uint64_t request_id);
	void CancelAll();
	void AbortAllWithoutCallbacks();
	void AdvanceGeneration();

	size_t PendingCount() const;
	uint64_t CurrentGeneration() const;
	uint64_t LastIssuedRequestId() const;

	static void RC_CCONV ServerCall(const rc_api_request_t *request,
		rc_client_server_callback_t callback, void *callback_data,
		rc_client_t *client);

	static int HttpStatusForTransportResult(RaHttpTransportResult result,
		int http_status);

private:
	struct PendingCall {
		rc_client_server_callback_t callback = nullptr;
		void *callback_data = nullptr;
		uint64_t generation = 0;
	};

	RaHttpClient *http_client_ = nullptr;
	uint64_t next_request_id_ = 1;
	uint64_t current_generation_ = 1;
	uint64_t last_issued_request_id_ = 0;
	std::map<uint64_t, PendingCall> pending_;
};

} // namespace Xm8Ra

#endif

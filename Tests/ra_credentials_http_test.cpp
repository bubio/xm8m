#include "ra_credentials.h"
#include "ra_file_util.h"
#include "ra_http_fake.h"
#ifdef __APPLE__
#include "ra_http_mac.h"
#elif defined(_WIN32)
#include "ra_connectivity.h"
#include "ra_http_win.h"
#endif
#include "ra_rc_client_http.h"

#include "rc_api_request.h"
#include "rc_client.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, const char *message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		failures++;
	}
}

bool MakeDirectory(const std::string& path)
{
	return Xm8Ra::EnsureRaDirectoryTree(path);
}

std::string LongString(size_t size, char value)
{
	return std::string(size, value);
}

struct CallbackCapture {
	int calls = 0;
	int http_status = 0;
	std::string body;
	bool body_null_terminated = false;
};

void RC_CCONV CaptureServerResponse(
	const rc_api_server_response_t *server_response, void *callback_data)
{
	CallbackCapture *capture = static_cast<CallbackCapture *>(callback_data);
	capture->calls++;
	capture->http_status = server_response->http_status_code;
	capture->body.assign(server_response->body != nullptr ? server_response->body : "",
		server_response->body_length);
	capture->body_null_terminated = server_response->body != nullptr &&
		server_response->body[server_response->body_length] == '\0';
}

uint32_t RC_CCONV ReadNoMemory(uint32_t, uint8_t*, uint32_t, rc_client_t*)
{
	return 0;
}

} // namespace

int main()
{
	const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
	const char *temporary = std::getenv(
#ifdef _WIN32
		"TEMP"
#else
		"TMPDIR"
#endif
	);
	const std::string base = std::string(temporary != nullptr ? temporary :
#ifdef _WIN32
		"."
#else
		"/tmp"
#endif
	) + "/xm8-ra-credentials-http-" + std::to_string(unique);

	Check(MakeDirectory(base), "create test root");
	Xm8Ra::RaPlatformCredentialsStore store(base);

	std::string error;
	Xm8Ra::RaCredentials saved;
	saved.username = "player";
	saved.token = "token-secret";
	Check(store.Save(saved, &error), "save credentials");
	if (!error.empty()) {
		std::cerr << error << '\n';
	}

	Xm8Ra::RaCredentials loaded;
	Check(store.Load(&loaded, &error), "load credentials");
	Check(loaded.username == saved.username, "loaded username");
	Check(loaded.token == saved.token, "loaded token");

	Check(!store.Save({ LongString(257, 'u'), "token" }, &error),
		"reject oversized username");
	Check(!store.Save({ "player", LongString(4097, 't') }, &error),
		"reject oversized token");

	Check(store.Save(saved, &error), "save credentials again");
	store.ClearSecret(&loaded);
	Check(loaded.token.empty(), "clear in-memory token");
	Check(store.Delete(&error), "delete credentials");
	Check(!store.Load(&loaded, &error), "deleted credentials not loaded");

	Xm8Ra::FakeRaHttpClient http;
	Xm8Ra::RaHttpRequest get;
	get.request_id = 1;
	get.url = "https://retroachievements.org/API/test";
	http.Send(get);

	Xm8Ra::RaHttpRequest empty_post;
	empty_post.request_id = 2;
	empty_post.url = get.url;
	empty_post.has_post_data = true;
	empty_post.content_type = "application/x-www-form-urlencoded";
	http.Send(empty_post);

	Xm8Ra::RaHttpRequest normal_post = empty_post;
	normal_post.request_id = 3;
	normal_post.post_data = "u=player&t=token-secret";
	http.Send(normal_post);
	normal_post.post_data = "mutated";

	Check(http.SentRequests().size() == 3, "fake captured requests");
	Check(!http.SentRequests()[0].has_post_data, "GET has no post data");
	Check(http.SentRequests()[1].has_post_data &&
		http.SentRequests()[1].post_data.empty(),
		"empty POST remains POST");
	Check(http.SentRequests()[2].post_data == "u=player&t=token-secret",
		"request body is owned copy");

	Xm8Ra::RaHttpResponse response;
	response.request_id = 1;
	response.http_status = 204;
	http.Complete(response);
	http.Cancel(2);
	response.request_id = 2;
	response.http_status = 200;
	http.Complete(response);

	std::vector<Xm8Ra::RaHttpResponse> completed;
	http.DrainCompleted(&completed);
	Check(completed.size() == 2, "drain completed responses");
	Check(completed[0].request_id == 1 &&
		completed[0].transport_result == Xm8Ra::RaHttpTransportResult::Success,
		"success response drained");
	Check(completed[1].request_id == 2 &&
		completed[1].transport_result == Xm8Ra::RaHttpTransportResult::Canceled,
		"cancel response drained once");

	Xm8Ra::FakeRaHttpClient bridge_http;
	Xm8Ra::RaRcClientHttpBridge bridge(&bridge_http);
	rc_api_request_t api_request = {};
	api_request.url = "https://retroachievements.org/API/API_GetGame";
	api_request.post_data = "";
	api_request.content_type = "application/x-www-form-urlencoded";
	CallbackCapture callback_capture;
	bridge.BeginServerCall(&api_request, CaptureServerResponse, &callback_capture);
	Check(bridge_http.SentRequests().size() == 1, "bridge sends request");
	Check(bridge_http.SentRequests()[0].has_post_data &&
		bridge_http.SentRequests()[0].post_data.empty(),
		"bridge preserves empty POST");
	Check(bridge.PendingCount() == 1, "bridge tracks pending request");

	Xm8Ra::RaHttpResponse bridge_response;
	bridge_response.request_id = bridge.LastIssuedRequestId();
	bridge_response.http_status = 200;
	const std::string response_body = "{\"Success\":true}";
	bridge_response.body.assign(response_body.begin(), response_body.end());
	bridge_http.Complete(bridge_response);
	bridge.DrainCompleted();
	Check(callback_capture.calls == 1, "bridge invokes callback once");
	Check(callback_capture.http_status == 200, "bridge forwards HTTP status");
	Check(callback_capture.body == response_body, "bridge forwards response body");
	Check(callback_capture.body_null_terminated,
		"bridge adds NUL sentinel after response body");
	Check(bridge.PendingCount() == 0, "bridge clears completed pending request");

	CallbackCapture abandoned_capture;
	const uint64_t abandoned_request_id = bridge.BeginServerCall(
		&api_request, CaptureServerResponse, &abandoned_capture);
	Check(bridge.Abandon(abandoned_request_id),
		"bridge abandons an owned pending request");
	bridge.DrainCompleted();
	Check(abandoned_capture.calls == 0 && bridge.PendingCount() == 0 &&
		bridge_http.IsCanceled(abandoned_request_id),
		"abandoned request is canceled without invoking its callback");

	CallbackCapture stale_capture;
	bridge.BeginServerCall(&api_request, CaptureServerResponse, &stale_capture);
	const uint64_t stale_request_id = bridge.LastIssuedRequestId();
	Check(bridge.PendingCount() == 1, "bridge tracks stale-generation request");
	bridge.AdvanceGeneration();
	Xm8Ra::RaHttpResponse stale_response;
	stale_response.request_id = stale_request_id;
	stale_response.http_status = 200;
	stale_response.body.assign(response_body.begin(), response_body.end());
	bridge_http.Complete(stale_response);
	bridge.DrainCompleted();
	Check(stale_capture.calls == 0, "stale-generation response is discarded");
	Check(bridge.PendingCount() == 0, "stale-generation request is cleared");

	Check(Xm8Ra::RaRcClientHttpBridge::HttpStatusForTransportResult(
		Xm8Ra::RaHttpTransportResult::Timeout, 0) ==
		RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR,
		"timeout maps to retryable client error");
	Check(Xm8Ra::RaRcClientHttpBridge::HttpStatusForTransportResult(
		Xm8Ra::RaHttpTransportResult::Oversize, 0) ==
		RC_API_SERVER_RESPONSE_CLIENT_ERROR,
		"oversize maps to client error");
	Check(Xm8Ra::RaRcClientHttpBridge::HttpStatusForTransportResult(
		Xm8Ra::RaHttpTransportResult::Success, 503) == 503,
		"HTTP status preserved for successful transport");

	CallbackCapture cancel_capture;
	api_request.post_data = nullptr;
	bridge.BeginServerCall(&api_request, CaptureServerResponse, &cancel_capture);
	const uint64_t cancel_request_id = bridge.LastIssuedRequestId();
	bridge.Cancel(cancel_request_id);
	bridge.DrainCompleted();
	Check(cancel_capture.calls == 1, "cancel invokes callback");
	Check(cancel_capture.http_status == RC_API_SERVER_RESPONSE_CLIENT_ERROR,
		"cancel maps to client error response");

	CallbackCapture orphan_capture;
	Xm8Ra::RaRcClientHttpBridge::ServerCall(&api_request, CaptureServerResponse,
		&orphan_capture, nullptr);
	Check(orphan_capture.calls == 1 &&
		orphan_capture.http_status == RC_API_SERVER_RESPONSE_CLIENT_ERROR,
		"server call without bridge fails synchronously");

	Xm8Ra::FakeRaHttpClient userdata_http;
	Xm8Ra::RaRcClientHttpBridge userdata_bridge(&userdata_http);
	rc_client_t *client = rc_client_create(ReadNoMemory,
		Xm8Ra::RaRcClientHttpBridge::ServerCall);
	Check(client != nullptr, "create rc_client for bridge test");
	if (client != nullptr) {
		rc_client_set_userdata(client, &userdata_bridge);
		CallbackCapture userdata_capture;
		Xm8Ra::RaRcClientHttpBridge::ServerCall(&api_request, CaptureServerResponse,
			&userdata_capture, client);
		Check(userdata_http.SentRequests().size() == 1,
			"server call uses bridge from rc_client userdata");
		Xm8Ra::RaHttpResponse userdata_response;
		userdata_response.request_id = userdata_bridge.LastIssuedRequestId();
		userdata_response.transport_result =
			Xm8Ra::RaHttpTransportResult::RetryableClientError;
		userdata_http.Complete(userdata_response);
		userdata_bridge.DrainCompleted();
		Check(userdata_capture.calls == 1 &&
			userdata_capture.http_status ==
				RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR,
			"retryable transport result returned to rc_client callback");
		rc_client_destroy(client);
	}

#ifdef __APPLE__
	{
		auto mac_http = Xm8Ra::CreateMacRaHttpClient(
			"XM8/test rcheevos/test (macOS)");
		Check(mac_http != nullptr, "create macOS HTTP client");
		std::vector<Xm8Ra::RaHttpResponse> mac_completed;
		mac_http->DrainCompleted(&mac_completed);
		Check(mac_completed.empty(), "macOS HTTP empty drain");
		mac_http->CancelAll();
	}
#elif defined(_WIN32)
	{
		auto win_http = Xm8Ra::CreateWinRaHttpClient(
			"XM8/test rcheevos/test (Windows)");
		Check(win_http != nullptr, "create Windows HTTP client");
		if (win_http != nullptr) {
			Xm8Ra::RaHttpRequest invalid;
			invalid.request_id = 9001;
			invalid.url = "http://example.invalid/not-https";
			win_http->Send(invalid);
			std::vector<Xm8Ra::RaHttpResponse> win_completed;
			win_http->DrainCompleted(&win_completed);
			Check(win_completed.size() == 1 &&
				win_completed[0].transport_result ==
					Xm8Ra::RaHttpTransportResult::ClientError,
				"Windows HTTP rejects non-HTTPS URL");
			win_http->CancelAll();
		}
		auto connectivity = Xm8Ra::CreatePlatformRaConnectivityMonitor();
		Check(connectivity != nullptr, "create Windows connectivity monitor");
	}
#endif

	store.Delete(nullptr);
	Xm8Ra::RemoveRaTree(base);
	return failures == 0 ? 0 : 1;
}

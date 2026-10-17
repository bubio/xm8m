#include "ra_http_win.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <atomic>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, const char *message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
}

class LocalTcpServer {
public:
	explicit LocalTcpServer(bool send_plaintext) : send_plaintext_(send_plaintext)
	{
		listen_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (listen_ == INVALID_SOCKET) return;
		sockaddr_in address = {};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		address.sin_port = 0;
		if (bind(listen_, reinterpret_cast<sockaddr *>(&address),
			sizeof(address)) == SOCKET_ERROR || listen(listen_, 1) == SOCKET_ERROR) {
			closesocket(listen_);
			listen_ = INVALID_SOCKET;
			return;
		}
		int size = sizeof(address);
		if (getsockname(listen_, reinterpret_cast<sockaddr *>(&address), &size) ==
			SOCKET_ERROR) return;
		port_ = ntohs(address.sin_port);
		thread_ = std::thread(&LocalTcpServer::Run, this);
	}

	~LocalTcpServer()
	{
		stop_ = true;
		const SOCKET client = client_.exchange(INVALID_SOCKET);
		if (client != INVALID_SOCKET) {
			shutdown(client, SD_BOTH);
			closesocket(client);
		}
		if (listen_ != INVALID_SOCKET) {
			closesocket(listen_);
		}
		if (thread_.joinable()) thread_.join();
		listen_ = INVALID_SOCKET;
	}

	bool Valid() const { return port_ != 0; }
	bool ReceivedHandshake() const { return received_handshake_; }
	std::string Url() const
	{
		return "https://127.0.0.1:" + std::to_string(port_) + "/test";
	}

private:
	void Run()
	{
		// WinHTTP can reconnect during TLS setup. Handle each connection
		// instead of leaving retries queued behind an idle listening socket.
		while (!stop_) {
			const SOCKET client = accept(listen_, nullptr, nullptr);
			if (client == INVALID_SOCKET) return;
			client_ = client;
			char buffer[256];
			if (send_plaintext_) {
				if (recv(client, buffer, sizeof(buffer), 0) > 0) {
					received_handshake_ = true;
					const char response[] =
						"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK";
					send(client, response, static_cast<int>(sizeof(response) - 1), 0);
				}
			} else {
				while (!stop_ && recv(client, buffer, sizeof(buffer), 0) > 0) {
					received_handshake_ = true;
				}
			}
			const SOCKET finished = client_.exchange(INVALID_SOCKET);
			if (finished != INVALID_SOCKET) {
				shutdown(finished, SD_BOTH);
				closesocket(finished);
			}
		}
	}

	bool send_plaintext_ = false;
	std::atomic<bool> received_handshake_ = false;
	std::atomic<bool> stop_ = false;
	SOCKET listen_ = INVALID_SOCKET;
	std::atomic<SOCKET> client_ = INVALID_SOCKET;
	unsigned short port_ = 0;
	std::thread thread_;
};

bool WaitForResponse(Xm8Ra::RaHttpClient *client,
	Xm8Ra::RaHttpResponse *response, int timeout_ms)
{
	const auto deadline = std::chrono::steady_clock::now() +
		std::chrono::milliseconds(timeout_ms);
	while (std::chrono::steady_clock::now() < deadline) {
		std::vector<Xm8Ra::RaHttpResponse> completed;
		client->DrainCompleted(&completed);
		if (!completed.empty()) {
			*response = std::move(completed.front());
			return true;
		}
		Sleep(10);
	}
	return false;
}

} // namespace

int main()
{
	WSADATA winsock = {};
	Check(WSAStartup(MAKEWORD(2, 2), &winsock) == 0, "start Winsock");

	{
		LocalTcpServer server(true);
		Check(server.Valid(), "start invalid TLS server");
		auto client = Xm8Ra::CreateWinRaHttpClient("XM8/phase9-test");
		Xm8Ra::RaHttpRequest request;
		request.request_id = 1;
		request.url = server.Url();
		request.connect_timeout_ms = 500;
		request.total_timeout_ms = 500;
		client->Send(request);
		Xm8Ra::RaHttpResponse response;
		Check(WaitForResponse(client.get(), &response, 3000),
			"invalid TLS request completes");
		Check(server.ReceivedHandshake(), "invalid TLS server receives handshake");
		Check(response.transport_result != Xm8Ra::RaHttpTransportResult::Success &&
			response.http_status == 0,
			"invalid TLS is rejected");
	}

	{
		LocalTcpServer server(false);
		Check(server.Valid(), "start timeout server");
		auto client = Xm8Ra::CreateWinRaHttpClient("XM8/phase9-test");
		Xm8Ra::RaHttpRequest request;
		request.request_id = 2;
		request.url = server.Url();
		request.connect_timeout_ms = 200;
		request.total_timeout_ms = 200;
		const auto started = std::chrono::steady_clock::now();
		client->Send(request);
		Xm8Ra::RaHttpResponse response;
		Check(WaitForResponse(client.get(), &response, 3000),
			"stalled TLS request completes");
		Check(response.transport_result == Xm8Ra::RaHttpTransportResult::Timeout &&
			response.http_status == 0,
			"stalled TLS request fails safely");
		Check(server.ReceivedHandshake(), "timeout server receives handshake");
		Check(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(1500),
			"whole-request deadline bounds stalled TLS");
		client->Cancel(request.request_id);
		std::vector<Xm8Ra::RaHttpResponse> duplicates;
		client->DrainCompleted(&duplicates);
		Check(duplicates.empty(), "timeout completes only once");
	}

	{
		LocalTcpServer server(false);
		auto client = Xm8Ra::CreateWinRaHttpClient("XM8/phase9-test");
		Xm8Ra::RaHttpRequest request;
		request.request_id = 3;
		request.url = server.Url();
		client->Send(request);
		client->Cancel(request.request_id);
		Xm8Ra::RaHttpResponse response;
		Check(WaitForResponse(client.get(), &response, 3000),
			"canceled request completes");
		Check(response.transport_result == Xm8Ra::RaHttpTransportResult::Canceled,
			"request cancellation classified");
	}

	WSACleanup();
	return failures == 0 ? 0 : 1;
}

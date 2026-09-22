#include "ra_credentials.h"
#include "ra_disk_transaction.h"
#include "ra_file_util.h"
#include "ra_http_fake.h"
#include "ra_service.h"

#include "rc_error.h"
#include "rc_api_runtime.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>

namespace {

int failures = 0;
const char *kKnownSupportedPc8800Hash =
	"d2bf8ef1abf7bb47e54da38a286f4ac8";

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

std::string TemporaryRoot(const char *name)
{
	const auto unique = std::chrono::steady_clock::now()
		.time_since_epoch().count();
	const char *temporary = std::getenv(
#ifdef _WIN32
		"TEMP"
#else
		"TMPDIR"
#endif
	);
	return std::string(temporary != nullptr ? temporary :
#ifdef _WIN32
		"."
#else
		"/tmp"
#endif
	) + "/" + name + "-" + std::to_string(unique);
}

std::unique_ptr<Xm8Ra::FakeRaHttpClient> MakeFakeHttp()
{
	return std::unique_ptr<Xm8Ra::FakeRaHttpClient>(
		new Xm8Ra::FakeRaHttpClient());
}

Xm8Ra::RaHttpResponse MakeJsonResponseWithStatus(uint64_t request_id,
	int http_status, const std::string& json)
{
	Xm8Ra::RaHttpResponse response;
	response.request_id = request_id;
	response.http_status = http_status;
	response.transport_result = Xm8Ra::RaHttpTransportResult::Success;
	response.body.assign(json.begin(), json.end());
	return response;
}

Xm8Ra::RaHttpResponse MakeJsonResponse(uint64_t request_id,
	const std::string& json)
{
	return MakeJsonResponseWithStatus(request_id, 200, json);
}

std::string MinimalAchievementSetsJson()
{
	return "{\"Success\":true,\"GameId\":1234,\"Title\":\"Test Game\","
		"\"ConsoleId\":47,"
		"\"ImageIconUrl\":\"https://media.retroachievements.org/Images/012345.png\","
		"\"RichPresenceGameId\":1234,\"RichPresencePatch\":\"\","
		"\"Sets\":[{\"AchievementSetId\":1234,\"GameId\":1234,"
		"\"Title\":\"Test Game\",\"Type\":\"core\","
		"\"ImageIconUrl\":\"https://media.retroachievements.org/Images/012345.png\","
		"\"Achievements\":[],\"Leaderboards\":[]}]}";
}

std::string FrameAchievementSetsJson()
{
	return "{\"Success\":true,\"GameId\":1234,\"Title\":\"Frame Test\","
		"\"ConsoleId\":47,\"ImageIconUrl\":\"\","
		"\"RichPresenceGameId\":0,\"RichPresencePatch\":\"\","
		"\"Sets\":[{\"AchievementSetId\":1234,\"GameId\":1234,"
		"\"Title\":\"Frame Test\",\"Type\":\"core\",\"ImageIconUrl\":\"\","
		"\"Achievements\":[{\"ID\":42,\"Title\":\"Frame Edge\","
		"\"Description\":\"Observe an intermediate frame\",\"Flags\":3,"
		"\"Points\":5,\"MemAddr\":\"0xH0000=1\",\"Author\":\"test\","
		"\"BadgeName\":\"00001\",\"Created\":1710000000,"
		"\"Modified\":1710000000,\"Type\":\"\",\"Rarity\":100.0,"
		"\"RarityHardcore\":100.0}],\"Leaderboards\":[]}]}";
}

std::string StartSessionJson()
{
	return "{\"Success\":true,\"Unlocks\":[],\"HardcoreUnlocks\":[],"
		"\"ServerNow\":1710000000}";
}

std::string LeaderboardInfoJson()
{
	return "{\"Success\":true,\"LeaderboardData\":{"
		"\"LBID\":77,\"LBFormat\":\"SCORE\",\"LowerIsBetter\":0,"
		"\"LBTitle\":\"Fastest Clear\",\"LBDesc\":\"Finish quickly\","
		"\"LBMem\":\"STA:0xH0000=1::CAN:0xH0001=0::SUB:0xH0002=1::VAL:0xH0003\","
		"\"GameID\":1234,\"LBAuthor\":\"dev\","
		"\"LBCreated\":\"2024-03-09 16:00:00\","
		"\"LBUpdated\":\"2024-03-09 16:00:01\","
		"\"TotalEntries\":10,"
		"\"Entries\":["
		"{\"User\":\"first\",\"Rank\":1,\"Index\":1,\"Score\":1000,"
		"\"DateSubmitted\":1710000002,\"AvatarUrl\":\"\"},"
		"{\"User\":\"player\",\"Rank\":2,\"Index\":2,\"Score\":900,"
		"\"DateSubmitted\":1710000003,\"AvatarUrl\":\"\"}"
		"]}}";
}

struct FrameMemory {
	uint8_t value = 0;
	uint32_t reads = 0;
};

struct ServerCallbackCapture {
	int calls = 0;
	int http_status = 0;
	std::string body;
};

struct FakeMonotonicClock {
	uint64_t now = 1000;

	static uint64_t Read(void *userdata)
	{
		FakeMonotonicClock *clock =
			static_cast<FakeMonotonicClock *>(userdata);
		return clock != nullptr ? clock->now : 0;
	}
};

void RC_CCONV CaptureServerCallback(
	const rc_api_server_response_t *response, void *userdata)
{
	ServerCallbackCapture *capture =
		static_cast<ServerCallbackCapture *>(userdata);
	if (capture == nullptr) return;
	++capture->calls;
	capture->http_status = response != nullptr ?
		response->http_status_code : 0;
	capture->body.assign(response != nullptr && response->body != nullptr ?
		response->body : "", response != nullptr ? response->body_length : 0);
}

class MemoryPendingUnlockStore : public Xm8Ra::RaPendingUnlockStore {
public:
	bool EnqueuePendingUnlock(const Xm8Ra::RaPendingUnlockRecord& record,
		int64_t *record_id, std::string *error) override
	{
		if (fail_enqueue) {
			if (error != nullptr) *error = "forced pending enqueue failure";
			return false;
		}
		for (auto& item : records) {
			if (item.account == record.account &&
				item.achievement_id == record.achievement_id &&
				item.hardcore == record.hardcore) {
				if (record_id != nullptr) *record_id = item.id;
				return true;
			}
		}
		Xm8Ra::RaPendingUnlockRecord stored = record;
		stored.id = next_id++;
		records.push_back(stored);
		if (record_id != nullptr) *record_id = stored.id;
		return true;
	}
	bool ListPendingUnlocks(const std::string& account,
		std::vector<Xm8Ra::RaPendingUnlockRecord> *output,
		std::string *) override
	{
		output->clear();
		for (const auto& item : records) if (item.account == account)
			output->push_back(item);
		return true;
	}
	bool MarkPendingUnlockAttempt(int64_t id,
		Xm8Ra::RaPendingUnlockStatus status, const std::string& message,
		std::string *error) override
	{
		if (fail_mark) {
			if (error != nullptr) *error = "forced pending mark failure";
			return false;
		}
		for (auto& item : records) if (item.id == id) {
			++item.attempt_count;
			item.status = status;
			item.last_error = message;
			return true;
		}
		return false;
	}
	bool RemovePendingUnlock(int64_t id, std::string *) override
	{
		for (auto it = records.begin(); it != records.end(); ++it) {
			if (it->id == id) {
				records.erase(it);
				return true;
			}
		}
		return true;
	}
	bool RemovePendingUnlocksForAccount(const std::string& account,
		std::string *) override
	{
		records.erase(std::remove_if(records.begin(), records.end(),
			[&](const auto& item) { return item.account == account; }), records.end());
		return true;
	}
	bool CountPendingUnlocks(const std::string& account, size_t *count,
		std::string *error) override
	{
		if (fail_count) {
			if (error != nullptr) *error = "forced pending count failure";
			return false;
		}
		*count = 0;
		for (const auto& item : records) if (item.account == account) ++*count;
		return true;
	}
	bool RecoveryRequired(std::string *reason) const override
	{
		if (recovery_required && reason != nullptr) {
			*reason = "forced recovery confirmation";
		}
		return recovery_required;
	}
	bool ConfirmDiscardRecovery(std::string *) override
	{
		recovery_required = false;
		return true;
	}

	std::vector<Xm8Ra::RaPendingUnlockRecord> records;
	int64_t next_id = 1;
	bool fail_count = false;
	bool fail_enqueue = false;
	bool fail_mark = false;
	bool recovery_required = false;
};

class FailingDeleteCredentialsStore : public Xm8Ra::RaCredentialsStore {
public:
	bool Save(const Xm8Ra::RaCredentials& value, std::string *) override
	{
		credentials = value;
		return true;
	}
	bool Load(Xm8Ra::RaCredentials *value, std::string *) const override
	{
		if (value != nullptr) *value = credentials;
		return !credentials.token.empty();
	}
	bool Delete(std::string *error) override
	{
		delete_attempted = true;
		if (error != nullptr) *error = "forced credential delete failure";
		return false;
	}
	void ClearSecret(Xm8Ra::RaCredentials *value) const override
	{
		if (value != nullptr) value->token.clear();
	}

	Xm8Ra::RaCredentials credentials;
	bool delete_attempted = false;
};

uint32_t ReadFrameMemory(uint32_t address, uint8_t *buffer,
	uint32_t num_bytes, void *userdata)
{
	FrameMemory *memory = static_cast<FrameMemory *>(userdata);
	if (memory == nullptr || buffer == nullptr || num_bytes == 0 ||
		address != 0) {
		return 0;
	}
	memory->reads++;
	buffer[0] = memory->value;
	if (num_bytes > 1) {
		std::memset(buffer + 1, 0, num_bytes - 1);
	}
	return num_bytes;
}

} // namespace

int main()
{
	const std::string base = TemporaryRoot("xm8-ra-service");
	Check(MakeDirectory(base), "create service test root");
	Xm8Ra::RaPlatformCredentialsStore credential_store(base);
	MemoryPendingUnlockStore default_pending_unlocks;

	{
		// A Library launch must not start its Drive 2 verification before the
		// anchor game load. BeginLoadGameByHash advances the HTTP generation;
		// this recreates the former ordering bug where the verification reply
		// was discarded and the UI transaction stayed Pending forever.
		MemoryPendingUnlockStore pending_unlocks;
		auto fake_http = MakeFakeHttp();
		Xm8Ra::FakeRaHttpClient *fake_http_raw = fake_http.get();
		Xm8Ra::RaServiceOptions options;
		options.ra_root = base;
		options.credentials_store =
			Xm8Ra::CreatePlatformRaCredentialsStore(base);
		options.http_client = std::move(fake_http);
		options.pending_unlock_store = &pending_unlocks;
		Xm8Ra::RaService service(std::move(options));
		std::string error;
		const std::string auxiliary_hash =
			"11111111111111111111111111111111";

		Check(service.BeginLoginWithPassword("ordering-player", "secret", &error),
			"begin login for Library Drive 2 ordering regression");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"User\":\"ordering-player\","
			"\"Token\":\"ordering-token\",\"Score\":1,"
			"\"SoftcoreScore\":2,\"Messages\":0}"));
		service.DrainHttp();

		Check(!Xm8Ra::CanBeginAuxiliaryVerification(true, false),
			"Library Drive 2 verification is blocked before the anchor is loaded");
		Check(service.BeginVerifyMediaHashForGame(auxiliary_hash, 1234, &error),
			"unsafe pre-load Drive 2 verification starts for regression setup");
		const uint64_t pre_load_verification_request =
			service.LastIssuedRequestId();
		Check(service.BeginLoadGameByHash(kKnownSupportedPc8800Hash, &error),
			"anchor load advances the HTTP generation in regression setup");
		fake_http_raw->Complete(MakeJsonResponse(pre_load_verification_request,
			"{\"Success\":true,\"GameID\":1234}"));
		service.DrainHttp();
		Check(service.MediaVerificationSnapshot().state ==
			Xm8Ra::RaMediaChangeState::Pending,
			"pre-load verification reply is discarded after anchor load generation change");
		service.Shutdown();
	}

	{
		MemoryPendingUnlockStore pending_unlocks;
		auto fake_http = MakeFakeHttp();
		Xm8Ra::FakeRaHttpClient *fake_http_raw = fake_http.get();
		Xm8Ra::RaServiceOptions options;
		options.ra_root = base;
		options.credentials_store =
			Xm8Ra::CreatePlatformRaCredentialsStore(base);
		options.http_client = std::move(fake_http);
		options.pending_unlock_store = &pending_unlocks;
		Xm8Ra::RaService service(std::move(options));
		std::string error;
		const std::string auxiliary_hash =
			"22222222222222222222222222222222";

		Check(service.BeginLoginWithPassword("ordered-player", "secret", &error),
			"begin login for ordered Library Drive 2 verification");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"User\":\"ordered-player\","
			"\"Token\":\"ordered-token\",\"Score\":1,"
			"\"SoftcoreScore\":2,\"Messages\":0}"));
		service.DrainHttp();
		Check(service.BeginLoadGameByHash(kKnownSupportedPc8800Hash, &error),
			"begin anchor load before Drive 2 verification");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			MinimalAchievementSetsJson()));
		service.DrainHttp();
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			StartSessionJson()));
		service.DrainHttp();
		service.Idle();
		Check(service.GameSessionSnapshot().state ==
			Xm8Ra::RaGameSessionState::Loaded,
			"anchor game loads before Drive 2 verification");
		Check(Xm8Ra::CanBeginAuxiliaryVerification(true, true),
			"Library Drive 2 verification is enabled after anchor load");
		Check(service.BeginVerifyMediaHashForGame(auxiliary_hash, 1234, &error),
			"begin Drive 2 verification after anchor load");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"GameID\":1234}"));
		service.DrainHttp();
		Check(service.MediaVerificationSnapshot().state ==
			Xm8Ra::RaMediaChangeState::Succeeded,
			"post-load Drive 2 verification completes instead of remaining pending");
	}

	{
		auto fake_http = MakeFakeHttp();
		Xm8Ra::FakeRaHttpClient *fake_http_raw = fake_http.get();
		Xm8Ra::RaServiceOptions options;
		options.ra_root = base;
		options.credentials_store =
			Xm8Ra::CreatePlatformRaCredentialsStore(base);
		options.http_client = std::move(fake_http);
		options.pending_unlock_store = &default_pending_unlocks;
		Xm8Ra::RaService service(std::move(options));

		std::string error;
		Check(service.IsReady(), "service is ready");
		Check(!service.IsHardcoreEnabled(),
			"service starts with explicit Casual mode");
		service.SetHardcoreEnabled(true);
		Check(service.IsHardcoreEnabled(),
			"service enables Hardcore before loading a game");
		uint32_t frames_remaining = 999;
		Check(service.CanPause(&frames_remaining),
			"fresh Hardcore client allows its initial pause");
		service.SetHardcoreEnabled(false);
		Check(!service.IsHardcoreEnabled() && service.CanPause(),
			"Casual mode always allows pause");
		Check(service.BeginLoginWithPassword("player", "secret-password",
			&error), "begin password login");
		Check(fake_http_raw->SentRequests().size() == 1,
			"password login sends HTTP request");
		Check(fake_http_raw->SentRequests()[0].post_data.find(
			"p=secret-password") != std::string::npos,
			"password login request contains password only in HTTP body");
		Check(service.LoginSnapshot().state == Xm8Ra::RaLoginState::LoginPending,
			"password login pending");

		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"User\":\"player\",\"Token\":\"saved-token\","
			"\"Score\":10,\"SoftcoreScore\":20,\"Messages\":0}"));
		service.DrainHttp();
		const Xm8Ra::RaLoginSnapshot snapshot = service.LoginSnapshot();
		Check(snapshot.state == Xm8Ra::RaLoginState::LoggedIn,
			"password login succeeds");
		Check(snapshot.username == "player", "logged in username captured");

		Xm8Ra::RaCredentials loaded;
		Check(credential_store.Load(&loaded, &error),
			"password login saves credentials");
		Check(loaded.username == "player" && loaded.token == "saved-token",
			"saved credentials use returned token");

		service.Shutdown();
		Check(service.PendingHttpCount() == 0, "shutdown clears pending HTTP");
	}

	{
		MemoryPendingUnlockStore pending_unlocks;
		pending_unlocks.recovery_required = true;
		auto fake_http = MakeFakeHttp();
		Xm8Ra::FakeRaHttpClient *fake_http_raw = fake_http.get();
		Xm8Ra::RaServiceOptions options;
		options.ra_root = base;
		options.credentials_store =
			Xm8Ra::CreatePlatformRaCredentialsStore(base);
		options.http_client = std::move(fake_http);
		options.pending_unlock_store = &pending_unlocks;
		Xm8Ra::RaService service(std::move(options));
		std::string error;
		Check(service.BeginLoginWithPassword("recovery-player", "secret",
			&error), "begin login with pending recovery marker");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"User\":\"recovery-player\","
			"\"Token\":\"recovery-token\",\"Score\":1,"
			"\"SoftcoreScore\":2,\"Messages\":0}"));
		service.DrainHttp();
		Check(service.UnlockSyncSnapshot().state ==
			Xm8Ra::RaUnlockSyncState::Failed,
			"outbox recovery marker fails closed before game load");
		Check(!service.BeginLoadGameByHash(kKnownSupportedPc8800Hash, &error),
			"game load is blocked until recovery loss is acknowledged");
		size_t pending_count = 0;
		Check(service.HasPendingUnlocks(&pending_count, &error) &&
			pending_count == 1,
			"recovery marker is exposed as pending data during logout");
		Check(service.Logout(true, &error) &&
			!pending_unlocks.recovery_required,
			"confirmed logout clears the recovery marker");
	}

	{
		MemoryPendingUnlockStore pending_unlocks;
		auto fake_http = MakeFakeHttp();
		Xm8Ra::FakeRaHttpClient *fake_http_raw = fake_http.get();
		std::unique_ptr<FailingDeleteCredentialsStore> failing_credentials(
			new FailingDeleteCredentialsStore());
		FailingDeleteCredentialsStore *failing_credentials_raw =
			failing_credentials.get();
		Xm8Ra::RaServiceOptions options;
		options.ra_root = base;
		options.credentials_store = std::move(failing_credentials);
		options.http_client = std::move(fake_http);
		options.pending_unlock_store = &pending_unlocks;
		Xm8Ra::RaService service(std::move(options));
		std::string error;
		Check(service.BeginLoginWithPassword("credential-player", "secret",
			&error), "begin login for credential deletion failure");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"User\":\"credential-player\","
			"\"Token\":\"credential-token\",\"Score\":1,"
			"\"SoftcoreScore\":2,\"Messages\":0}"));
		service.DrainHttp();
		Check(!service.Logout(false, &error) &&
			failing_credentials_raw->delete_attempted &&
			service.LoginSnapshot().state == Xm8Ra::RaLoginState::Failed &&
			error.find("credential") != std::string::npos,
			"credential deletion failure cannot report logout success");
		Check(service.BeginLoginWithPassword("credential-player-retry", "secret",
			&error),
			"credential deletion failure leaves service ready for a new login");
		Check(failing_credentials_raw->credentials.token == "credential-token",
			"failed credential deletion remains observable for remediation");
	}

	{
		MemoryPendingUnlockStore pending_unlocks;
		Xm8Ra::RaPendingUnlockRecord record;
		record.account = "shutdown-player";
		record.achievement_id = 66;
		record.hardcore = true;
		record.game_hash = kKnownSupportedPc8800Hash;
		record.unlocked_at = static_cast<int64_t>(std::time(nullptr)) - 10;
		pending_unlocks.EnqueuePendingUnlock(record, nullptr, nullptr);
		auto fake_http = MakeFakeHttp();
		Xm8Ra::FakeRaHttpClient *fake_http_raw = fake_http.get();
		Xm8Ra::RaServiceOptions options;
		options.ra_root = base;
		options.credentials_store =
			Xm8Ra::CreatePlatformRaCredentialsStore(base);
		options.http_client = std::move(fake_http);
		options.pending_unlock_store = &pending_unlocks;
		Xm8Ra::RaService service(std::move(options));
		std::string error;
		Check(service.BeginLoginWithPassword("shutdown-player", "secret",
			&error), "begin login before pending sync shutdown");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"User\":\"shutdown-player\","
			"\"Token\":\"shutdown-token\",\"Score\":1,"
			"\"SoftcoreScore\":2,\"Messages\":0}"));
		service.DrainHttp();
		Check(service.UnlockSyncSnapshot().state ==
			Xm8Ra::RaUnlockSyncState::Pending,
			"pending unlock HTTP is active before shutdown");
		service.Shutdown();
		Check(service.PendingHttpCount() == 0 &&
			service.UnlockSyncSnapshot().state ==
				Xm8Ra::RaUnlockSyncState::None,
			"shutdown cancels and releases pending unlock sync context");
	}

	{
		MemoryPendingUnlockStore pending_unlocks;
		Xm8Ra::RaPendingUnlockRecord record;
		record.account = "player";
		record.achievement_id = 77;
		record.hardcore = true;
		record.game_hash = kKnownSupportedPc8800Hash;
		record.unlocked_at = static_cast<int64_t>(std::time(nullptr)) - 30;
		pending_unlocks.EnqueuePendingUnlock(record, nullptr, nullptr);
		record.account = "other-player";
		record.achievement_id = 88;
		pending_unlocks.EnqueuePendingUnlock(record, nullptr, nullptr);

		auto fake_http = MakeFakeHttp();
		Xm8Ra::FakeRaHttpClient *fake_http_raw = fake_http.get();
		Xm8Ra::RaServiceOptions options;
		options.ra_root = base;
		options.credentials_store =
			Xm8Ra::CreatePlatformRaCredentialsStore(base);
		options.http_client = std::move(fake_http);
		options.pending_unlock_store = &pending_unlocks;
		Xm8Ra::RaService service(std::move(options));
		std::string error;
		Check(service.BeginLoginWithPassword("player", "secret", &error),
			"begin login before restart unlock sync");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"User\":\"player\",\"Token\":\"saved-token\","
			"\"Score\":1,\"SoftcoreScore\":2,\"Messages\":0}"));
		service.DrainHttp();
		Check(service.UnlockSyncSnapshot().state ==
			Xm8Ra::RaUnlockSyncState::Pending,
			"login starts pending unlock sync before game load");
		const size_t requests_before_account_switch =
			fake_http_raw->SentRequests().size();
		Check(!service.BeginLoginWithPassword("other-player", "secret", &error),
			"logged-in account cannot be replaced without logout");
		Check(fake_http_raw->SentRequests().size() ==
			requests_before_account_switch &&
			service.UnlockSyncSnapshot().state ==
				Xm8Ra::RaUnlockSyncState::Pending,
			"rejected account switch preserves current outbox synchronization");
		const size_t requests_before_blocked_load =
			fake_http_raw->SentRequests().size();
		Check(!service.BeginLoadGameByHash(kKnownSupportedPc8800Hash, &error),
			"game load is rejected while pending unlock sync is active");
		Check(fake_http_raw->SentRequests().size() ==
			requests_before_blocked_load,
			"blocked game load sends no identification request");
		Check(fake_http_raw->SentRequests().back().post_data.find(
			"r=awardachievement") != std::string::npos &&
			fake_http_raw->SentRequests().back().post_data.find("a=77") !=
				std::string::npos &&
			fake_http_raw->SentRequests().back().post_data.find("o=") !=
				std::string::npos,
			"restart sync includes identity and seconds_since_unlock");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":false,\"Error\":\"User already has this achievement awarded.\"}"));
		service.DrainHttp();
		Check(service.UnlockSyncSnapshot().state ==
			Xm8Ra::RaUnlockSyncState::Succeeded,
			"already-unlocked response completes restart sync");
		Check(pending_unlocks.records.size() == 1 &&
			pending_unlocks.records[0].account == "other-player",
			"sync removes only the logged-in account record");
	}

	{
		MemoryPendingUnlockStore pending_unlocks;
		Xm8Ra::RaPendingUnlockRecord record;
		record.account = "retry-player";
		record.achievement_id = 78;
		record.hardcore = true;
		record.game_hash = kKnownSupportedPc8800Hash;
		record.unlocked_at = static_cast<int64_t>(std::time(nullptr)) - 30;
		pending_unlocks.EnqueuePendingUnlock(record, nullptr, nullptr);

		auto fake_http = MakeFakeHttp();
		Xm8Ra::FakeRaHttpClient *fake_http_raw = fake_http.get();
		Xm8Ra::RaServiceOptions options;
		options.ra_root = base;
		options.credentials_store =
			Xm8Ra::CreatePlatformRaCredentialsStore(base);
		options.http_client = std::move(fake_http);
		options.pending_unlock_store = &pending_unlocks;
		Xm8Ra::RaService service(std::move(options));
		std::string error;
		Check(service.BeginLoginWithPassword("retry-player", "secret", &error),
			"begin login before transient unlock sync failure");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"User\":\"retry-player\","
			"\"Token\":\"retry-token\",\"Score\":1,\"SoftcoreScore\":2,"
			"\"Messages\":0}"));
		service.DrainHttp();
		fake_http_raw->Complete(MakeJsonResponseWithStatus(
			service.LastIssuedRequestId(), 503,
			"{\"Success\":false,\"Error\":\"Maintenance\"}"));
		service.DrainHttp();
		Check(service.UnlockSyncSnapshot().state ==
			Xm8Ra::RaUnlockSyncState::None &&
			pending_unlocks.records.size() == 1 &&
			pending_unlocks.records[0].status ==
				Xm8Ra::RaPendingUnlockStatus::Pending,
			"HTTP 503 remains Pending and requests a scheduled retry");

		Check(service.BeginPendingUnlockSync(&error),
			"retry pending unlock sync after transient failure");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"AchievementID\":78}"));
		service.DrainHttp();
		Check(service.UnlockSyncSnapshot().state ==
			Xm8Ra::RaUnlockSyncState::Succeeded &&
			pending_unlocks.records.empty(),
			"transient pending unlock succeeds on retry");

		record.achievement_id = 80;
		pending_unlocks.EnqueuePendingUnlock(record, nullptr, nullptr);
		Check(service.BeginPendingUnlockSync(&error),
			"begin pending sync for mismatched success response");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"AchievementID\":999}"));
		service.DrainHttp();
		Check(service.UnlockSyncSnapshot().state ==
			Xm8Ra::RaUnlockSyncState::Failed &&
			pending_unlocks.records.size() == 1 &&
			pending_unlocks.records[0].status ==
				Xm8Ra::RaPendingUnlockStatus::Held,
			"mismatched success cannot delete a queued unlock");
		Check(pending_unlocks.RemovePendingUnlock(
			pending_unlocks.records[0].id, nullptr),
			"remove held mismatch record before permanent rejection test");

		record.achievement_id = 79;
		pending_unlocks.EnqueuePendingUnlock(record, nullptr, nullptr);
		Check(service.BeginPendingUnlockSync(&error),
			"begin pending sync for permanent rejection");
		fake_http_raw->Complete(MakeJsonResponseWithStatus(
			service.LastIssuedRequestId(), 400,
			"{\"Success\":false,\"Error\":\"Invalid achievement\"}"));
		service.DrainHttp();
		Check(pending_unlocks.records.size() == 1 &&
			pending_unlocks.records[0].status ==
				Xm8Ra::RaPendingUnlockStatus::Held,
			"permanent API rejection is held for user attention");
		Check(pending_unlocks.RemovePendingUnlock(
			pending_unlocks.records[0].id, nullptr),
			"remove held rejection before mark failure test");

		record.achievement_id = 81;
		pending_unlocks.EnqueuePendingUnlock(record, nullptr, nullptr);
		pending_unlocks.fail_mark = true;
		Check(service.BeginPendingUnlockSync(&error),
			"begin pending sync before attempt persistence failure");
		fake_http_raw->Complete(MakeJsonResponseWithStatus(
			service.LastIssuedRequestId(), 400,
			"{\"Success\":false,\"Error\":\"Invalid achievement\"}"));
		service.DrainHttp();
		std::string integrity_failure;
		Check(service.UnlockSyncSnapshot().state ==
			Xm8Ra::RaUnlockSyncState::Failed &&
			service.UnlockSyncSnapshot().message ==
				"forced pending mark failure",
			"attempt persistence failure replaces the transport failure reason");
		Check(service.TakeIntegrityFailure(&integrity_failure) &&
			integrity_failure == "forced pending mark failure",
			"attempt persistence failure is latched as an integrity failure");
	}

	{
		MemoryPendingUnlockStore pending_unlocks;
		Xm8Ra::RaPendingUnlockRecord record;
		record.account = "first-player";
		record.achievement_id = 101;
		record.hardcore = true;
		record.game_hash = kKnownSupportedPc8800Hash;
		record.unlocked_at = static_cast<int64_t>(std::time(nullptr)) - 60;
		pending_unlocks.EnqueuePendingUnlock(record, nullptr, nullptr);
		record.achievement_id = 102;
		pending_unlocks.EnqueuePendingUnlock(record, nullptr, nullptr);
		record.account = "second-player";
		record.achievement_id = 201;
		pending_unlocks.EnqueuePendingUnlock(record, nullptr, nullptr);

		auto fake_http = MakeFakeHttp();
		Xm8Ra::FakeRaHttpClient *fake_http_raw = fake_http.get();
		Xm8Ra::RaServiceOptions options;
		options.ra_root = base;
		options.credentials_store =
			Xm8Ra::CreatePlatformRaCredentialsStore(base);
		options.http_client = std::move(fake_http);
		options.pending_unlock_store = &pending_unlocks;
		Xm8Ra::RaService service(std::move(options));
		std::string error;
		Check(service.BeginLoginWithPassword("first-player", "secret", &error),
			"begin first account login for stale sync test");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"User\":\"first-player\","
			"\"Token\":\"first-token\",\"Score\":1,\"SoftcoreScore\":2,"
			"\"Messages\":0}"));
		service.DrainHttp();
		const uint64_t stale_request = service.LastIssuedRequestId();
		Check(service.UnlockSyncSnapshot().state ==
			Xm8Ra::RaUnlockSyncState::Pending,
			"first account unlock sync is pending");

		Check(service.Logout(), "logout cancels first account unlock sync");
		Check(fake_http_raw->IsCanceled(stale_request),
			"logout cancels the old account award POST at the transport");
		Check(service.BeginLoginWithPassword("second-player", "secret", &error),
			"begin second account login while old sync response is pending");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"User\":\"second-player\","
			"\"Token\":\"second-token\",\"Score\":1,\"SoftcoreScore\":2,"
			"\"Messages\":0}"));
		service.DrainHttp();
		const uint64_t second_request = service.LastIssuedRequestId();
		const size_t requests_before_stale_response =
			fake_http_raw->SentRequests().size();
		Check(service.UnlockSyncSnapshot().state ==
			Xm8Ra::RaUnlockSyncState::Pending &&
			fake_http_raw->SentRequests().back().post_data.find("u=second-player") !=
				std::string::npos,
			"second account starts only its own unlock sync");

		fake_http_raw->Complete(MakeJsonResponse(stale_request,
			"{\"Success\":true,\"AchievementID\":101}"));
		service.DrainHttp();
		Check(service.UnlockSyncSnapshot().state ==
			Xm8Ra::RaUnlockSyncState::Pending &&
			fake_http_raw->SentRequests().size() == requests_before_stale_response,
			"stale first-account callback cannot advance second-account sync");

		fake_http_raw->Complete(MakeJsonResponse(second_request,
			"{\"Success\":true,\"AchievementID\":201}"));
		service.DrainHttp();
		Check(service.UnlockSyncSnapshot().state ==
			Xm8Ra::RaUnlockSyncState::Succeeded,
			"second account sync completes after stale callback is ignored");
		Check(pending_unlocks.records.size() == 2 &&
			pending_unlocks.records[0].account == "first-player" &&
			pending_unlocks.records[1].account == "first-player",
			"account switch never submits or removes first-account unlocks");
	}

	{
		MemoryPendingUnlockStore pending_unlocks;
		Xm8Ra::RaPendingUnlockRecord record;
		record.account = "held-player";
		record.achievement_id = 301;
		record.hardcore = true;
		record.game_hash = kKnownSupportedPc8800Hash;
		record.unlocked_at = static_cast<int64_t>(std::time(nullptr)) - 15;
		record.status = Xm8Ra::RaPendingUnlockStatus::Held;
		record.last_error = "permanent award rejection";
		pending_unlocks.EnqueuePendingUnlock(record, nullptr, nullptr);

		auto fake_http = MakeFakeHttp();
		Xm8Ra::FakeRaHttpClient *fake_http_raw = fake_http.get();
		Xm8Ra::RaServiceOptions options;
		options.ra_root = base;
		options.credentials_store =
			Xm8Ra::CreatePlatformRaCredentialsStore(base);
		options.http_client = std::move(fake_http);
		options.pending_unlock_store = &pending_unlocks;
		Xm8Ra::RaService service(std::move(options));
		std::string error;
		Check(service.BeginLoginWithPassword("held-player", "secret", &error),
			"begin login with held unlock");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"User\":\"held-player\","
			"\"Token\":\"held-token\",\"Score\":1,\"SoftcoreScore\":2,"
			"\"Messages\":0}"));
		service.DrainHttp();
		Check(service.UnlockSyncSnapshot().state ==
			Xm8Ra::RaUnlockSyncState::Failed &&
			service.UnlockSyncSnapshot().message == "permanent award rejection",
			"held unlock exposes its permanent failure reason");

		size_t count = 99;
		pending_unlocks.fail_count = true;
		Check(!service.HasPendingUnlocks(&count, &error) && count == 0 &&
			error == "forced pending count failure",
			"pending count storage failure is propagated instead of becoming zero");

		Xm8Ra::RaCredentials restored_credentials;
		restored_credentials.username = "player";
		restored_credentials.token = "saved-token";
		Check(credential_store.Save(restored_credentials, &error),
			"restore saved-token fixture after account isolation tests");
	}

	{
		auto fake_http = MakeFakeHttp();
		Xm8Ra::FakeRaHttpClient *fake_http_raw = fake_http.get();
		Xm8Ra::RaServiceOptions options;
		options.ra_root = base;
		options.credentials_store =
			Xm8Ra::CreatePlatformRaCredentialsStore(base);
		options.http_client = std::move(fake_http);
		options.pending_unlock_store = &default_pending_unlocks;
		Xm8Ra::RaService service(std::move(options));

		std::string error;
		Check(service.BeginLoginWithSavedToken(&error),
			"begin saved token login");
		Check(fake_http_raw->SentRequests().size() == 1,
			"saved token login sends HTTP request");
		Check(fake_http_raw->SentRequests()[0].post_data.find(
			"t=saved-token") != std::string::npos,
			"saved token login request uses token");
		Check(fake_http_raw->SentRequests()[0].post_data.find(
			"p=") == std::string::npos,
			"saved token login does not send password field");

		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":false,\"Error\":\"Invalid token\","
			"\"Code\":\"invalid_credentials\"}"));
		service.DrainHttp();
		const Xm8Ra::RaLoginSnapshot snapshot = service.LoginSnapshot();
		Check(snapshot.state == Xm8Ra::RaLoginState::Failed,
			"saved token rejection fails login");
		Check(snapshot.credentials_deleted,
			"saved token rejection deletes credentials");

		Xm8Ra::RaCredentials loaded;
		Check(!credential_store.Load(&loaded, &error),
			"stored token removed after rejected token");
	}

	{
		Xm8Ra::RaCredentials credentials;
		credentials.username = "player";
		credentials.token = "pending-token";
		std::string error;
		Check(credential_store.Save(credentials, &error),
			"save credentials for pending token switch");

		auto fake_http = MakeFakeHttp();
		Xm8Ra::FakeRaHttpClient *fake_http_raw = fake_http.get();
		Xm8Ra::RaServiceOptions options;
		options.ra_root = base;
		options.credentials_store =
			Xm8Ra::CreatePlatformRaCredentialsStore(base);
		options.http_client = std::move(fake_http);
		options.pending_unlock_store = &default_pending_unlocks;
		Xm8Ra::RaService service(std::move(options));

		Check(service.BeginLoginWithSavedToken(&error),
			"begin saved token login before manual switch");
		Check(service.LoginSnapshot().state ==
			Xm8Ra::RaLoginState::LoginPending,
			"saved token login remains pending");
		Check(service.BeginLoginWithPassword("player", "password", &error),
			"password login aborts pending token login");
		Check(fake_http_raw->SentRequests().size() == 2,
			"manual login sends second HTTP request");
		Check(fake_http_raw->SentRequests()[1].post_data.find(
			"p=password") != std::string::npos,
			"manual switch request sends password");

		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"User\":\"player\",\"Token\":\"manual-token\","
			"\"Score\":10,\"SoftcoreScore\":20,\"Messages\":0}"));
		service.DrainHttp();
		Check(service.LoginSnapshot().state == Xm8Ra::RaLoginState::LoggedIn,
			"manual login succeeds after aborting token login");
	}

	{
		Xm8Ra::RaCredentials credentials;
		credentials.username = "player";
		credentials.token = "logout-token";
		std::string error;
		Check(credential_store.Save(credentials, &error),
			"save credentials for logout");

		auto fake_http = MakeFakeHttp();
		Xm8Ra::FakeRaHttpClient *fake_http_raw = fake_http.get();
		Xm8Ra::RaServiceOptions options;
		options.ra_root = base;
		options.credentials_store =
			Xm8Ra::CreatePlatformRaCredentialsStore(base);
		options.http_client = std::move(fake_http);
		options.pending_unlock_store = &default_pending_unlocks;
		Xm8Ra::RaService service(std::move(options));

		Check(service.BeginLoginWithSavedToken(&error),
			"begin saved token login for logout");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"User\":\"player\",\"Token\":\"logout-token\","
			"\"Score\":1,\"SoftcoreScore\":2,\"Messages\":0}"));
		service.DrainHttp();
		Check(service.LoginSnapshot().state == Xm8Ra::RaLoginState::LoggedIn,
			"saved token login succeeds before logout");

		service.Logout();
		Check(service.LoginSnapshot().state == Xm8Ra::RaLoginState::LoggedOut,
			"logout resets login state");
		Xm8Ra::RaCredentials loaded;
		Check(!credential_store.Load(&loaded, &error),
			"logout deletes saved token");
	}

	{
		Xm8Ra::RaCredentials credentials;
		credentials.username = "player";
		credentials.token = "resume-token";
		std::string error;
		Check(credential_store.Save(credentials, &error),
			"save credentials for resume login");

		auto fake_http = MakeFakeHttp();
		Xm8Ra::FakeRaHttpClient *fake_http_raw = fake_http.get();
		Xm8Ra::RaServiceOptions options;
		options.ra_root = base;
		options.credentials_store =
			Xm8Ra::CreatePlatformRaCredentialsStore(base);
		options.http_client = std::move(fake_http);
		options.pending_unlock_store = &default_pending_unlocks;
		Xm8Ra::RaService service(std::move(options));

		Check(service.BeginLoginWithSavedToken(&error),
			"begin saved token login before unload");
		service.UnloadGame();
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"User\":\"player\",\"Token\":\"resume-token\","
			"\"Score\":1,\"SoftcoreScore\":2,\"Messages\":0}"));
		service.DrainHttp();
		Check(service.LoginSnapshot().state == Xm8Ra::RaLoginState::LoggedIn,
			"unload game does not discard pending login");
	}

	{
		Xm8Ra::RaCredentials credentials;
		credentials.username = "player";
		credentials.token = "load-token";
		std::string error;
		Check(credential_store.Save(credentials, &error),
			"save credentials for load game");

		auto fake_http = MakeFakeHttp();
		Xm8Ra::FakeRaHttpClient *fake_http_raw = fake_http.get();
		Xm8Ra::RaServiceOptions options;
		options.ra_root = base;
		options.credentials_store =
			Xm8Ra::CreatePlatformRaCredentialsStore(base);
		options.http_client = std::move(fake_http);
		options.pending_unlock_store = &default_pending_unlocks;
		Xm8Ra::RaService service(std::move(options));

		Check(service.BeginLoginWithSavedToken(&error),
			"begin saved token login for load game");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"User\":\"player\",\"Token\":\"load-token\","
			"\"Score\":1,\"SoftcoreScore\":2,\"Messages\":0}"));
		service.DrainHttp();
		Check(service.LoginSnapshot().state == Xm8Ra::RaLoginState::LoggedIn,
			"saved token login succeeds before load game");

		const std::string hash = kKnownSupportedPc8800Hash;
		Check(service.BeginLibrarySync({hash}, &error),
			"begin idle library sync");
		Check(service.LibrarySyncSnapshot().state ==
			Xm8Ra::RaLibrarySyncState::PendingHashes,
			"library sync starts with hash library");
		Check(fake_http_raw->SentRequests().back().post_data.find(
			"r=hashlibrary") != std::string::npos,
			"library sync requests PC-8800 hash library");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			std::string("{\"Success\":true,\"MD5List\":{\"") + hash +
			"\":1234,\"ffffffffffffffffffffffffffffffff\":9999}}"));
		service.DrainHttp();
		Check(service.LibrarySyncSnapshot().state ==
			Xm8Ra::RaLibrarySyncState::PendingTitles,
			"library sync advances to matched titles");
		Check(service.LibrarySyncSnapshot().hashes.size() == 1 &&
			service.LibrarySyncSnapshot().hashes[0].game_id == 1234,
			"hash library is filtered to local hashes");
		Check(fake_http_raw->SentRequests().back().post_data.find(
			"r=gameinfolist") != std::string::npos,
			"library sync requests matched game titles");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"Response\":[{\"ID\":1234,"
			"\"Title\":\"Synced Game\",\"ImageIcon\":\"/Images/1234.png\","
			"\"ImageUrl\":\"https://media.example/1234.png\"}]}"));
		service.DrainHttp();
		Check(service.LibrarySyncSnapshot().state ==
			Xm8Ra::RaLibrarySyncState::PendingProgress,
			"library sync advances to all progress");
		Check(fake_http_raw->SentRequests().back().post_data.find(
			"r=allprogress") != std::string::npos,
			"library sync requests all user progress");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"Response\":{\"1234\":{"
			"\"Achievements\":10,\"Unlocked\":4,"
			"\"UnlockedHardcore\":2}}}"));
		service.DrainHttp();
		const Xm8Ra::RaLibrarySyncSnapshot library_sync =
			service.LibrarySyncSnapshot();
		Check(library_sync.state == Xm8Ra::RaLibrarySyncState::Succeeded,
			"library sync succeeds only after all three responses");
		Check(library_sync.username == "player" &&
			library_sync.titles.size() == 1 &&
			library_sync.progress.size() == 1,
			"library sync copies username, title, and progress");
		Check(library_sync.progress[0].total == 10 &&
			library_sync.progress[0].unlocked == 4 &&
			library_sync.progress[0].hardcore_unlocked == 2,
			"library sync progress values are copied");
		service.ClearLibrarySyncResult();
		Check(service.LibrarySyncSnapshot().state ==
			Xm8Ra::RaLibrarySyncState::None,
			"completed library sync result can be consumed");

		Check(service.BeginLibrarySync({hash}, &error),
			"begin library sync that will partially fail");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			std::string("{\"Success\":true,\"MD5List\":{\"") + hash +
			"\":1234}}"));
		service.DrainHttp();
		const size_t requests_before_title_failure =
			fake_http_raw->SentRequests().size();
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":false,\"Error\":\"Titles unavailable\"}"));
		service.DrainHttp();
		Check(service.LibrarySyncSnapshot().state ==
			Xm8Ra::RaLibrarySyncState::Failed,
			"partial library sync is reported as failed");
		Check(fake_http_raw->SentRequests().size() ==
			requests_before_title_failure,
			"failed title stage does not request progress");
		service.ClearLibrarySyncResult();

		Check(service.BeginLibrarySync({hash}, &error),
			"begin library sync before game start");
		const uint64_t pending_sync_request = service.LastIssuedRequestId();
		service.UnloadGame();
		fake_http_raw->Complete(MakeJsonResponse(pending_sync_request,
			std::string("{\"Success\":true,\"MD5List\":{\"") + hash +
			"\":1234}}"));
		service.DrainHttp();
		Check(service.LibrarySyncSnapshot().state ==
			Xm8Ra::RaLibrarySyncState::None,
			"game start aborts pending library sync and ignores late response");

		service.SetHardcoreEnabled(true);
		Check(service.BeginLoadGameByHash(hash, &error),
			"begin load game by hash");
		Check(service.GameSessionSnapshot().state ==
			Xm8Ra::RaGameSessionState::LoadPending,
			"load game enters pending state");
		Check(fake_http_raw->SentRequests().back().post_data.find(
			"r=achievementsets") != std::string::npos,
			"load game fetches achievement sets through rc_client");
		Check(fake_http_raw->SentRequests().back().post_data.find(
			std::string("m=") + kKnownSupportedPc8800Hash) != std::string::npos,
			"load game sends known supported PC-8800 hash");

		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			MinimalAchievementSetsJson()));
		service.DrainHttp();
		Check(fake_http_raw->SentRequests().back().post_data.find(
			"r=startsession") != std::string::npos,
			"load game starts RA session");
		Check(fake_http_raw->SentRequests().back().post_data.find(
			"g=1234") != std::string::npos,
			"start session uses identified game id");
		Check(fake_http_raw->SentRequests().back().post_data.find(
			"h=1") != std::string::npos,
			"start session sends the selected Hardcore mode");

		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			StartSessionJson()));
		service.DrainHttp();
		service.Idle();
		const Xm8Ra::RaGameSessionSnapshot snapshot =
			service.GameSessionSnapshot();
		Check(snapshot.state == Xm8Ra::RaGameSessionState::Loaded,
			"load game succeeds");
		Check(snapshot.game_id == 1234, "loaded game id captured");
		Check(snapshot.console_id == 47, "loaded console id captured");
		Check(snapshot.title == "Test Game", "loaded game title captured");
		Check(snapshot.hash == hash, "loaded game hash retained");
		Check(!snapshot.disabled_for_session,
			"successful load does not disable RA session");
		const Xm8Ra::RaAchievementListSnapshot achievements =
			service.AchievementListSnapshot();
		Check(achievements.game_loaded,
			"achievement list reports loaded game");
		Check(achievements.game_title == "Test Game",
			"achievement list captures game title");
		Check(!achievements.has_achievements,
			"empty achievement set reports no achievements");
		Check(achievements.achievements.empty(),
			"empty achievement set has no list items");

		const std::string alternate_media_hash =
			"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
		Check(service.BeginChangeMediaByHash(alternate_media_hash, &error),
			"begin media change by hash");
		Check(service.MediaChangeSnapshot().state ==
			Xm8Ra::RaMediaChangeState::Pending,
			"media change enters pending state");
		Check(fake_http_raw->SentRequests().back().post_data.find(
			"r=gameid") != std::string::npos,
			"media change resolves an unknown hash");
		Check(fake_http_raw->SentRequests().back().post_data.find(
			std::string("m=") + alternate_media_hash) != std::string::npos,
			"media change sends the requested hash");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"GameID\":1234}"));
		service.DrainHttp();
		Check(service.MediaChangeSnapshot().state ==
			Xm8Ra::RaMediaChangeState::Pending,
			"verified media hash is passed to rc_client");
		Check(fake_http_raw->SentRequests().back().post_data.find(
			"r=gameid") != std::string::npos,
			"rc_client resolves the verified media hash");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"GameID\":1234}"));
		service.DrainHttp();
		Check(service.MediaChangeSnapshot().state ==
			Xm8Ra::RaMediaChangeState::Succeeded,
			"media change succeeds after hash resolution");
		Check(service.GameSessionSnapshot().hash == alternate_media_hash,
			"successful media change updates active hash");
		service.ClearMediaChangeResult();
		Check(service.MediaChangeSnapshot().state ==
			Xm8Ra::RaMediaChangeState::None,
			"media change result can be consumed");

		const std::string auxiliary_hash =
			"eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
		const size_t requests_before_verification =
			fake_http_raw->SentRequests().size();
		Check(service.BeginVerifyMediaHashForCurrentGame(auxiliary_hash, &error),
			"begin auxiliary media verification");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"GameID\":1234}"));
		service.DrainHttp();
		Check(service.MediaVerificationSnapshot().state ==
			Xm8Ra::RaMediaChangeState::Succeeded,
			"same-game auxiliary media is verified");
		Check(fake_http_raw->SentRequests().size() ==
			requests_before_verification + 1,
			"verification does not invoke media change");
		Check(service.GameSessionSnapshot().hash == alternate_media_hash,
			"verification preserves active media hash");
		service.ClearMediaVerificationResult();
		const size_t requests_after_verification =
			fake_http_raw->SentRequests().size();
		Check(service.BeginVerifyMediaHashForCurrentGame(auxiliary_hash, &error),
			"repeat auxiliary verification uses cache");
		Check(service.IsMediaHashVerifiedForCurrentGame(auxiliary_hash),
			"verified media cache is exposed to the policy adapter");
		Check(fake_http_raw->SentRequests().size() ==
			requests_after_verification,
			"cached verification sends no request");
		service.ClearMediaVerificationResult();
		Check(service.BeginVerifyMediaHashForGame(auxiliary_hash, 1234, &error),
			"expected Game ID verification uses the same cache");
		Check(fake_http_raw->SentRequests().size() ==
			requests_after_verification,
			"expected Game ID cache sends no request");
		service.ClearMediaVerificationResult();
		Check(!service.BeginVerifyMediaHashForGame(auxiliary_hash, 9999, &error),
			"cached hash is rejected for a different expected Game ID");
		Check(service.MediaVerificationSnapshot().failure ==
			Xm8Ra::RaMediaVerificationFailure::DifferentGame,
			"expected Game ID mismatch is semantic, not unavailable");
		service.ClearMediaVerificationResult();

		const std::string auxiliary_other_game_hash =
			"ffffffffffffffffffffffffffffffff";
		Check(service.BeginVerifyMediaHashForCurrentGame(
			auxiliary_other_game_hash, &error),
			"begin different-game auxiliary verification");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"GameID\":9999}"));
		service.DrainHttp();
		Check(service.MediaVerificationSnapshot().failure ==
			Xm8Ra::RaMediaVerificationFailure::DifferentGame,
			"different-game failure is distinguished from connectivity");
		Check(service.GameSessionSnapshot().hash == alternate_media_hash,
			"rejected auxiliary media preserves active hash");
		service.ClearMediaVerificationResult();

		const std::string unavailable_auxiliary_hash =
			"cccccccccccccccccccccccccccccccc";
		Check(service.BeginVerifyMediaHashForCurrentGame(
			unavailable_auxiliary_hash, &error),
			"begin unavailable auxiliary verification");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":false,\"Error\":\"offline\"}"));
		service.DrainHttp();
		Check(service.MediaVerificationSnapshot().failure ==
			Xm8Ra::RaMediaVerificationFailure::Unavailable,
			"transport/API failure is classified as unavailable");
		service.ClearMediaVerificationResult();

		const std::string failed_media_hash =
			"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
		Check(service.BeginChangeMediaByHash(failed_media_hash, &error),
			"begin media change that will fail");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":false,\"Error\":\"Unknown media\"}"));
		service.DrainHttp();
		Check(service.MediaChangeSnapshot().state ==
			Xm8Ra::RaMediaChangeState::Failed,
			"failed media hash resolution is reported");
		Check(service.GameSessionSnapshot().hash == alternate_media_hash,
			"failed media change preserves the active hash");
		service.ClearMediaChangeResult();

		const std::string other_game_media_hash =
			"dddddddddddddddddddddddddddddddd";
		Check(service.BeginChangeMediaByHash(other_game_media_hash, &error),
			"begin media change for a different RA game");
		const size_t requests_before_other_game =
			fake_http_raw->SentRequests().size();
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"GameID\":9999}"));
		service.DrainHttp();
		Check(service.MediaChangeSnapshot().state ==
			Xm8Ra::RaMediaChangeState::Failed,
			"different RA Game ID is rejected before media change");
		Check(fake_http_raw->SentRequests().size() ==
			requests_before_other_game,
			"different-game hash is not passed to rc_client media change");
		Check(service.GameSessionSnapshot().hash == alternate_media_hash,
			"different-game media preserves active hash");
		service.ClearMediaChangeResult();

		Check(service.BeginChangeMediaByHash(hash, &error),
			"begin media rollback to a known hash");
		Check(service.MediaChangeSnapshot().state ==
			Xm8Ra::RaMediaChangeState::Succeeded,
			"known media rollback completes synchronously");
		Check(service.GameSessionSnapshot().hash == hash,
			"media rollback restores the previous hash");
		service.ClearMediaChangeResult();
		Check(!service.BeginChangeMediaByHash("not-a-md5", &error),
			"reject invalid media change hash");

		Check(service.BeginFetchLeaderboardEntries(77, 1, 5, &error),
			"begin leaderboard entry fetch");
		Check(fake_http_raw->SentRequests().back().post_data.find(
			"r=lbinfo") != std::string::npos,
			"leaderboard entry fetch uses lbinfo API");
		Check(fake_http_raw->SentRequests().back().post_data.find(
			"i=77") != std::string::npos,
			"leaderboard entry fetch sends leaderboard id");
		Check(fake_http_raw->SentRequests().back().post_data.find(
			"c=5") != std::string::npos,
			"leaderboard entry fetch sends entry count");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			LeaderboardInfoJson()));
		service.DrainHttp();
		const Xm8Ra::RaLeaderboardEntriesSnapshot leaderboard_entries =
			service.LeaderboardEntriesSnapshot();
		Check(leaderboard_entries.state ==
			Xm8Ra::RaLeaderboardEntriesState::Loaded,
			"leaderboard entries load succeeds");
		Check(leaderboard_entries.leaderboard_id == 77,
			"leaderboard entries retain leaderboard id");
		Check(leaderboard_entries.total_entries == 10,
			"leaderboard entries capture total entry count");
		Check(leaderboard_entries.entries.size() == 2,
			"leaderboard entries copy returned entries");
		Check(leaderboard_entries.entries[1].username == "player",
			"leaderboard entry username is copied");
		Check(leaderboard_entries.entries[1].display == "000900",
			"leaderboard entry display is formatted");

		const std::string pending_media_hash =
			"cccccccccccccccccccccccccccccccc";
		Check(service.BeginChangeMediaByHash(pending_media_hash, &error),
			"begin media change before unload");
		const uint64_t pending_media_request = service.LastIssuedRequestId();
		Check(service.MediaChangeSnapshot().state ==
			Xm8Ra::RaMediaChangeState::Pending,
			"media change remains pending before unload");
		service.UnloadGame();
		Check(service.MediaChangeSnapshot().state ==
			Xm8Ra::RaMediaChangeState::None,
			"unload aborts pending media change");
		fake_http_raw->Complete(MakeJsonResponse(pending_media_request,
			"{\"Success\":true,\"GameID\":1234}"));
		service.DrainHttp();
		Check(service.GameSessionSnapshot().state ==
			Xm8Ra::RaGameSessionState::NoGame,
			"unload game resets game session state");
		Check(service.BeginLoadGameByHash(
			"11111111111111111111111111111111", &error),
			"begin reload after unload");
		Check(fake_http_raw->SentRequests().back().post_data.find(
			"m=11111111111111111111111111111111") != std::string::npos,
			"reload uses new RA identification hash");
	}

	{
		auto fake_http = MakeFakeHttp();
		Xm8Ra::FakeRaHttpClient *fake_http_raw = fake_http.get();
		Xm8Ra::RaServiceOptions options;
		options.ra_root = base;
		options.credentials_store =
			Xm8Ra::CreatePlatformRaCredentialsStore(base);
		options.http_client = std::move(fake_http);
		options.pending_unlock_store = &default_pending_unlocks;
		Xm8Ra::RaService service(std::move(options));

		std::string error;
		Check(service.BeginLoginWithPassword("player", "secret", &error),
			"begin password login before failed load");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"User\":\"player\",\"Token\":\"failed-load\","
			"\"Score\":1,\"SoftcoreScore\":2,\"Messages\":0}"));
		service.DrainHttp();

		Check(!service.BeginLoadGameByHash("not-a-md5", &error),
			"reject invalid hash before RA request");
		Check(fake_http_raw->SentRequests().size() == 1,
			"invalid hash does not send HTTP request");

		Check(service.BeginLoadGameByHash(
			"fedcba9876543210fedcba9876543210", &error),
			"begin load game that will fail");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":false,\"Error\":\"Game not found\","
			"\"Code\":\"not_found\"}"));
		service.DrainHttp();
		const Xm8Ra::RaGameSessionSnapshot snapshot =
			service.GameSessionSnapshot();
		Check(snapshot.state == Xm8Ra::RaGameSessionState::DisabledForSession,
			"failed game load disables RA for session");
		Check(snapshot.disabled_for_session,
			"failed game load marks disabled flag");
		Check(!snapshot.message.empty(),
			"failed game load captures an explanatory message");
	}

	{
		MemoryPendingUnlockStore pending_unlocks;
		FakeMonotonicClock retry_clock;
		auto fake_http = MakeFakeHttp();
		Xm8Ra::FakeRaHttpClient *fake_http_raw = fake_http.get();
		Xm8Ra::RaServiceOptions options;
		options.ra_root = base;
		options.credentials_store =
			Xm8Ra::CreatePlatformRaCredentialsStore(base);
		options.http_client = std::move(fake_http);
		options.pending_unlock_store = &pending_unlocks;
		options.monotonic_millis = FakeMonotonicClock::Read;
		options.monotonic_millis_userdata = &retry_clock;
		Xm8Ra::RaService service(std::move(options));
		std::string error;
		Check(service.BeginLoginWithPassword("submission-player", "secret",
			&error), "begin login for submission ownership tests");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"User\":\"submission-player\","
			"\"Token\":\"submission-token\",\"Score\":1,"
			"\"SoftcoreScore\":2,\"Messages\":0}"));
		service.DrainHttp();

		rc_api_award_achievement_request_t award_params = {};
		award_params.username = "submission-player";
		award_params.api_token = "submission-token";
		award_params.achievement_id = 88;
		award_params.hardcore = 1;
		award_params.game_hash = kKnownSupportedPc8800Hash;
		rc_api_request_t award_request = {};
		award_params.api_token = "wrong-token";
		Check(rc_api_init_award_achievement_request(&award_request,
			&award_params) == RC_OK, "create invalid-identity award request");
		ServerCallbackCapture invalid_award_capture;
		const size_t requests_before_invalid_award =
			fake_http_raw->SentRequests().size();
		service.ServerCallForTesting(&award_request, CaptureServerCallback,
			&invalid_award_capture);
		Check(invalid_award_capture.calls == 1 &&
			invalid_award_capture.http_status == 400 &&
			fake_http_raw->SentRequests().size() ==
				requests_before_invalid_award &&
			pending_unlocks.records.empty(),
			"award with mismatched token is rejected before outbox and HTTP");
		rc_api_destroy_request(&award_request);

		award_params.api_token = "submission-token";
		award_request = {};
		Check(rc_api_init_award_achievement_request(&award_request,
			&award_params) == RC_OK, "create direct award request");
		ServerCallbackCapture award_capture;
		service.ServerCallForTesting(&award_request, CaptureServerCallback,
			&award_capture);
		const uint64_t award_request_id = service.LastIssuedRequestId();
		fake_http_raw->Complete(MakeJsonResponse(award_request_id,
			"{\"Success\":true,\"AchievementID\":999}"));
		service.DrainHttp();
		Check(award_capture.calls == 1 && award_capture.http_status == 400,
			"mismatched live award response is completed terminally once");
		Check(pending_unlocks.records.size() == 1 &&
			pending_unlocks.records[0].achievement_id == 88 &&
			pending_unlocks.records[0].status ==
				Xm8Ra::RaPendingUnlockStatus::Held,
			"mismatched live award remains held in the outbox");
		rc_api_destroy_request(&award_request);
		Check(pending_unlocks.RemovePendingUnlock(
			pending_unlocks.records[0].id, nullptr),
			"remove held live mismatch before reconnect ownership test");

		award_params.achievement_id = 89;
		award_request = {};
		Check(rc_api_init_award_achievement_request(&award_request,
			&award_params) == RC_OK,
			"create live award request before reconnect synchronization");
		ServerCallbackCapture reconnect_award_capture;
		service.ServerCallForTesting(&award_request, CaptureServerCallback,
			&reconnect_award_capture);
		const uint64_t superseded_award_id = service.LastIssuedRequestId();
		Check(service.BeginPendingUnlockSync(&error),
			"reconnect synchronization takes ownership of live award outbox row");
		Check(fake_http_raw->IsCanceled(superseded_award_id) &&
			reconnect_award_capture.calls == 1 &&
			reconnect_award_capture.http_status == 400,
			"reconnect cancels and terminally completes the superseded callback");
		const uint64_t reconnect_sync_id = service.LastIssuedRequestId();
		Check(reconnect_sync_id != superseded_award_id,
			"reconnect sends a new outbox-owned synchronization request");
		fake_http_raw->Complete(MakeJsonResponse(reconnect_sync_id,
			"{\"Success\":true,\"AchievementID\":89}"));
		service.DrainHttp();
		Check(service.UnlockSyncSnapshot().state ==
			Xm8Ra::RaUnlockSyncState::Succeeded &&
			pending_unlocks.records.empty(),
			"outbox-owned reconnect synchronization removes confirmed record");
		rc_api_destroy_request(&award_request);

		award_params.achievement_id = 90;
		award_request = {};
		Check(rc_api_init_award_achievement_request(&award_request,
			&award_params) == RC_OK,
			"create live award request before permanent rejection");
		ServerCallbackCapture rejected_award_capture;
		service.ServerCallForTesting(&award_request, CaptureServerCallback,
			&rejected_award_capture);
		fake_http_raw->Complete(MakeJsonResponseWithStatus(
			service.LastIssuedRequestId(), 400,
			"{\"Success\":false,\"Error\":\"Invalid achievement\"}"));
		service.DrainHttp();
		Check(rejected_award_capture.calls == 1 &&
			rejected_award_capture.http_status == 400 &&
			pending_unlocks.records.size() == 1 &&
			pending_unlocks.records[0].status ==
				Xm8Ra::RaPendingUnlockStatus::Held &&
			service.UnlockSyncSnapshot().state ==
				Xm8Ra::RaUnlockSyncState::Failed,
			"permanently rejected live award blocks the next sync boundary");
		const size_t requests_before_held_load =
			fake_http_raw->SentRequests().size();
		Check(!service.BeginLoadGameByHash(kKnownSupportedPc8800Hash, &error) &&
			fake_http_raw->SentRequests().size() == requests_before_held_load,
			"held live award prevents the next game load");
		Check(pending_unlocks.RemovePendingUnlock(
			pending_unlocks.records[0].id, nullptr),
			"remove held live rejection after load gate test");
		rc_api_destroy_request(&award_request);

		rc_api_submit_lboard_entry_request_t leaderboard_params = {};
		leaderboard_params.username = "submission-player";
		leaderboard_params.api_token = "submission-token";
		leaderboard_params.leaderboard_id = 77;
		leaderboard_params.score = -123;
		leaderboard_params.game_hash = kKnownSupportedPc8800Hash;
		rc_api_request_t leaderboard_request = {};
		Check(rc_api_init_submit_lboard_entry_request(&leaderboard_request,
			&leaderboard_params) == RC_OK,
			"create direct leaderboard submission request");
		ServerCallbackCapture leaderboard_capture;
		service.ServerCallForTesting(&leaderboard_request,
			CaptureServerCallback, &leaderboard_capture);
		const size_t requests_before_leaderboard =
			fake_http_raw->SentRequests().size();
		const uint64_t leaderboard_request_id = service.LastIssuedRequestId();
		fake_http_raw->Complete(MakeJsonResponseWithStatus(
			leaderboard_request_id, 503,
			"{\"Success\":false,\"Error\":\"Maintenance\"}"));
		service.DrainHttp();
		Check(leaderboard_capture.calls == 0 &&
			fake_http_raw->SentRequests().size() ==
				requests_before_leaderboard + 1,
			"first transient leaderboard response retries immediately");
		const uint64_t immediate_retry_id = service.LastIssuedRequestId();
		Check(immediate_retry_id != leaderboard_request_id &&
			fake_http_raw->SentRequests().back().post_data ==
				fake_http_raw->SentRequests()[requests_before_leaderboard - 1].post_data,
			"leaderboard retry preserves request identity and payload");
		fake_http_raw->Complete(MakeJsonResponseWithStatus(
			immediate_retry_id, 503,
			"{\"Success\":false,\"Error\":\"Maintenance\"}"));
		service.DrainHttp();
		const size_t requests_during_backoff =
			fake_http_raw->SentRequests().size();
		Check(leaderboard_capture.calls == 0 &&
			service.PendingHttpCount() == 1 &&
			service.IsProcessingRequired(),
			"second transient leaderboard response enters owned backoff");
		retry_clock.now += 999;
		service.DrainHttp();
		Check(fake_http_raw->SentRequests().size() == requests_during_backoff,
			"leaderboard retry waits for the full backoff interval");
		retry_clock.now += 1;
		service.DrainHttp();
		Check(fake_http_raw->SentRequests().size() ==
			requests_during_backoff + 1,
			"leaderboard retry resumes when the backoff expires");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"Score\":-123}"));
		service.DrainHttp();
		Check(leaderboard_capture.calls == 1 &&
			leaderboard_capture.http_status == 200 &&
			service.PendingHttpCount() == 0,
			"successful leaderboard retry reaches the original callback once");

		ServerCallbackCapture rejected_leaderboard_capture;
		const size_t requests_before_rejected_leaderboard =
			fake_http_raw->SentRequests().size();
		service.ServerCallForTesting(&leaderboard_request,
			CaptureServerCallback, &rejected_leaderboard_capture);
		fake_http_raw->Complete(MakeJsonResponseWithStatus(
			service.LastIssuedRequestId(), 400,
			"{\"Success\":false,\"Error\":\"Invalid leaderboard\"}"));
		service.DrainHttp();
		Check(rejected_leaderboard_capture.calls == 1 &&
			rejected_leaderboard_capture.http_status == 400 &&
			fake_http_raw->SentRequests().size() ==
				requests_before_rejected_leaderboard + 1 &&
			service.PendingHttpCount() == 0,
			"permanent leaderboard rejection is delivered without retrying");

		ServerCallbackCapture waiting_leaderboard_capture;
		service.ServerCallForTesting(&leaderboard_request,
			CaptureServerCallback, &waiting_leaderboard_capture);
		fake_http_raw->Complete(MakeJsonResponseWithStatus(
			service.LastIssuedRequestId(), 503,
			"{\"Success\":false,\"Error\":\"Maintenance\"}"));
		service.DrainHttp();
		fake_http_raw->Complete(MakeJsonResponseWithStatus(
			service.LastIssuedRequestId(), 503,
			"{\"Success\":false,\"Error\":\"Maintenance\"}"));
		service.DrainHttp();
		const size_t requests_before_waiting_cancel =
			fake_http_raw->SentRequests().size();
		service.UnloadGame();
		Check(waiting_leaderboard_capture.calls == 1 &&
			waiting_leaderboard_capture.http_status == 400 &&
			service.PendingHttpCount() == 0,
			"game unload terminally owns and cancels a waiting retry");
		retry_clock.now += 120000;
		service.DrainHttp();
		Check(fake_http_raw->SentRequests().size() ==
			requests_before_waiting_cancel,
			"canceled waiting leaderboard retry cannot restart later");

		ServerCallbackCapture canceled_leaderboard_capture;
		service.ServerCallForTesting(&leaderboard_request,
			CaptureServerCallback, &canceled_leaderboard_capture);
		const uint64_t canceled_leaderboard_id = service.LastIssuedRequestId();
		Check(service.Logout(),
			"logout succeeds while leaderboard submission is pending");
		Check(fake_http_raw->IsCanceled(canceled_leaderboard_id) &&
			canceled_leaderboard_capture.calls == 1 &&
			canceled_leaderboard_capture.http_status == 400,
			"logout cancels and terminally completes leaderboard submission");
		rc_api_destroy_request(&leaderboard_request);
	}

	{
		auto fake_http = MakeFakeHttp();
		Xm8Ra::FakeRaHttpClient *fake_http_raw = fake_http.get();
		FrameMemory memory;
		MemoryPendingUnlockStore pending_unlocks;
		Xm8Ra::RaServiceOptions options;
		options.ra_root = base;
		options.credentials_store =
			Xm8Ra::CreatePlatformRaCredentialsStore(base);
		options.http_client = std::move(fake_http);
		options.host_read_memory = ReadFrameMemory;
		options.host_read_memory_userdata = &memory;
		options.pending_unlock_store = &pending_unlocks;
		Xm8Ra::RaService service(std::move(options));

		std::string error;
		Check(service.BeginLoginWithPassword("player", "secret", &error),
			"begin login for frame evaluation");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"User\":\"player\",\"Token\":\"frame-token\","
			"\"Score\":1,\"SoftcoreScore\":2,\"Messages\":0}"));
		service.DrainHttp();
		Check(service.BeginLoadGameByHash(kKnownSupportedPc8800Hash, &error),
			"begin frame evaluation game load");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			FrameAchievementSetsJson()));
		service.DrainHttp();
		if (fake_http_raw->SentRequests().empty() ||
			fake_http_raw->SentRequests().back().post_data.find(
				"r=startsession") == std::string::npos) {
			std::cerr << "frame evaluation did not request startsession\n";
		}
		Check(!fake_http_raw->SentRequests().empty() &&
			fake_http_raw->SentRequests().back().post_data.find(
				"r=startsession") != std::string::npos,
			"frame evaluation starts an RA session");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			StartSessionJson()));
		service.DrainHttp();
		service.Idle();
		const Xm8Ra::RaGameSessionSnapshot frame_game =
			service.GameSessionSnapshot();
		if (frame_game.state != Xm8Ra::RaGameSessionState::Loaded) {
			std::cerr << "frame evaluation game failed to load: "
				<< frame_game.message << '\n';
		}
		Check(frame_game.state == Xm8Ra::RaGameSessionState::Loaded,
			"frame evaluation game is loaded");
		service.TakeEvents();
		std::vector<uint8_t> saved_progress;
		Check(service.SerializeProgress(&saved_progress, &error) &&
			!saved_progress.empty(), "serialize loaded RA progress");
		std::vector<uint8_t> damaged_progress = saved_progress;
		damaged_progress[0] ^= 0x80;
		Check(!service.DeserializeProgress(damaged_progress, &error),
			"reject damaged RA progress");
		Check(service.DeserializeProgress(saved_progress, &error),
			"restore valid RA progress after rejection");

		memory.value = 0;
		Check(service.DoFrame(), "evaluate first completed frame");
		Check(service.TakeEvents().empty(),
			"achievement remains locked on first frame");
		memory.value = 1;
		Check(service.DoFrame(), "evaluate second completed frame");
		const std::vector<Xm8Ra::RaEvent> frame_events = service.TakeEvents();
		bool triggered = false;
		for (const Xm8Ra::RaEvent& event : frame_events) {
			if (event.type == Xm8Ra::RaEventType::AchievementTriggered &&
				event.achievement.id == 42) {
				triggered = true;
			}
		}
		Check(triggered,
			"memory change on the second frame triggers the achievement");
		Check(pending_unlocks.records.size() == 1,
			"achievement is persisted before its HTTP request completes");
		Check(!fake_http_raw->SentRequests().empty() &&
			fake_http_raw->SentRequests().back().post_data.find(
				"r=awardachievement") != std::string::npos,
			"persisted achievement is submitted");
		Check(fake_http_raw->SentRequests().back().post_data.find(
			"t=frame-token") != std::string::npos,
			"token is used in HTTP but not represented by outbox record");
		const size_t unlock_requests_before_retry =
			fake_http_raw->SentRequests().size();
		fake_http_raw->Complete(MakeJsonResponseWithStatus(
			service.LastIssuedRequestId(), 429,
			"{\"Success\":false,\"Error\":\"Too many requests\"}"));
		service.DrainHttp();
		Check(pending_unlocks.records.size() == 1 &&
			pending_unlocks.records[0].status ==
				Xm8Ra::RaPendingUnlockStatus::Pending,
			"live HTTP 429 remains Pending instead of becoming Held");
		Check(fake_http_raw->SentRequests().size() ==
			unlock_requests_before_retry &&
			service.UnlockSyncSnapshot().state ==
				Xm8Ra::RaUnlockSyncState::None,
			"live transient failure is handed to the persistent outbox");
		Check(service.BeginPendingUnlockSync(&error),
			"restart retained live unlock through the outbox owner");
		Check(fake_http_raw->SentRequests().size() ==
			unlock_requests_before_retry + 1 &&
			fake_http_raw->SentRequests().back().post_data.find(
				"r=awardachievement") != std::string::npos,
			"explicit outbox sync sends the retained unlock");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"AchievementID\":42,\"Score\":6,"
			"\"SoftcoreScore\":2,\"AchievementsRemaining\":1}"));
		service.DrainHttp();
		Check(pending_unlocks.records.empty(),
			"successful unlock submission removes outbox row");
		Check(memory.reads >= 2,
			"each completed frame reads current emulated memory");
		service.SetHardcoreEnabled(true);
		const std::vector<Xm8Ra::RaEvent> reset_events = service.TakeEvents();
		int reset_count = 0;
		for (const Xm8Ra::RaEvent& event : reset_events) {
			if (event.type == Xm8Ra::RaEventType::ResetRequested) reset_count++;
		}
		Check(reset_count == 1,
			"enabling Hardcore with a loaded game requests exactly one reset");
		service.SetHardcoreEnabled(false);
		const std::vector<Xm8Ra::RaEvent> casual_events = service.TakeEvents();
		bool casual_requested_reset = false;
		for (const Xm8Ra::RaEvent& event : casual_events) {
			if (event.type == Xm8Ra::RaEventType::ResetRequested) {
				casual_requested_reset = true;
			}
		}
		Check(!casual_requested_reset && !service.IsHardcoreEnabled() &&
			service.GameSessionSnapshot().state ==
				Xm8Ra::RaGameSessionState::Loaded,
			"disabling Hardcore keeps the loaded game without requesting reset");
	}

	{
		auto fake_http = MakeFakeHttp();
		Xm8Ra::FakeRaHttpClient *fake_http_raw = fake_http.get();
		FrameMemory memory;
		MemoryPendingUnlockStore pending_unlocks;
		Xm8Ra::RaServiceOptions options;
		options.ra_root = base;
		options.credentials_store =
			Xm8Ra::CreatePlatformRaCredentialsStore(base);
		options.http_client = std::move(fake_http);
		options.host_read_memory = ReadFrameMemory;
		options.host_read_memory_userdata = &memory;
		options.pending_unlock_store = &pending_unlocks;
		Xm8Ra::RaService service(std::move(options));
		std::string error;

		Check(service.BeginLoginWithPassword("fail-closed-player", "secret",
			&error), "begin login for frame fail-closed test");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"User\":\"fail-closed-player\","
			"\"Token\":\"fail-closed-token\",\"Score\":1,"
			"\"SoftcoreScore\":2,\"Messages\":0}"));
		service.DrainHttp();
		Check(service.BeginLoadGameByHash(kKnownSupportedPc8800Hash, &error),
			"begin game load for frame fail-closed test");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			FrameAchievementSetsJson()));
		service.DrainHttp();
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			StartSessionJson()));
		service.DrainHttp();
		service.Idle();
		Check(service.GameSessionSnapshot().state ==
			Xm8Ra::RaGameSessionState::Loaded,
			"game is loaded before frame fail-closed test");

		memory.value = 0;
		Check(service.DoFrame(), "prime achievement before enqueue failure");
		service.TakeEvents();
		pending_unlocks.fail_enqueue = true;
		memory.value = 1;
		Check(service.DoFrame(),
			"frame that encounters enqueue failure completes evaluation");
		Check(!service.DoFrame(),
			"next frame is rejected while integrity failure is unconsumed");
		std::string integrity_failure;
		Check(service.TakeIntegrityFailure(&integrity_failure) &&
			integrity_failure == "forced pending enqueue failure",
			"enqueue failure remains available after the rejected frame");
	}

	{
		auto fake_http = MakeFakeHttp();
		Xm8Ra::FakeRaHttpClient *fake_http_raw = fake_http.get();
		FrameMemory memory;
		MemoryPendingUnlockStore pending_unlocks;
		Xm8Ra::RaServiceOptions options;
		options.ra_root = base;
		options.credentials_store =
			Xm8Ra::CreatePlatformRaCredentialsStore(base);
		options.http_client = std::move(fake_http);
		options.host_read_memory = ReadFrameMemory;
		options.host_read_memory_userdata = &memory;
		options.pending_unlock_store = &pending_unlocks;
		Xm8Ra::RaService service(std::move(options));
		std::string error;

		Check(service.BeginLoginWithPassword("player", "secret", &error),
			"begin login for pending award logout test");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			"{\"Success\":true,\"User\":\"player\","
			"\"Token\":\"logout-award-token\",\"Score\":1,"
			"\"SoftcoreScore\":2,\"Messages\":0}"));
		service.DrainHttp();
		Check(service.BeginLoadGameByHash(kKnownSupportedPc8800Hash, &error),
			"begin game load for pending award logout test");
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			FrameAchievementSetsJson()));
		service.DrainHttp();
		fake_http_raw->Complete(MakeJsonResponse(service.LastIssuedRequestId(),
			StartSessionJson()));
		service.DrainHttp();
		service.Idle();
		Check(service.GameSessionSnapshot().state ==
			Xm8Ra::RaGameSessionState::Loaded,
			"game is loaded before pending award logout test");

		memory.value = 0;
		Check(service.DoFrame(), "evaluate pre-unlock frame before logout");
		service.TakeEvents();
		memory.value = 1;
		Check(service.DoFrame(), "trigger pending award before logout");
		service.TakeEvents();
		const uint64_t pending_award_request = service.LastIssuedRequestId();
		Check(pending_unlocks.records.size() == 1 &&
			service.PendingHttpCount() == 1,
			"award HTTP and outbox row are pending before logout");

		service.Logout();
		Check(service.LoginSnapshot().state == Xm8Ra::RaLoginState::LoggedOut &&
			service.GameSessionSnapshot().state ==
				Xm8Ra::RaGameSessionState::NoGame,
			"logout completes while an award response is pending");
		Check(service.PendingHttpCount() == 0 &&
			fake_http_raw->IsCanceled(pending_award_request),
			"logout cancels and releases the live award callback");
		Check(pending_unlocks.records.size() == 1 &&
			pending_unlocks.records[0].status ==
				Xm8Ra::RaPendingUnlockStatus::Pending,
			"service logout leaves an unconfirmed award retryable");
		fake_http_raw->Complete(MakeJsonResponse(pending_award_request,
			"{\"Success\":true,\"AchievementID\":42,\"Score\":6,"
			"\"SoftcoreScore\":2,\"AchievementsRemaining\":0}"));
		service.DrainHttp();
		Check(pending_unlocks.records.size() == 1,
			"late award response cannot mutate the outbox after logout");
		bool exposed_cancellation_error = false;
		for (const Xm8Ra::RaEvent& event : service.TakeEvents()) {
			if (event.type == Xm8Ra::RaEventType::ServerError) {
				exposed_cancellation_error = true;
			}
		}
		Check(!exposed_cancellation_error,
			"internal award cancellation does not show a server error");
	}

	{
		auto fake_http = MakeFakeHttp();
		Xm8Ra::RaServiceOptions options;
		options.ra_root = base;
		options.credentials_store =
			Xm8Ra::CreatePlatformRaCredentialsStore(base);
		options.http_client = std::move(fake_http);
		options.pending_unlock_store = &default_pending_unlocks;
		Xm8Ra::RaService service(std::move(options));

		std::vector<uint8_t> progress = {1};
		std::string progress_error;
		Check(!service.SerializeProgress(&progress, &progress_error),
			"progress serialization requires a loaded game");
		Check(progress.empty(), "failed progress serialization clears output");
		Check(!service.DoFrame(), "do frame is ignored before a game is loaded");
		Check(service.Idle(), "idle is allowed before a game is loaded");
		Check(service.TakeEvents().empty(),
			"idle before game load does not synthesize RA events");

		rc_client_achievement_t achievement = {};
		achievement.id = 42;
		achievement.points = 5;
		achievement.title = "Original Title";
		achievement.description = "Original Description";
		achievement.measured_percent = 50.0f;
		std::snprintf(achievement.measured_progress,
			sizeof(achievement.measured_progress), "%s", "1/2");
		achievement.badge_url = "https://media.example/ach.png";
		achievement.badge_locked_url = "https://media.example/lock.png";

		rc_client_event_t event = {};
		event.type = RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED;
		event.achievement = &achievement;
		service.QueueEventForTesting(&event);
		achievement.title = "Mutated Title";

		std::vector<Xm8Ra::RaEvent> events = service.TakeEvents();
		Check(events.size() == 1, "achievement event is queued");
		Check(events[0].type == Xm8Ra::RaEventType::AchievementTriggered,
			"achievement event type is mapped");
		Check(events[0].achievement.id == 42,
			"achievement event id is copied");
		Check(events[0].achievement.title == "Original Title",
			"achievement event title is copied away from rcheevos pointer");
		Check(events[0].achievement.measured_progress == "1/2",
			"achievement event measured progress is copied");
		Check(service.TakeEvents().empty(),
			"take events drains the event queue");

		std::snprintf(achievement.measured_progress,
			sizeof(achievement.measured_progress), "%s", "2/3");
		event = {};
		event.type = RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_UPDATE;
		event.achievement = &achievement;
		service.QueueEventForTesting(&event);
		std::snprintf(achievement.measured_progress,
			sizeof(achievement.measured_progress), "%s", "mutated");
		events = service.TakeEvents();
		Check(events.size() == 1,
			"progress indicator event is queued");
		Check(events[0].type ==
			Xm8Ra::RaEventType::AchievementProgressIndicatorUpdate,
			"progress indicator event type is mapped");
		Check(events[0].achievement.measured_progress == "2/3",
			"progress indicator value is deep copied");

		rc_client_leaderboard_tracker_t tracker = {};
		tracker.id = 77;
		std::snprintf(tracker.display, sizeof(tracker.display), "%s", "01:23");
		event = {};
		event.type = RC_CLIENT_EVENT_LEADERBOARD_TRACKER_SHOW;
		event.leaderboard_tracker = &tracker;
		service.QueueEventForTesting(&event);
		std::snprintf(tracker.display, sizeof(tracker.display), "%s", "mutated");
		events = service.TakeEvents();
		Check(events.size() == 1,
			"leaderboard tracker event is queued");
		Check(events[0].type == Xm8Ra::RaEventType::LeaderboardTrackerShow,
			"leaderboard tracker event type is mapped");
		Check(events[0].leaderboard.id == 77 &&
			events[0].leaderboard.display == "01:23",
			"leaderboard tracker is deep copied");

		event = {};
		event.type = RC_CLIENT_EVENT_DISCONNECTED;
		service.QueueEventForTesting(&event);
		event.type = RC_CLIENT_EVENT_RECONNECTED;
		service.QueueEventForTesting(&event);
		events = service.TakeEvents();
		Check(events.size() == 2,
			"connection state events are queued in order");
		Check(events[0].type == Xm8Ra::RaEventType::Disconnected,
			"disconnected event is mapped");
		Check(events[1].type == Xm8Ra::RaEventType::Reconnected,
			"reconnected event is mapped");
	}

	{
		auto fake_http = MakeFakeHttp();
		Xm8Ra::RaServiceOptions options;
		options.ra_root = base;
		options.credentials_store =
			Xm8Ra::CreatePlatformRaCredentialsStore(base);
		options.http_client = std::move(fake_http);
		options.pending_unlock_store = &default_pending_unlocks;
		Xm8Ra::RaService service(std::move(options));

		rc_client_leaderboard_scoreboard_entry_t top_entries[2] = {};
		top_entries[0].username = "first";
		top_entries[0].rank = 1;
		std::snprintf(top_entries[0].score, sizeof(top_entries[0].score),
			"%s", "1000");
		top_entries[1].username = "second";
		top_entries[1].rank = 2;
		std::snprintf(top_entries[1].score, sizeof(top_entries[1].score),
			"%s", "900");

		rc_client_leaderboard_scoreboard_t scoreboard = {};
		scoreboard.leaderboard_id = 77;
		scoreboard.new_rank = 2;
		scoreboard.num_entries = 10;
		std::snprintf(scoreboard.submitted_score,
			sizeof(scoreboard.submitted_score), "%s", "900");
		std::snprintf(scoreboard.best_score, sizeof(scoreboard.best_score),
			"%s", "900");
		scoreboard.top_entries = top_entries;
		scoreboard.num_top_entries = 2;

		rc_client_event_t event = {};
		event.type = RC_CLIENT_EVENT_LEADERBOARD_SCOREBOARD;
		event.leaderboard_scoreboard = &scoreboard;
		service.QueueEventForTesting(&event);
		top_entries[1].username = "mutated";

		const std::vector<Xm8Ra::RaEvent> events = service.TakeEvents();
		Check(events.size() == 1, "scoreboard event is queued");
		Check(events[0].type == Xm8Ra::RaEventType::LeaderboardScoreboard,
			"scoreboard event type is mapped");
		Check(events[0].scoreboard.leaderboard_id == 77,
			"scoreboard leaderboard id is copied");
		Check(events[0].scoreboard.top_entries.size() == 2,
			"scoreboard top entries are copied");
		Check(events[0].scoreboard.top_entries[1].username == "second",
			"scoreboard entry username is copied away from rcheevos pointer");
	}

	{
		auto fake_http = MakeFakeHttp();
		Xm8Ra::RaServiceOptions options;
		options.ra_root = base;
		options.credentials_store =
			Xm8Ra::CreatePlatformRaCredentialsStore(base);
		options.http_client = std::move(fake_http);
		options.pending_unlock_store = &default_pending_unlocks;
		Xm8Ra::RaService service(std::move(options));

		char api[] = "award_achievement";
		char message[] = "request rejected";
		rc_client_server_error_t server_error = {};
		server_error.api = api;
		server_error.error_message = message;
		server_error.result = RC_API_FAILURE;
		server_error.related_id = 42;
		rc_client_event_t event = {};
		event.type = RC_CLIENT_EVENT_SERVER_ERROR;
		event.server_error = &server_error;
		service.QueueEventForTesting(&event);
		api[0] = 'X';
		message[0] = 'X';

		const std::vector<Xm8Ra::RaEvent> events = service.TakeEvents();
		Check(events.size() == 1, "server error event is queued");
		Check(events[0].type == Xm8Ra::RaEventType::ServerError,
			"server error event type is mapped");
		Check(events[0].server_error.api == "award_achievement",
			"server error API is deep copied");
		Check(events[0].server_error.message == "request rejected",
			"server error message is deep copied");
		Check(events[0].server_error.related_id == 42,
			"server error related id is copied");
	}

	{
		auto fake_http = MakeFakeHttp();
		Xm8Ra::RaServiceOptions options;
		options.ra_root = base;
		options.credentials_store =
			Xm8Ra::CreatePlatformRaCredentialsStore(base);
		options.http_client = std::move(fake_http);
		options.pending_unlock_store = &default_pending_unlocks;
		Xm8Ra::RaService service(std::move(options));

		std::string error;
		Check(service.BeginLoginWithPassword("player", "pending-secret",
			&error), "begin pending login");
		Check(service.PendingHttpCount() == 1, "pending login has HTTP call");
		service.Shutdown();
		Check(service.PendingHttpCount() == 0,
			"shutdown drains canceled request");
	}

	credential_store.Delete(nullptr);
	Xm8Ra::RemoveRaTree(base);

	if (failures != 0) {
		std::cerr << failures << " RA service test failure(s)\n";
		return 1;
	}

	std::cout << "RA service tests passed\n";
	return 0;
}

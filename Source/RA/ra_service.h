#ifndef XM8_RA_SERVICE_H
#define XM8_RA_SERVICE_H

#if defined(XM8_ENABLE_RETROACHIEVEMENTS) && \
	(defined(XM8_ENABLE_DEBUGGER) || defined(USE_DEBUGGER))
#error "The emulator debugger is forbidden in RetroAchievements builds"
#endif

#include "ra_credentials.h"
#include "ra_http_client.h"
#include "ra_pending_unlock.h"
#include "ra_rc_client_http.h"

#include "rc_client.h"

#include <memory>
#include <map>
#include <string>
#include <vector>

namespace Xm8Ra {

typedef uint32_t (*RaHostReadMemoryFunc)(uint32_t address, uint8_t *buffer,
	uint32_t num_bytes, void *userdata);
typedef uint64_t (*RaMonotonicMillisFunc)(void *userdata);

enum class RaLoginState {
	LoggedOut,
	LoginPending,
	LoggedIn,
	Failed,
};

enum class RaGameSessionState {
	NoGame,
	LoadPending,
	Loaded,
	DisabledForSession,
};

struct RaLoginSnapshot {
	RaLoginState state = RaLoginState::LoggedOut;
	int result = 0;
	std::string message;
	std::string username;
	std::string display_name;
	bool credentials_deleted = false;
};

struct RaGameSessionSnapshot {
	RaGameSessionState state = RaGameSessionState::NoGame;
	int result = 0;
	int load_state = 0;
	std::string message;
	std::string hash;
	uint32_t game_id = 0;
	uint32_t console_id = 0;
	std::string title;
	std::string badge_url;
	bool disabled_for_session = false;
};

enum class RaEventType {
	None,
	AchievementTriggered,
	LeaderboardStarted,
	LeaderboardFailed,
	LeaderboardSubmitted,
	AchievementChallengeIndicatorShow,
	AchievementChallengeIndicatorHide,
	AchievementProgressIndicatorShow,
	AchievementProgressIndicatorHide,
	AchievementProgressIndicatorUpdate,
	LeaderboardTrackerShow,
	LeaderboardTrackerHide,
	LeaderboardTrackerUpdate,
	LeaderboardScoreboard,
	ResetRequested,
	GameCompleted,
	ServerError,
	Disconnected,
	Reconnected,
	SubsetCompleted,
	RichPresenceChanged,
};

struct RaAchievementEvent {
	uint32_t id = 0;
	uint32_t points = 0;
	uint8_t state = 0;
	uint8_t category = 0;
	uint8_t bucket = 0;
	uint8_t unlocked = 0;
	uint8_t type = 0;
	float measured_percent = 0.0f;
	std::string title;
	std::string description;
	std::string measured_progress;
	std::string badge_url;
	std::string badge_locked_url;
};

struct RaAchievementListItem {
	uint32_t id = 0;
	uint32_t points = 0;
	uint8_t state = 0;
	uint8_t category = 0;
	uint8_t bucket = 0;
	uint8_t unlocked = 0;
	uint8_t type = 0;
	float measured_percent = 0.0f;
	float rarity = 0.0f;
	float rarity_hardcore = 0.0f;
	std::string title;
	std::string description;
	std::string measured_progress;
	std::string badge_url;
	std::string badge_locked_url;
	std::string bucket_label;
};

struct RaAchievementListSnapshot {
	bool game_loaded = false;
	bool has_achievements = false;
	std::string game_title;
	std::vector<RaAchievementListItem> achievements;
};

struct RaLeaderboardEvent {
	uint32_t id = 0;
	uint8_t state = 0;
	uint8_t format = 0;
	bool lower_is_better = false;
	std::string title;
	std::string description;
	std::string tracker_value;
	std::string display;
};

struct RaLeaderboardListItem {
	uint32_t id = 0;
	uint8_t state = 0;
	uint8_t format = 0;
	bool lower_is_better = false;
	std::string title;
	std::string description;
	std::string tracker_value;
	std::string bucket_label;
};

struct RaLeaderboardListSnapshot {
	bool game_loaded = false;
	bool has_leaderboards = false;
	std::string game_title;
	std::vector<RaLeaderboardListItem> leaderboards;
};

struct RaLeaderboardScoreboardEntry {
	uint32_t rank = 0;
	std::string username;
	std::string score;
};

struct RaLeaderboardScoreboardEvent {
	uint32_t leaderboard_id = 0;
	uint32_t new_rank = 0;
	uint32_t num_entries = 0;
	std::string submitted_score;
	std::string best_score;
	std::vector<RaLeaderboardScoreboardEntry> top_entries;
};

enum class RaLeaderboardEntriesState {
	None,
	FetchPending,
	Loaded,
	Failed,
};

struct RaLeaderboardEntryItem {
	uint32_t rank = 0;
	uint32_t index = 0;
	std::string username;
	std::string display;
};

struct RaLeaderboardEntriesSnapshot {
	RaLeaderboardEntriesState state = RaLeaderboardEntriesState::None;
	int result = 0;
	std::string message;
	uint32_t leaderboard_id = 0;
	uint32_t total_entries = 0;
	int32_t user_index = -1;
	std::vector<RaLeaderboardEntryItem> entries;
};

enum class RaMediaChangeState {
	None,
	Pending,
	Succeeded,
	Failed,
};

struct RaMediaChangeSnapshot {
	RaMediaChangeState state = RaMediaChangeState::None;
	int result = 0;
	std::string message;
	std::string hash;
};

enum class RaMediaVerificationFailure {
	None,
	Unavailable,
	DifferentGame,
};

struct RaMediaVerificationSnapshot {
	RaMediaChangeState state = RaMediaChangeState::None;
	RaMediaVerificationFailure failure = RaMediaVerificationFailure::None;
	int result = 0;
	std::string message;
	std::string hash;
};

enum class RaLibrarySyncState {
	None,
	PendingHashes,
	PendingTitles,
	PendingProgress,
	Succeeded,
	Failed,
};

struct RaLibrarySyncHash {
	std::string hash;
	uint32_t game_id = 0;
};

struct RaLibrarySyncTitle {
	uint32_t game_id = 0;
	std::string title;
	std::string badge_url;
};

struct RaLibrarySyncProgress {
	uint32_t game_id = 0;
	uint32_t total = 0;
	uint32_t unlocked = 0;
	uint32_t hardcore_unlocked = 0;
};

struct RaLibrarySyncSnapshot {
	RaLibrarySyncState state = RaLibrarySyncState::None;
	int result = 0;
	std::string message;
	std::string username;
	std::vector<RaLibrarySyncHash> hashes;
	std::vector<RaLibrarySyncTitle> titles;
	std::vector<RaLibrarySyncProgress> progress;
};

struct RaServerErrorEvent {
	int result = 0;
	uint32_t related_id = 0;
	std::string api;
	std::string message;
};

struct RaSubsetEvent {
	uint32_t id = 0;
	uint32_t num_achievements = 0;
	uint32_t num_leaderboards = 0;
	std::string title;
	std::string badge_url;
};

struct RaEvent {
	RaEventType type = RaEventType::None;
	uint32_t raw_type = 0;
	RaAchievementEvent achievement;
	RaLeaderboardEvent leaderboard;
	RaLeaderboardScoreboardEvent scoreboard;
	RaServerErrorEvent server_error;
	RaSubsetEvent subset;
	std::string rich_presence;
};

struct RaServiceOptions {
	std::string ra_root;
	std::unique_ptr<RaHttpClient> http_client;
	std::unique_ptr<RaCredentialsStore> credentials_store;
	rc_client_read_memory_func_t read_memory = nullptr;
	RaHostReadMemoryFunc host_read_memory = nullptr;
	void *host_read_memory_userdata = nullptr;
	std::string user_agent;
	RaPendingUnlockStore *pending_unlock_store = nullptr;
	RaMonotonicMillisFunc monotonic_millis = nullptr;
	void *monotonic_millis_userdata = nullptr;
};

class RaService {
public:
	explicit RaService(RaServiceOptions options);
	~RaService();

	RaService(const RaService&) = delete;
	RaService& operator=(const RaService&) = delete;

	bool IsReady() const;
	bool BeginLoginWithPassword(const std::string& username,
		const std::string& password, std::string *error);
	bool BeginLoginWithSavedToken(std::string *error);
	bool BeginLoadGameByHash(const std::string& hash, std::string *error);
	bool BeginPendingUnlockSync(std::string *error);
	bool BeginChangeMediaByHash(const std::string& hash, std::string *error);
	void ClearMediaChangeResult();
	void CancelMediaChange();
	bool BeginVerifyMediaHashForCurrentGame(const std::string& hash,
		std::string *error);
	bool BeginVerifyMediaHashForGame(const std::string& hash,
		uint32_t expected_game_id, std::string *error);
	void ClearMediaVerificationResult();
	void CancelMediaVerification();
	bool IsMediaHashVerifiedForCurrentGame(const std::string& hash) const;
	bool IsMediaHashVerifiedForGame(const std::string& hash,
		uint32_t expected_game_id) const;
	bool BeginLibrarySync(const std::vector<std::string>& local_hashes,
		std::string *error);
	void ClearLibrarySyncResult();
	bool BeginFetchLeaderboardEntries(uint32_t leaderboard_id,
		uint32_t first_entry, uint32_t count, std::string *error);
	void DrainHttp();
	bool DoFrame();
	bool Idle();
	bool IsProcessingRequired() const;
	bool SerializeProgress(std::vector<uint8_t> *progress,
		std::string *error) const;
	bool DeserializeProgress(const std::vector<uint8_t>& progress,
		std::string *error);
	void ResetProgress();
	void SetHardcoreEnabled(bool enabled);
	bool IsHardcoreEnabled() const;
	bool CanPause(uint32_t *frames_remaining = nullptr) const;
	std::vector<RaEvent> TakeEvents();
	void UnloadGame();
	bool Logout(bool delete_pending = false, std::string *error = nullptr);
	void Shutdown();

	RaLoginSnapshot LoginSnapshot() const;
	RaGameSessionSnapshot GameSessionSnapshot() const;
	RaMediaChangeSnapshot MediaChangeSnapshot() const;
	RaMediaVerificationSnapshot MediaVerificationSnapshot() const;
	RaLibrarySyncSnapshot LibrarySyncSnapshot() const;
	RaUnlockSyncSnapshot UnlockSyncSnapshot() const;
	bool TakeIntegrityFailure(std::string *message);
	bool HasPendingUnlocks(size_t *count, std::string *error) const;
	bool DeletePendingUnlocks(std::string *error);
	std::string RichPresence() const;
	RaAchievementListSnapshot AchievementListSnapshot() const;
	RaLeaderboardListSnapshot LeaderboardListSnapshot() const;
	RaLeaderboardEntriesSnapshot LeaderboardEntriesSnapshot() const;
	size_t PendingHttpCount() const;
	uint64_t LastIssuedRequestId() const;
	const RaHttpClient *HttpClientForTesting() const;
	RaHttpClient *HttpClientForTesting();
	void QueueEventForTesting(const rc_client_event_t *event);
	void ServerCallForTesting(const rc_api_request_t *request,
		rc_client_server_callback_t callback, void *callback_data);

private:
	struct PendingAwardContext;
	struct PendingLeaderboardContext;
	struct PendingSyncContext;

	enum class LoginKind {
		None,
		Password,
		SavedToken,
	};

	static uint32_t RC_CCONV ReadNoMemory(uint32_t address, uint8_t *buffer,
		uint32_t num_bytes, rc_client_t *client);
	static uint32_t RC_CCONV ReadHostMemory(uint32_t address, uint8_t *buffer,
		uint32_t num_bytes, rc_client_t *client);
	static void RC_CCONV LoginCallback(int result, const char *error_message,
		rc_client_t *client, void *userdata);
	static void RC_CCONV LoadGameCallback(int result, const char *error_message,
		rc_client_t *client, void *userdata);
	static void RC_CCONV MediaChangeCallback(int result,
		const char *error_message, rc_client_t *client, void *userdata);
	static void RC_CCONV ResolveMediaHashCallback(
		const rc_api_server_response_t *server_response, void *userdata);
	static void RC_CCONV VerifyMediaHashCallback(
		const rc_api_server_response_t *server_response, void *userdata);
	static void RC_CCONV LibraryHashesCallback(int result,
		const char *error_message, rc_client_hash_library_t *list,
		rc_client_t *client, void *userdata);
	static void RC_CCONV LibraryTitlesCallback(int result,
		const char *error_message, rc_client_game_title_list_t *list,
		rc_client_t *client, void *userdata);
	static void RC_CCONV LibraryProgressCallback(int result,
		const char *error_message, rc_client_all_user_progress_t *list,
		rc_client_t *client, void *userdata);
	static void RC_CCONV LeaderboardEntriesCallback(int result,
		const char *error_message, rc_client_leaderboard_entry_list_t *list,
		rc_client_t *client, void *userdata);
	static void RC_CCONV ClientEventHandler(const rc_client_event_t *event,
		rc_client_t *client);
	static void RC_CCONV ServerCall(const rc_api_request_t *request,
		rc_client_server_callback_t callback, void *callback_data,
		rc_client_t *client);
	static void RC_CCONV PendingAwardCallback(
		const rc_api_server_response_t *server_response, void *userdata);
	static void RC_CCONV PendingLeaderboardCallback(
		const rc_api_server_response_t *server_response, void *userdata);
	static void RC_CCONV PendingSyncCallback(
		const rc_api_server_response_t *server_response, void *userdata);

	void HandleLoginCallback(int result, const char *error_message);
	bool InterceptAwardRequest(const rc_api_request_t *request,
		rc_client_server_callback_t callback, void *callback_data);
	bool InterceptLeaderboardSubmission(const rc_api_request_t *request,
		rc_client_server_callback_t callback, void *callback_data);
	bool StartPendingLeaderboardRequest(PendingLeaderboardContext *context);
	void ProcessPendingLeaderboardRetries();
	void CancelPendingAwardRequests();
	void CancelPendingLeaderboardRequests();
	void CancelPendingUnlockSyncRequests();
	void CompleteCanceledPendingAward(PendingAwardContext *context,
		const char *message = nullptr);
	void CompleteCanceledPendingLeaderboard(PendingLeaderboardContext *context,
		const char *message = nullptr);
	void CompleteSubmissionCallback(rc_client_server_callback_t callback,
		void *callback_data, const char *message);
	void StartNextPendingUnlock();
	void InvalidatePendingUnlockSync();
	void HandleLoadGameCallback(int result, const char *error_message);
	void HandleMediaChangeCallback(int result, const char *error_message);
	void HandleResolveMediaHashCallback(
		const rc_api_server_response_t *server_response);
	void HandleVerifyMediaHashCallback(
		const rc_api_server_response_t *server_response);
	bool StartClientMediaChange(std::string *error);
	void HandleLibraryHashesCallback(int result, const char *error_message,
		rc_client_hash_library_t *list);
	void HandleLibraryTitlesCallback(int result, const char *error_message,
		rc_client_game_title_list_t *list);
	void HandleLibraryProgressCallback(int result, const char *error_message,
		rc_client_all_user_progress_t *list);
	bool StartNextLibraryTitleBatch(std::string *error);
	bool StartLibraryProgress(std::string *error);
	void FailLibrarySync(int result, const char *message);
	void HandleLeaderboardEntriesCallback(int result, const char *error_message,
		rc_client_leaderboard_entry_list_t *list);
	void HandleClientEvent(const rc_client_event_t *event);
	void UpdateRichPresenceEvent();
	void SetFailed(int result, const std::string& message);
	void DisableGameSession(int result, const std::string& message);
	void DeleteCredentialsForRejectedToken();
	void AbortLoginInProgress();
	void AbortLeaderboardEntriesInProgress();
	void AbortMediaChangeInProgress();
	void AbortLibrarySyncInProgress();

	std::unique_ptr<RaHttpClient> http_client_;
	std::unique_ptr<RaCredentialsStore> credentials_;
	std::unique_ptr<RaRcClientHttpBridge> http_bridge_;
	RaPendingUnlockStore *pending_unlock_store_ = nullptr;
	RaMonotonicMillisFunc monotonic_millis_ = nullptr;
	void *monotonic_millis_userdata_ = nullptr;
	rc_client_t *client_ = nullptr;
	RaHostReadMemoryFunc host_read_memory_ = nullptr;
	void *host_read_memory_userdata_ = nullptr;
	LoginKind login_kind_ = LoginKind::None;
	rc_client_async_handle_t *login_async_handle_ = nullptr;
	rc_client_async_handle_t *leaderboard_entries_async_handle_ = nullptr;
	rc_client_async_handle_t *media_change_async_handle_ = nullptr;
	rc_client_async_handle_t *library_sync_async_handle_ = nullptr;
	RaLoginSnapshot login_;
	RaGameSessionSnapshot game_session_;
	RaLeaderboardEntriesSnapshot leaderboard_entries_;
	RaMediaChangeSnapshot media_change_;
	RaMediaVerificationSnapshot media_verification_;
	uint32_t media_verification_expected_game_id_ = 0;
	RaLibrarySyncSnapshot library_sync_;
	RaUnlockSyncSnapshot unlock_sync_;
	std::vector<RaPendingUnlockRecord> pending_unlock_sync_records_;
	size_t pending_unlock_sync_offset_ = 0;
	uint64_t pending_unlock_sync_generation_ = 0;
	std::map<uint64_t, PendingAwardContext *> pending_award_requests_;
	std::map<uint64_t, PendingLeaderboardContext *>
		pending_leaderboard_requests_;
	std::map<PendingLeaderboardContext *, PendingLeaderboardContext *>
		pending_leaderboard_retries_;
	std::map<uint64_t, PendingSyncContext *> pending_unlock_sync_requests_;
	bool canceling_submission_requests_ = false;
	std::string integrity_failure_;
	std::map<std::string, bool> library_sync_local_hashes_;
	std::vector<uint32_t> library_sync_title_game_ids_;
	size_t library_sync_title_offset_ = 0;
	std::map<std::string, uint32_t> verified_media_game_ids_;
	bool media_change_preflight_pending_ = false;
	uint64_t media_change_preflight_request_id_ = 0;
	bool media_verification_pending_ = false;
	uint64_t media_verification_request_id_ = 0;
	std::vector<RaEvent> events_;
	std::string rich_presence_;
	bool shutdown_ = false;
};

} // namespace Xm8Ra

#endif

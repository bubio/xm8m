#include "ra_service.h"
#include "ra_state_store.h"

#include "rc_error.h"
#include "rc_api_runtime.h"
#include "rc_consoles.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <utility>

namespace Xm8Ra {

namespace {

bool IsMd5Hex(const std::string& hash)
{
	if (hash.size() != 32) {
		return false;
	}
	for (const char c : hash) {
		if (!std::isxdigit(static_cast<unsigned char>(c))) {
			return false;
		}
	}
	return true;
}

std::string SafeString(const char *value)
{
	return value != nullptr ? value : "";
}

bool DecodeFormValue(const std::string& source, std::string *value)
{
	value->clear();
	for (size_t i = 0; i < source.size(); ++i) {
		const char ch = source[i];
		if (ch == '+') value->push_back(' ');
		else if (ch == '%') {
			if (i + 2 >= source.size() || !std::isxdigit(source[i + 1]) ||
				!std::isxdigit(source[i + 2])) return false;
			const std::string hex = source.substr(i + 1, 2);
			value->push_back(static_cast<char>(std::strtoul(hex.c_str(), nullptr, 16)));
			i += 2;
		}
		else value->push_back(ch);
	}
	return true;
}

bool ParseForm(const char *post_data, std::map<std::string, std::string> *fields)
{
	fields->clear();
	if (post_data == nullptr) return false;
	const std::string form(post_data);
	size_t begin = 0;
	while (begin <= form.size()) {
		const size_t end = form.find('&', begin);
		const std::string item = form.substr(begin,
			end == std::string::npos ? std::string::npos : end - begin);
		const size_t equals = item.find('=');
		std::string key;
		std::string value;
		if (equals == std::string::npos ||
			!DecodeFormValue(item.substr(0, equals), &key) ||
			!DecodeFormValue(item.substr(equals + 1), &value) || key.empty() ||
			!fields->emplace(key, value).second) return false;
		if (end == std::string::npos) break;
		begin = end + 1;
	}
	return true;
}

bool ParseUnsigned(const std::string& value, uint32_t *number)
{
	if (value.empty()) return false;
	for (const char ch : value) {
		if (ch < '0' || ch > '9') return false;
	}
	char *end = nullptr;
	const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
	if (end == nullptr || *end != '\0' || parsed > UINT32_MAX) return false;
	*number = static_cast<uint32_t>(parsed);
	return true;
}

bool ParseSigned(const std::string& value, int32_t *number)
{
	if (value.empty()) return false;
	size_t offset = value[0] == '-' ? 1 : 0;
	if (offset == value.size()) return false;
	for (size_t index = offset; index < value.size(); ++index) {
		if (value[index] < '0' || value[index] > '9') return false;
	}
	char *end = nullptr;
	const long parsed = std::strtol(value.c_str(), &end, 10);
	if (end == nullptr || *end != '\0' || parsed < INT32_MIN ||
		parsed > INT32_MAX) return false;
	*number = static_cast<int32_t>(parsed);
	return true;
}

bool IsRetryableSubmissionResponse(const rc_api_server_response_t *response)
{
	if (response == nullptr) return false;
	switch (response->http_status_code) {
	case 429:
	case 502:
	case 503:
	case 504:
	case 521:
	case 522:
	case 523:
	case 524:
	case 525:
	case RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR:
		return true;
	default:
		return false;
	}
}

bool IsSuccessfulHttpResponse(const rc_api_server_response_t *response)
{
	return response != nullptr && response->http_status_code >= 200 &&
		response->http_status_code < 300;
}

bool IsAlreadyAwardedResponse(
	const rc_api_award_achievement_response_t& response)
{
	return response.response.error_message != nullptr &&
		std::strncmp(response.response.error_message, "User already has", 16) == 0;
}

uint64_t DefaultMonotonicMillis(void *)
{
	return static_cast<uint64_t>(std::chrono::duration_cast<
		std::chrono::milliseconds>(std::chrono::steady_clock::now()
		.time_since_epoch()).count());
}

uint64_t LeaderboardRetryDelayMillis(uint32_t retry_count)
{
	if (retry_count <= 1) return 0;
	const uint32_t seconds = retry_count > 8 ? 120U :
		(1U << (retry_count - 2));
	return static_cast<uint64_t>(seconds) * 1000U;
}

RaEventType MapEventType(uint32_t type)
{
	switch (type) {
	case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
		return RaEventType::AchievementTriggered;
	case RC_CLIENT_EVENT_LEADERBOARD_STARTED:
		return RaEventType::LeaderboardStarted;
	case RC_CLIENT_EVENT_LEADERBOARD_FAILED:
		return RaEventType::LeaderboardFailed;
	case RC_CLIENT_EVENT_LEADERBOARD_SUBMITTED:
		return RaEventType::LeaderboardSubmitted;
	case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW:
		return RaEventType::AchievementChallengeIndicatorShow;
	case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_HIDE:
		return RaEventType::AchievementChallengeIndicatorHide;
	case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW:
		return RaEventType::AchievementProgressIndicatorShow;
	case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_HIDE:
		return RaEventType::AchievementProgressIndicatorHide;
	case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_UPDATE:
		return RaEventType::AchievementProgressIndicatorUpdate;
	case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_SHOW:
		return RaEventType::LeaderboardTrackerShow;
	case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_HIDE:
		return RaEventType::LeaderboardTrackerHide;
	case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_UPDATE:
		return RaEventType::LeaderboardTrackerUpdate;
	case RC_CLIENT_EVENT_LEADERBOARD_SCOREBOARD:
		return RaEventType::LeaderboardScoreboard;
	case RC_CLIENT_EVENT_RESET:
		return RaEventType::ResetRequested;
	case RC_CLIENT_EVENT_GAME_COMPLETED:
		return RaEventType::GameCompleted;
	case RC_CLIENT_EVENT_SERVER_ERROR:
		return RaEventType::ServerError;
	case RC_CLIENT_EVENT_DISCONNECTED:
		return RaEventType::Disconnected;
	case RC_CLIENT_EVENT_RECONNECTED:
		return RaEventType::Reconnected;
	case RC_CLIENT_EVENT_SUBSET_COMPLETED:
		return RaEventType::SubsetCompleted;
	default:
		return RaEventType::None;
	}
}

void CopyAchievement(RaAchievementEvent *target,
	const rc_client_achievement_t *achievement)
{
	if (target == nullptr || achievement == nullptr) {
		return;
	}

	target->id = achievement->id;
	target->points = achievement->points;
	target->state = achievement->state;
	target->category = achievement->category;
	target->bucket = achievement->bucket;
	target->unlocked = achievement->unlocked;
	target->type = achievement->type;
	target->measured_percent = achievement->measured_percent;
	target->title = SafeString(achievement->title);
	target->description = SafeString(achievement->description);
	target->measured_progress = achievement->measured_progress;
	target->badge_url = SafeString(achievement->badge_url);
	target->badge_locked_url = SafeString(achievement->badge_locked_url);
}

void CopyAchievementListItem(RaAchievementListItem *target,
	const rc_client_achievement_t *achievement, const char *bucket_label)
{
	if (target == nullptr || achievement == nullptr) {
		return;
	}

	target->id = achievement->id;
	target->points = achievement->points;
	target->state = achievement->state;
	target->category = achievement->category;
	target->bucket = achievement->bucket;
	target->unlocked = achievement->unlocked;
	target->type = achievement->type;
	target->measured_percent = achievement->measured_percent;
	target->rarity = achievement->rarity;
	target->rarity_hardcore = achievement->rarity_hardcore;
	target->title = SafeString(achievement->title);
	target->description = SafeString(achievement->description);
	target->measured_progress = achievement->measured_progress;
	target->badge_url = SafeString(achievement->badge_url);
	target->badge_locked_url = SafeString(achievement->badge_locked_url);
	target->bucket_label = SafeString(bucket_label);
}

void CopyLeaderboard(RaLeaderboardEvent *target,
	const rc_client_leaderboard_t *leaderboard)
{
	if (target == nullptr || leaderboard == nullptr) {
		return;
	}

	target->id = leaderboard->id;
	target->state = leaderboard->state;
	target->format = leaderboard->format;
	target->lower_is_better = leaderboard->lower_is_better != 0;
	target->title = SafeString(leaderboard->title);
	target->description = SafeString(leaderboard->description);
	target->tracker_value = SafeString(leaderboard->tracker_value);
}

void CopyLeaderboardListItem(RaLeaderboardListItem *target,
	const rc_client_leaderboard_t *leaderboard, const char *bucket_label)
{
	if (target == nullptr || leaderboard == nullptr) {
		return;
	}

	target->id = leaderboard->id;
	target->state = leaderboard->state;
	target->format = leaderboard->format;
	target->lower_is_better = leaderboard->lower_is_better != 0;
	target->title = SafeString(leaderboard->title);
	target->description = SafeString(leaderboard->description);
	target->tracker_value = SafeString(leaderboard->tracker_value);
	target->bucket_label = SafeString(bucket_label);
}

void CopyLeaderboardTracker(RaLeaderboardEvent *target,
	const rc_client_leaderboard_tracker_t *tracker)
{
	if (target == nullptr || tracker == nullptr) {
		return;
	}

	target->id = tracker->id;
	target->display = tracker->display;
}

void CopyScoreboard(RaLeaderboardScoreboardEvent *target,
	const rc_client_leaderboard_scoreboard_t *scoreboard)
{
	if (target == nullptr || scoreboard == nullptr) {
		return;
	}

	target->leaderboard_id = scoreboard->leaderboard_id;
	target->new_rank = scoreboard->new_rank;
	target->num_entries = scoreboard->num_entries;
	target->submitted_score = scoreboard->submitted_score;
	target->best_score = scoreboard->best_score;
	if (scoreboard->top_entries == nullptr) {
		return;
	}
	for (uint32_t i = 0; i < scoreboard->num_top_entries; i++) {
		RaLeaderboardScoreboardEntry entry;
		entry.rank = scoreboard->top_entries[i].rank;
		entry.username = SafeString(scoreboard->top_entries[i].username);
		entry.score = scoreboard->top_entries[i].score;
		target->top_entries.push_back(entry);
	}
}

void CopyServerError(RaServerErrorEvent *target,
	const rc_client_server_error_t *server_error)
{
	if (target == nullptr || server_error == nullptr) {
		return;
	}

	target->result = server_error->result;
	target->related_id = server_error->related_id;
	target->api = SafeString(server_error->api);
	target->message = SafeString(server_error->error_message);
}

void CopySubset(RaSubsetEvent *target, const rc_client_subset_t *subset)
{
	if (target == nullptr || subset == nullptr) {
		return;
	}

	target->id = subset->id;
	target->num_achievements = subset->num_achievements;
	target->num_leaderboards = subset->num_leaderboards;
	target->title = SafeString(subset->title);
	target->badge_url = SafeString(subset->badge_url);
}

} // namespace

struct RaService::PendingAwardContext {
	RaService *service = nullptr;
	int64_t record_id = 0;
	uint32_t achievement_id = 0;
	uint64_t request_id = 0;
	uint64_t generation = 0;
	std::string account;
	rc_client_server_callback_t callback = nullptr;
	void *callback_data = nullptr;
};

struct RaService::PendingLeaderboardContext {
	RaService *service = nullptr;
	uint64_t request_id = 0;
	uint64_t generation = 0;
	std::string account;
	std::string url;
	std::string post_data;
	std::string content_type;
	bool has_post_data = false;
	uint32_t retry_count = 0;
	uint64_t retry_at = 0;
	rc_client_server_callback_t callback = nullptr;
	void *callback_data = nullptr;
};

struct RaService::PendingSyncContext {
	RaService *service = nullptr;
	int64_t record_id = 0;
	uint32_t achievement_id = 0;
	uint64_t request_id = 0;
	uint64_t generation = 0;
	std::string account;
};

RaService::RaService(RaServiceOptions options)
	: http_client_(std::move(options.http_client)),
	  credentials_(std::move(options.credentials_store)),
	  pending_unlock_store_(options.pending_unlock_store),
	  monotonic_millis_(options.monotonic_millis != nullptr ?
		options.monotonic_millis : DefaultMonotonicMillis),
	  monotonic_millis_userdata_(options.monotonic_millis_userdata)
{
	if (http_client_ == nullptr) {
		SetFailed(RC_INVALID_STATE, "HTTP client is required");
		return;
	}
	if (credentials_ == nullptr) {
		SetFailed(RC_INVALID_STATE, "credentials store is required");
		return;
	}
	if (pending_unlock_store_ == nullptr) {
		SetFailed(RC_INVALID_STATE, "pending unlock store is required");
		return;
	}

	http_bridge_.reset(new RaRcClientHttpBridge(http_client_.get()));
	host_read_memory_ = options.host_read_memory;
	host_read_memory_userdata_ = options.host_read_memory_userdata;
	client_ = rc_client_create(
		host_read_memory_ != nullptr ? ReadHostMemory :
			(options.read_memory != nullptr ? options.read_memory : ReadNoMemory),
		ServerCall);
	if (client_ == nullptr) {
		SetFailed(RC_OUT_OF_MEMORY, rc_error_str(RC_OUT_OF_MEMORY));
		return;
	}

	rc_client_set_userdata(client_, this);
	rc_client_set_event_handler(client_, ClientEventHandler);
	rc_client_set_allow_background_memory_reads(client_, 0);
	rc_client_set_hardcore_enabled(client_, 0);
}

RaService::~RaService()
{
	Shutdown();
}

bool RaService::IsReady() const
{
	return client_ != nullptr && http_bridge_ != nullptr &&
		credentials_ != nullptr && pending_unlock_store_ != nullptr;
}

bool RaService::BeginLoginWithPassword(const std::string& username,
	const std::string& password, std::string *error)
{
	if (!IsReady()) {
		if (error != nullptr) {
			*error = "RA service is not ready";
		}
		return false;
	}
	if (login_.state == RaLoginState::LoggedIn) {
		if (error != nullptr) {
			*error = "logout is required before changing account";
		}
		return false;
	}

	AbortLoginInProgress();
	login_ = RaLoginSnapshot();
	login_.state = RaLoginState::LoginPending;
	login_.username = username;
	login_kind_ = LoginKind::Password;

	rc_client_async_handle_t *handle = rc_client_begin_login_with_password(
		client_, username.c_str(), password.c_str(), LoginCallback, this);
	if (handle == nullptr && login_.state == RaLoginState::LoginPending) {
		login_async_handle_ = nullptr;
		SetFailed(RC_INVALID_STATE, "Login did not start");
		if (error != nullptr) {
			*error = login_.message;
		}
		return false;
	}
	login_async_handle_ = handle;

	return true;
}

bool RaService::BeginLoginWithSavedToken(std::string *error)
{
	if (!IsReady()) {
		if (error != nullptr) {
			*error = "RA service is not ready";
		}
		return false;
	}
	if (login_.state == RaLoginState::LoggedIn) {
		if (error != nullptr) {
			*error = "logout is required before changing account";
		}
		return false;
	}

	AbortLoginInProgress();
	RaCredentials credentials;
	if (!credentials_->Load(&credentials, error)) {
		login_ = RaLoginSnapshot();
		login_.state = RaLoginState::LoggedOut;
		return false;
	}

	login_ = RaLoginSnapshot();
	login_.state = RaLoginState::LoginPending;
	login_.username = credentials.username;
	login_kind_ = LoginKind::SavedToken;

	rc_client_async_handle_t *handle = rc_client_begin_login_with_token(
		client_, credentials.username.c_str(), credentials.token.c_str(),
		LoginCallback, this);
	credentials_->ClearSecret(&credentials);
	if (handle == nullptr && login_.state == RaLoginState::LoginPending) {
		login_async_handle_ = nullptr;
		SetFailed(RC_INVALID_STATE, "Saved token login did not start");
		DeleteCredentialsForRejectedToken();
		if (error != nullptr) {
			*error = login_.message;
		}
		return false;
	}
	login_async_handle_ = handle;

	return true;
}

bool RaService::BeginPendingUnlockSync(std::string *error)
{
	// A reconnect may arrive before the failed live submission has drained.
	// Retire that rcheevos callback first so only the durable outbox owns the
	// retry and the same record cannot have two active HTTP callbacks.
	CancelPendingAwardRequests();
	CancelPendingUnlockSyncRequests();
	++pending_unlock_sync_generation_;
	unlock_sync_ = RaUnlockSyncSnapshot();
	pending_unlock_sync_records_.clear();
	pending_unlock_sync_offset_ = 0;
	if (login_.state != RaLoginState::LoggedIn || pending_unlock_store_ == nullptr) {
		unlock_sync_.state = RaUnlockSyncState::Failed;
		unlock_sync_.message = "pending unlock store is unavailable";
		if (error != nullptr) *error = unlock_sync_.message;
		return false;
	}
	std::string recovery_reason;
	if (pending_unlock_store_->RecoveryRequired(&recovery_reason)) {
		unlock_sync_.state = RaUnlockSyncState::Failed;
		unlock_sync_.message = recovery_reason.empty() ?
			"pending unlock recovery confirmation is required" : recovery_reason;
		if (error != nullptr) *error = unlock_sync_.message;
		return false;
	}
	if (!pending_unlock_store_->ListPendingUnlocks(login_.username,
		&pending_unlock_sync_records_, error)) {
		unlock_sync_.state = RaUnlockSyncState::Failed;
		unlock_sync_.message = error != nullptr ? *error :
			"pending unlock query failed";
		return false;
	}
	unlock_sync_.remaining = pending_unlock_sync_records_.size();
	if (pending_unlock_sync_records_.empty()) {
		unlock_sync_.state = RaUnlockSyncState::Succeeded;
		return true;
	}
	for (const RaPendingUnlockRecord& record : pending_unlock_sync_records_) {
		if (record.status == RaPendingUnlockStatus::Held) {
			unlock_sync_.state = RaUnlockSyncState::Failed;
			unlock_sync_.message = record.last_error.empty() ?
				"a pending unlock requires user attention" : record.last_error;
			if (error != nullptr) *error = unlock_sync_.message;
			return false;
		}
	}
	unlock_sync_.state = RaUnlockSyncState::Pending;
	StartNextPendingUnlock();
	return unlock_sync_.state != RaUnlockSyncState::Failed;
}

void RaService::StartNextPendingUnlock()
{
	if (pending_unlock_sync_offset_ >= pending_unlock_sync_records_.size()) {
		unlock_sync_.state = RaUnlockSyncState::Succeeded;
		unlock_sync_.remaining = 0;
		return;
	}
	const rc_client_user_t *user = rc_client_get_user_info(client_);
	if (user == nullptr || user->token == nullptr) {
		unlock_sync_.state = RaUnlockSyncState::Failed;
		unlock_sync_.message = "RA token is unavailable for unlock sync";
		return;
	}
	const RaPendingUnlockRecord& record =
		pending_unlock_sync_records_[pending_unlock_sync_offset_];
	if (record.account != login_.username || user->username == nullptr ||
		record.account != user->username) {
		unlock_sync_.state = RaUnlockSyncState::Failed;
		unlock_sync_.message = "pending unlock account changed during sync";
		return;
	}
	const int64_t now = static_cast<int64_t>(std::time(nullptr));
	const int64_t elapsed = std::max<int64_t>(0, now - record.unlocked_at);
	rc_api_award_achievement_request_t params = {};
	params.username = user->username;
	params.api_token = user->token;
	params.achievement_id = record.achievement_id;
	params.hardcore = record.hardcore ? 1 : 0;
	params.game_hash = record.game_hash.c_str();
	params.seconds_since_unlock = static_cast<uint32_t>(
		std::min<int64_t>(elapsed, UINT32_MAX));
	rc_api_request_t request = {};
	const int result = rc_api_init_award_achievement_request(&request, &params);
	if (result != RC_OK) {
		unlock_sync_.state = RaUnlockSyncState::Failed;
		unlock_sync_.message = rc_error_str(result);
		return;
	}
	PendingSyncContext *context = new PendingSyncContext();
	context->service = this;
	context->record_id = record.id;
	context->achievement_id = record.achievement_id;
	context->generation = pending_unlock_sync_generation_;
	context->account = record.account;
	context->request_id = http_bridge_->BeginServerCall(
		&request, PendingSyncCallback, context);
	rc_api_destroy_request(&request);
	if (context->request_id == 0) {
		delete context;
		unlock_sync_.state = RaUnlockSyncState::Failed;
		unlock_sync_.message = "pending unlock HTTP request did not start";
		return;
	}
	pending_unlock_sync_requests_[context->request_id] = context;
}

void RC_CCONV RaService::PendingSyncCallback(
	const rc_api_server_response_t *server_response, void *userdata)
{
	std::unique_ptr<PendingSyncContext> context(
		static_cast<PendingSyncContext *>(userdata));
	if (context == nullptr || context->service == nullptr) return;
	RaService *service = context->service;
	auto pending = service->pending_unlock_sync_requests_.find(
		context->request_id);
	if (pending != service->pending_unlock_sync_requests_.end() &&
		pending->second == context.get()) {
		service->pending_unlock_sync_requests_.erase(pending);
	}
	if (context->generation != service->pending_unlock_sync_generation_ ||
		context->account != service->login_.username) {
		return;
	}
	rc_api_award_achievement_response_t response = {};
	const int result = rc_api_process_award_achievement_server_response(
		&response, server_response);
	std::string store_error;
	const bool already_awarded = IsAlreadyAwardedResponse(response);
	const bool matching_success = IsSuccessfulHttpResponse(server_response) &&
		result == RC_OK && response.response.succeeded &&
		(already_awarded ||
		response.awarded_achievement_id == context->achievement_id);
	if (matching_success &&
		service->pending_unlock_store_->RemovePendingUnlock(
			context->record_id, &store_error)) {
		++service->pending_unlock_sync_offset_;
		service->unlock_sync_.remaining = service->pending_unlock_sync_records_.size() -
			service->pending_unlock_sync_offset_;
		rc_api_destroy_award_achievement_response(&response);
		service->StartNextPendingUnlock();
		return;
	}
	const bool mismatched_success = result == RC_OK &&
		response.response.succeeded && !matching_success;
	std::string message = !store_error.empty() ? store_error :
		(mismatched_success ? "award response achievement ID mismatch" :
		(response.response.error_message != nullptr ? response.response.error_message :
			rc_error_str(result)));
	const RaPendingUnlockStatus status = mismatched_success ?
		RaPendingUnlockStatus::Held :
		(response.response.error_message != nullptr ?
			(IsRetryableSubmissionResponse(server_response) ?
				RaPendingUnlockStatus::Pending : RaPendingUnlockStatus::Held) :
			RaPendingUnlockStatus::Pending);
	const bool attempt_recorded =
		service->pending_unlock_store_->MarkPendingUnlockAttempt(
			context->record_id, status, message, &store_error);
	if (!attempt_recorded) {
		if (store_error.empty()) {
			store_error = "pending unlock attempt could not be recorded";
		}
		service->integrity_failure_ = store_error;
		message = store_error;
	}
	if (attempt_recorded && status == RaPendingUnlockStatus::Pending) {
		service->unlock_sync_ = RaUnlockSyncSnapshot();
		service->unlock_sync_.message = message;
	}
	else {
		service->unlock_sync_.state = RaUnlockSyncState::Failed;
		service->unlock_sync_.message = message;
	}
	rc_api_destroy_award_achievement_response(&response);
}

void RaService::InvalidatePendingUnlockSync()
{
	CancelPendingUnlockSyncRequests();
	++pending_unlock_sync_generation_;
	unlock_sync_ = RaUnlockSyncSnapshot();
	pending_unlock_sync_records_.clear();
	pending_unlock_sync_offset_ = 0;
}

bool RaService::BeginLoadGameByHash(const std::string& hash, std::string *error)
{
	if (!IsReady()) {
		if (error != nullptr) {
			*error = "RA service is not ready";
		}
		return false;
	}
	if (login_.state != RaLoginState::LoggedIn) {
		if (error != nullptr) {
			*error = "RA login is required before loading a game";
		}
		return false;
	}
	if (unlock_sync_.state != RaUnlockSyncState::Succeeded ||
		unlock_sync_.remaining != 0 || !pending_unlock_sync_requests_.empty() ||
		!pending_award_requests_.empty()) {
		if (error != nullptr) {
			*error = "pending unlock sync must succeed before loading a game";
		}
		return false;
	}
	if (!IsMd5Hex(hash)) {
		if (error != nullptr) {
			*error = "RA game hash must be a 32-character MD5 hex string";
		}
		return false;
	}
	if (game_session_.state == RaGameSessionState::LoadPending) {
		if (error != nullptr) {
			*error = "RA game load is already pending";
		}
		return false;
	}

	CancelPendingAwardRequests();
	CancelPendingLeaderboardRequests();
	http_bridge_->AdvanceGeneration();
	game_session_ = RaGameSessionSnapshot();
	game_session_.state = RaGameSessionState::LoadPending;
	game_session_.hash = hash;
	game_session_.load_state = rc_client_get_load_game_state(client_);

	rc_client_async_handle_t *handle = rc_client_begin_load_game(
		client_, hash.c_str(), LoadGameCallback, this);
	game_session_.load_state = rc_client_get_load_game_state(client_);
	if (handle == nullptr &&
		game_session_.state == RaGameSessionState::LoadPending) {
		DisableGameSession(RC_INVALID_STATE, "RA game load did not start");
		if (error != nullptr) {
			*error = game_session_.message;
		}
		return false;
	}

	return true;
}

bool RaService::BeginChangeMediaByHash(const std::string& hash,
	std::string *error)
{
	if (!IsReady()) {
		if (error != nullptr) {
			*error = "RA service is not ready";
		}
		return false;
	}
	if (login_.state != RaLoginState::LoggedIn ||
		game_session_.state != RaGameSessionState::Loaded) {
		if (error != nullptr) {
			*error = "RA game is required before changing media";
		}
		return false;
	}
	if (!IsMd5Hex(hash)) {
		if (error != nullptr) {
			*error = "RA media hash must be a 32-character MD5 hex string";
		}
		return false;
	}
	if (media_change_.state == RaMediaChangeState::Pending) {
		if (error != nullptr) {
			*error = "RA media change is already pending";
		}
		return false;
	}

	media_change_ = RaMediaChangeSnapshot();
	media_change_.state = RaMediaChangeState::Pending;
	media_change_.hash = hash;
	const auto verified = verified_media_game_ids_.find(hash);
	if (verified != verified_media_game_ids_.end()) {
		if (verified->second != game_session_.game_id) {
			media_change_.state = RaMediaChangeState::Failed;
			media_change_.result = RC_INVALID_STATE;
			media_change_.message = "RA media belongs to another game";
			if (error != nullptr) {
				*error = media_change_.message;
			}
			return false;
		}
		return StartClientMediaChange(error);
	}

	rc_api_resolve_hash_request_t params = {};
	params.game_hash = hash.c_str();
	rc_api_request_t request = {};
	const int result = rc_api_init_resolve_hash_request(&request, &params);
	if (result != RC_OK) {
		media_change_.state = RaMediaChangeState::Failed;
		media_change_.result = result;
		media_change_.message = rc_error_str(result);
		if (error != nullptr) {
			*error = media_change_.message;
		}
		return false;
	}
	media_change_preflight_pending_ = true;
	http_bridge_->BeginServerCall(&request, ResolveMediaHashCallback, this);
	rc_api_destroy_request(&request);
	return media_change_.state == RaMediaChangeState::Pending;
}

bool RaService::StartClientMediaChange(std::string *error)
{
	rc_client_async_handle_t *handle = rc_client_begin_change_media(
		client_, media_change_.hash.c_str(), MediaChangeCallback, this);
	media_change_async_handle_ = handle;
	if (handle == nullptr &&
		media_change_.state == RaMediaChangeState::Pending) {
		media_change_.state = RaMediaChangeState::Failed;
		media_change_.result = RC_INVALID_STATE;
		media_change_.message = "RA media change did not start";
		if (error != nullptr) {
			*error = media_change_.message;
		}
		return false;
	}
	return media_change_.state != RaMediaChangeState::Failed;
}

void RaService::ClearMediaChangeResult()
{
	if (media_change_.state != RaMediaChangeState::Pending) {
		media_change_ = RaMediaChangeSnapshot();
	}
}

bool RaService::BeginVerifyMediaHashForCurrentGame(const std::string& hash,
	std::string *error)
{
	if (game_session_.state != RaGameSessionState::Loaded) {
		if (error != nullptr) *error = "RA game is required before verifying media";
		return false;
	}
	return BeginVerifyMediaHashForGame(hash, game_session_.game_id, error);
}

bool RaService::BeginVerifyMediaHashForGame(const std::string& hash,
	uint32_t expected_game_id, std::string *error)
{
	if (!IsReady() || login_.state != RaLoginState::LoggedIn ||
		expected_game_id == 0) {
		if (error != nullptr) *error =
			"RA game ID is required before verifying media";
		return false;
	}
	if (!IsMd5Hex(hash)) {
		if (error != nullptr) {
			*error = "RA media hash must be a 32-character MD5 hex string";
		}
		return false;
	}
	if (media_verification_.state == RaMediaChangeState::Pending) {
		if (error != nullptr) *error = "RA media verification is already pending";
		return false;
	}

	media_verification_ = RaMediaVerificationSnapshot();
	media_verification_expected_game_id_ = 0;
	media_verification_.state = RaMediaChangeState::Pending;
	media_verification_.hash = hash;
	media_verification_expected_game_id_ = expected_game_id;
	const auto verified = verified_media_game_ids_.find(hash);
	if (verified != verified_media_game_ids_.end()) {
		if (verified->second == expected_game_id) {
			media_verification_.state = RaMediaChangeState::Succeeded;
			if (error != nullptr) error->clear();
			return true;
		}
		media_verification_.state = RaMediaChangeState::Failed;
		media_verification_.failure = RaMediaVerificationFailure::DifferentGame;
		media_verification_.result = RC_INVALID_STATE;
		media_verification_.message = "RA media belongs to another game";
		if (error != nullptr) *error = media_verification_.message;
		return false;
	}

	rc_api_resolve_hash_request_t params = {};
	params.game_hash = hash.c_str();
	rc_api_request_t request = {};
	const int result = rc_api_init_resolve_hash_request(&request, &params);
	if (result != RC_OK) {
		media_verification_.state = RaMediaChangeState::Failed;
		media_verification_.failure = RaMediaVerificationFailure::Unavailable;
		media_verification_.result = result;
		media_verification_.message = rc_error_str(result);
		if (error != nullptr) *error = media_verification_.message;
		return false;
	}
	media_verification_pending_ = true;
	const uint64_t request_id = http_bridge_->BeginServerCall(
		&request, VerifyMediaHashCallback, this);
	media_verification_request_id_ = media_verification_pending_ ? request_id : 0;
	rc_api_destroy_request(&request);
	if (request_id == 0 &&
		media_verification_.state == RaMediaChangeState::Pending) {
		media_verification_pending_ = false;
		media_verification_.state = RaMediaChangeState::Failed;
		media_verification_.failure = RaMediaVerificationFailure::Unavailable;
		media_verification_.result = RC_INVALID_STATE;
		media_verification_.message = "RA media verification did not start";
		if (error != nullptr) *error = media_verification_.message;
		return false;
	}
	if (error != nullptr) error->clear();
	return media_verification_.state != RaMediaChangeState::Failed;
}

void RaService::CancelMediaVerification()
{
	if (http_bridge_ != nullptr && media_verification_request_id_ != 0)
		http_bridge_->Abandon(media_verification_request_id_);
	media_verification_request_id_ = 0;
	media_verification_pending_ = false;
	media_verification_ = RaMediaVerificationSnapshot();
	media_verification_expected_game_id_ = 0;
}

void RaService::ClearMediaVerificationResult()
{
	if (media_verification_.state != RaMediaChangeState::Pending) {
		media_verification_ = RaMediaVerificationSnapshot();
		media_verification_expected_game_id_ = 0;
	}
}

bool RaService::IsMediaHashVerifiedForCurrentGame(
	const std::string& hash) const
{
	return game_session_.state == RaGameSessionState::Loaded &&
		IsMediaHashVerifiedForGame(hash, game_session_.game_id);
}

bool RaService::IsMediaHashVerifiedForGame(const std::string& hash,
	uint32_t expected_game_id) const
{
	const auto verified = verified_media_game_ids_.find(hash);
	return expected_game_id > 0 &&
		verified != verified_media_game_ids_.end() &&
		verified->second == expected_game_id;
}

bool RaService::BeginLibrarySync(const std::vector<std::string>& local_hashes,
	std::string *error)
{
	if (!IsReady() || login_.state != RaLoginState::LoggedIn ||
		game_session_.state != RaGameSessionState::NoGame) {
		if (error != nullptr) {
			*error = "RA library sync requires an idle logged-in service";
		}
		return false;
	}
	if (library_sync_.state == RaLibrarySyncState::PendingHashes ||
		library_sync_.state == RaLibrarySyncState::PendingTitles ||
		library_sync_.state == RaLibrarySyncState::PendingProgress) {
		if (error != nullptr) {
			*error = "RA library sync is already pending";
		}
		return false;
	}

	library_sync_ = RaLibrarySyncSnapshot();
	library_sync_.state = RaLibrarySyncState::PendingHashes;
	library_sync_.username = login_.username;
	library_sync_local_hashes_.clear();
	for (const std::string& hash : local_hashes) {
		if (!IsMd5Hex(hash)) {
			FailLibrarySync(RC_INVALID_STATE, "invalid local RA hash");
			if (error != nullptr) {
				*error = library_sync_.message;
			}
			return false;
		}
		library_sync_local_hashes_[hash] = true;
	}
	library_sync_title_game_ids_.clear();
	library_sync_title_offset_ = 0;
	rc_client_async_handle_t *handle = rc_client_begin_fetch_hash_library(
		client_, RC_CONSOLE_PC8800, LibraryHashesCallback, this);
	library_sync_async_handle_ =
		library_sync_.state == RaLibrarySyncState::PendingHashes ? handle : nullptr;
	if (handle == nullptr &&
		library_sync_.state == RaLibrarySyncState::PendingHashes) {
		FailLibrarySync(RC_INVALID_STATE, "RA hash library request did not start");
	}
	if (library_sync_.state == RaLibrarySyncState::Failed) {
		if (error != nullptr) {
			*error = library_sync_.message;
		}
		return false;
	}
	return true;
}

void RaService::ClearLibrarySyncResult()
{
	if (library_sync_.state != RaLibrarySyncState::PendingHashes &&
		library_sync_.state != RaLibrarySyncState::PendingTitles &&
		library_sync_.state != RaLibrarySyncState::PendingProgress) {
		library_sync_ = RaLibrarySyncSnapshot();
		library_sync_local_hashes_.clear();
		library_sync_title_game_ids_.clear();
		library_sync_title_offset_ = 0;
	}
}

bool RaService::BeginFetchLeaderboardEntries(uint32_t leaderboard_id,
	uint32_t first_entry, uint32_t count, std::string *error)
{
	if (!IsReady()) {
		if (error != nullptr) {
			*error = "RA service is not ready";
		}
		return false;
	}
	if (login_.state != RaLoginState::LoggedIn) {
		if (error != nullptr) {
			*error = "RA login is required before loading leaderboard entries";
		}
		return false;
	}
	if (game_session_.state != RaGameSessionState::Loaded) {
		if (error != nullptr) {
			*error = "RA game is required before loading leaderboard entries";
		}
		return false;
	}
	if (leaderboard_id == 0 || count == 0) {
		if (error != nullptr) {
			*error = "invalid RA leaderboard entry request";
		}
		return false;
	}
	if (leaderboard_entries_.state == RaLeaderboardEntriesState::FetchPending &&
		leaderboard_entries_.leaderboard_id == leaderboard_id) {
		return true;
	}

	AbortLeaderboardEntriesInProgress();
	leaderboard_entries_ = RaLeaderboardEntriesSnapshot();
	leaderboard_entries_.state = RaLeaderboardEntriesState::FetchPending;
	leaderboard_entries_.leaderboard_id = leaderboard_id;

	rc_client_async_handle_t *handle =
		rc_client_begin_fetch_leaderboard_entries(client_, leaderboard_id,
			first_entry, count, LeaderboardEntriesCallback, this);
	leaderboard_entries_async_handle_ = handle;
	if (handle == nullptr &&
		leaderboard_entries_.state == RaLeaderboardEntriesState::FetchPending) {
		leaderboard_entries_async_handle_ = nullptr;
		leaderboard_entries_.state = RaLeaderboardEntriesState::Failed;
		leaderboard_entries_.result = RC_INVALID_STATE;
		leaderboard_entries_.message = "RA leaderboard entry load did not start";
		if (error != nullptr) {
			*error = leaderboard_entries_.message;
		}
		return false;
	}

	return true;
}

void RaService::DrainHttp()
{
	if (http_bridge_ != nullptr) {
		http_bridge_->DrainCompleted();
		ProcessPendingLeaderboardRetries();
		if (client_ != nullptr) {
			game_session_.load_state = rc_client_get_load_game_state(client_);
		}
	}
}

bool RaService::DoFrame()
{
	if (!IsReady() || game_session_.state != RaGameSessionState::Loaded ||
		!integrity_failure_.empty()) {
		return false;
	}

	rc_client_do_frame(client_);
	UpdateRichPresenceEvent();
	return true;
}

bool RaService::Idle()
{
	if (!IsReady()) {
		return false;
	}

	rc_client_idle(client_);
	UpdateRichPresenceEvent();
	return true;
}

bool RaService::IsProcessingRequired() const
{
	return (client_ != nullptr && rc_client_is_processing_required(client_) != 0) ||
		!pending_leaderboard_retries_.empty();
}

bool RaService::SerializeProgress(std::vector<uint8_t> *progress,
	std::string *error) const
{
	if (progress != nullptr) progress->clear();
	if (progress == nullptr || client_ == nullptr ||
		game_session_.state != RaGameSessionState::Loaded) {
		if (error != nullptr) *error = "RA game is not loaded";
		return false;
	}
	const size_t size = rc_client_progress_size(client_);
	if (size == 0 || size > kRaStateMaxProgressSize) {
		if (error != nullptr) *error = "invalid RA progress size";
		return false;
	}
	progress->resize(size);
	const int result = rc_client_serialize_progress_sized(client_,
		progress->data(), progress->size());
	if (result != RC_OK) {
		progress->clear();
		if (error != nullptr) *error = rc_error_str(result);
		return false;
	}
	if (error != nullptr) error->clear();
	return true;
}

bool RaService::DeserializeProgress(const std::vector<uint8_t>& progress,
	std::string *error)
{
	if (client_ == nullptr || game_session_.state != RaGameSessionState::Loaded ||
		progress.empty() || progress.size() > kRaStateMaxProgressSize) {
		if (error != nullptr) *error = "RA progress is unavailable";
		return false;
	}
	const int result = rc_client_deserialize_progress_sized(client_,
		progress.data(), progress.size());
	if (result != RC_OK) {
		if (error != nullptr) *error = rc_error_str(result);
		return false;
	}
	if (error != nullptr) error->clear();
	return true;
}

void RaService::ResetProgress()
{
	if (client_ != nullptr && game_session_.state == RaGameSessionState::Loaded) {
		rc_client_reset(client_);
	}
}

void RaService::SetHardcoreEnabled(bool enabled)
{
	if (client_ != nullptr) {
		rc_client_set_hardcore_enabled(client_, enabled ? 1 : 0);
	}
}

bool RaService::IsHardcoreEnabled() const
{
	return client_ != nullptr && rc_client_get_hardcore_enabled(client_) != 0;
}

bool RaService::CanPause(uint32_t *frames_remaining) const
{
	return client_ == nullptr ||
		rc_client_can_pause(client_, frames_remaining) != 0;
}

std::vector<RaEvent> RaService::TakeEvents()
{
	std::vector<RaEvent> events;
	events.swap(events_);
	return events;
}

void RaService::UnloadGame()
{
	CancelPendingAwardRequests();
	CancelPendingLeaderboardRequests();
	AbortLibrarySyncInProgress();
	AbortMediaChangeInProgress();
	AbortLeaderboardEntriesInProgress();
	if (client_ != nullptr) {
		rc_client_unload_game(client_);
	}
	game_session_ = RaGameSessionSnapshot();
	leaderboard_entries_ = RaLeaderboardEntriesSnapshot();
	media_change_ = RaMediaChangeSnapshot();
	media_verification_ = RaMediaVerificationSnapshot();
	media_verification_expected_game_id_ = 0;
	verified_media_game_ids_.clear();
	rich_presence_.clear();
}

bool RaService::Logout(bool delete_pending, std::string *error)
{
	InvalidatePendingUnlockSync();
	CancelPendingAwardRequests();
	CancelPendingLeaderboardRequests();
	AbortLoginInProgress();
	UnloadGame();

	std::string cleanup_error;
	if (credentials_ == nullptr || !credentials_->Delete(&cleanup_error)) {
		login_.state = RaLoginState::Failed;
		login_kind_ = LoginKind::None;
		login_.message = cleanup_error.empty() ?
			"RA credential deletion failed" : cleanup_error;
		if (error != nullptr) *error = login_.message;
		return false;
	}
	if (delete_pending && !DeletePendingUnlocks(&cleanup_error)) {
		login_.state = RaLoginState::Failed;
		login_kind_ = LoginKind::None;
		login_.message = cleanup_error.empty() ?
			"pending unlock deletion failed" : cleanup_error;
		if (error != nullptr) *error = login_.message;
		return false;
	}

	login_ = RaLoginSnapshot();
	login_.state = RaLoginState::LoggedOut;
	login_kind_ = LoginKind::None;
	if (error != nullptr) error->clear();
	return true;
}

void RaService::Shutdown()
{
	if (shutdown_) {
		return;
	}
	shutdown_ = true;

	InvalidatePendingUnlockSync();
	CancelPendingAwardRequests();
	CancelPendingLeaderboardRequests();
	AbortLoginInProgress();
	AbortLibrarySyncInProgress();
	AbortMediaChangeInProgress();
	AbortLeaderboardEntriesInProgress();
	if (http_bridge_ != nullptr) {
		http_bridge_->CancelAll();
		http_bridge_->DrainCompleted();
	}

	if (client_ != nullptr) {
		rc_client_set_userdata(client_, nullptr);
		rc_client_destroy(client_);
		client_ = nullptr;
	}

	if (http_bridge_ != nullptr) {
		http_bridge_->AbortAllWithoutCallbacks();
		http_bridge_.reset();
	}
	http_client_.reset();
	login_kind_ = LoginKind::None;
}

RaLoginSnapshot RaService::LoginSnapshot() const
{
	return login_;
}

RaGameSessionSnapshot RaService::GameSessionSnapshot() const
{
	return game_session_;
}

RaMediaChangeSnapshot RaService::MediaChangeSnapshot() const
{
	return media_change_;
}

RaMediaVerificationSnapshot RaService::MediaVerificationSnapshot() const
{
	return media_verification_;
}

RaLibrarySyncSnapshot RaService::LibrarySyncSnapshot() const
{
	return library_sync_;
}

RaUnlockSyncSnapshot RaService::UnlockSyncSnapshot() const
{
	return unlock_sync_;
}

bool RaService::TakeIntegrityFailure(std::string *message)
{
	if (integrity_failure_.empty()) return false;
	if (message != nullptr) *message = integrity_failure_;
	integrity_failure_.clear();
	return true;
}

bool RaService::HasPendingUnlocks(size_t *count, std::string *error) const
{
	if (count != nullptr) *count = 0;
	if (pending_unlock_store_ == nullptr || login_.username.empty()) {
		if (error != nullptr) *error = "pending unlock store is unavailable";
		return false;
	}
	if (!pending_unlock_store_->CountPendingUnlocks(login_.username,
		count, error)) return false;
	if (pending_unlock_store_->RecoveryRequired(nullptr)) ++*count;
	return true;
}

bool RaService::DeletePendingUnlocks(std::string *error)
{
	if (pending_unlock_store_ == nullptr || login_.username.empty()) {
		if (error != nullptr) *error = "pending unlock store is unavailable";
		return false;
	}
	if (!pending_unlock_store_->RemovePendingUnlocksForAccount(
		login_.username, error)) return false;
	return pending_unlock_store_->ConfirmDiscardRecovery(error);
}

std::string RaService::RichPresence() const
{
	return rich_presence_;
}

RaAchievementListSnapshot RaService::AchievementListSnapshot() const
{
	RaAchievementListSnapshot snapshot;
	snapshot.game_loaded = game_session_.state == RaGameSessionState::Loaded;
	snapshot.game_title = game_session_.title;
	if (!snapshot.game_loaded || client_ == nullptr ||
		rc_client_has_achievements(client_) == 0) {
		return snapshot;
	}

	rc_client_achievement_list_t *list = rc_client_create_achievement_list(
		client_, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE_AND_UNOFFICIAL,
		RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS);
	if (list == nullptr) {
		return snapshot;
	}

	for (uint32_t bucket_index = 0; bucket_index < list->num_buckets;
		++bucket_index) {
		const rc_client_achievement_bucket_t& bucket =
			list->buckets[bucket_index];
		for (uint32_t achievement_index = 0;
			achievement_index < bucket.num_achievements;
			++achievement_index) {
			RaAchievementListItem item;
			CopyAchievementListItem(&item,
				bucket.achievements[achievement_index], bucket.label);
			snapshot.achievements.push_back(item);
		}
	}

	rc_client_destroy_achievement_list(list);
	snapshot.has_achievements = !snapshot.achievements.empty();
	return snapshot;
}

RaLeaderboardListSnapshot RaService::LeaderboardListSnapshot() const
{
	RaLeaderboardListSnapshot snapshot;
	snapshot.game_loaded = game_session_.state == RaGameSessionState::Loaded;
	snapshot.game_title = game_session_.title;
	if (!snapshot.game_loaded || client_ == nullptr ||
		rc_client_has_leaderboards(client_) == 0) {
		return snapshot;
	}

	rc_client_leaderboard_list_t *list = rc_client_create_leaderboard_list(
		client_, RC_CLIENT_LEADERBOARD_LIST_GROUPING_TRACKING);
	if (list == nullptr) {
		return snapshot;
	}

	for (uint32_t bucket_index = 0; bucket_index < list->num_buckets;
		++bucket_index) {
		const rc_client_leaderboard_bucket_t& bucket =
			list->buckets[bucket_index];
		for (uint32_t leaderboard_index = 0;
			leaderboard_index < bucket.num_leaderboards;
			++leaderboard_index) {
			RaLeaderboardListItem item;
			CopyLeaderboardListItem(&item,
				bucket.leaderboards[leaderboard_index], bucket.label);
			snapshot.leaderboards.push_back(item);
		}
	}

	rc_client_destroy_leaderboard_list(list);
	snapshot.has_leaderboards = !snapshot.leaderboards.empty();
	return snapshot;
}

RaLeaderboardEntriesSnapshot RaService::LeaderboardEntriesSnapshot() const
{
	return leaderboard_entries_;
}

size_t RaService::PendingHttpCount() const
{
	return (http_bridge_ != nullptr ? http_bridge_->PendingCount() : 0) +
		pending_leaderboard_retries_.size();
}

uint64_t RaService::LastIssuedRequestId() const
{
	return http_bridge_ != nullptr ? http_bridge_->LastIssuedRequestId() : 0;
}

const RaHttpClient *RaService::HttpClientForTesting() const
{
	return http_client_.get();
}

RaHttpClient *RaService::HttpClientForTesting()
{
	return http_client_.get();
}

void RaService::QueueEventForTesting(const rc_client_event_t *event)
{
	HandleClientEvent(event);
}

void RaService::ServerCallForTesting(const rc_api_request_t *request,
	rc_client_server_callback_t callback, void *callback_data)
{
	ServerCall(request, callback, callback_data, client_);
}

uint32_t RC_CCONV RaService::ReadNoMemory(uint32_t, uint8_t *buffer,
	uint32_t num_bytes, rc_client_t *)
{
	if (buffer != nullptr && num_bytes != 0) {
		std::memset(buffer, 0, num_bytes);
	}
	return 0;
}

uint32_t RC_CCONV RaService::ReadHostMemory(uint32_t address, uint8_t *buffer,
	uint32_t num_bytes, rc_client_t *client)
{
	RaService *service = client == nullptr ? nullptr :
		static_cast<RaService *>(rc_client_get_userdata(client));
	if (service == nullptr || service->host_read_memory_ == nullptr) {
		if (buffer != nullptr && num_bytes != 0) {
			std::memset(buffer, 0, num_bytes);
		}
		return 0;
	}
	return service->host_read_memory_(address, buffer, num_bytes,
		service->host_read_memory_userdata_);
}

void RC_CCONV RaService::LoginCallback(int result, const char *error_message,
	rc_client_t *, void *userdata)
{
	RaService *service = static_cast<RaService *>(userdata);
	if (service != nullptr) {
		service->HandleLoginCallback(result, error_message);
	}
}

void RC_CCONV RaService::LoadGameCallback(int result,
	const char *error_message, rc_client_t *, void *userdata)
{
	RaService *service = static_cast<RaService *>(userdata);
	if (service != nullptr) {
		service->HandleLoadGameCallback(result, error_message);
	}
}

void RC_CCONV RaService::MediaChangeCallback(int result,
	const char *error_message, rc_client_t *, void *userdata)
{
	RaService *service = static_cast<RaService *>(userdata);
	if (service != nullptr) {
		service->HandleMediaChangeCallback(result, error_message);
	}
}

void RC_CCONV RaService::ResolveMediaHashCallback(
	const rc_api_server_response_t *server_response, void *userdata)
{
	RaService *service = static_cast<RaService *>(userdata);
	if (service != nullptr) {
		service->HandleResolveMediaHashCallback(server_response);
	}
}

void RC_CCONV RaService::VerifyMediaHashCallback(
	const rc_api_server_response_t *server_response, void *userdata)
{
	RaService *service = static_cast<RaService *>(userdata);
	if (service != nullptr) {
		service->HandleVerifyMediaHashCallback(server_response);
	}
}

void RC_CCONV RaService::LibraryHashesCallback(int result,
	const char *error_message, rc_client_hash_library_t *list,
	rc_client_t *, void *userdata)
{
	RaService *service = static_cast<RaService *>(userdata);
	if (service != nullptr) {
		service->HandleLibraryHashesCallback(result, error_message, list);
	}
	else if (list != nullptr) {
		rc_client_destroy_hash_library(list);
	}
}

void RC_CCONV RaService::LibraryTitlesCallback(int result,
	const char *error_message, rc_client_game_title_list_t *list,
	rc_client_t *, void *userdata)
{
	RaService *service = static_cast<RaService *>(userdata);
	if (service != nullptr) {
		service->HandleLibraryTitlesCallback(result, error_message, list);
	}
	else if (list != nullptr) {
		rc_client_destroy_game_title_list(list);
	}
}

void RC_CCONV RaService::LibraryProgressCallback(int result,
	const char *error_message, rc_client_all_user_progress_t *list,
	rc_client_t *, void *userdata)
{
	RaService *service = static_cast<RaService *>(userdata);
	if (service != nullptr) {
		service->HandleLibraryProgressCallback(result, error_message, list);
	}
	else if (list != nullptr) {
		rc_client_destroy_all_user_progress(list);
	}
}

void RC_CCONV RaService::LeaderboardEntriesCallback(int result,
	const char *error_message, rc_client_leaderboard_entry_list_t *list,
	rc_client_t *, void *userdata)
{
	RaService *service = static_cast<RaService *>(userdata);
	if (service != nullptr) {
		service->HandleLeaderboardEntriesCallback(result, error_message, list);
	}
}

void RC_CCONV RaService::ClientEventHandler(const rc_client_event_t *event,
	rc_client_t *client)
{
	RaService *service = client == nullptr ? nullptr :
		static_cast<RaService *>(rc_client_get_userdata(client));
	if (service != nullptr) {
		service->HandleClientEvent(event);
	}
}

void RC_CCONV RaService::ServerCall(const rc_api_request_t *request,
	rc_client_server_callback_t callback, void *callback_data,
	rc_client_t *client)
{
	RaService *service = client == nullptr ? nullptr :
		static_cast<RaService *>(rc_client_get_userdata(client));
	if (service == nullptr || service->http_bridge_ == nullptr) {
		if (callback != nullptr) {
			rc_api_server_response_t server_response = {};
			server_response.http_status_code = RC_API_SERVER_RESPONSE_CLIENT_ERROR;
			callback(&server_response, callback_data);
		}
		return;
	}
	if (service->InterceptAwardRequest(request, callback, callback_data)) {
		return;
	}
	if (service->InterceptLeaderboardSubmission(
		request, callback, callback_data)) {
		return;
	}

	service->http_bridge_->BeginServerCall(request, callback, callback_data);
}

bool RaService::InterceptAwardRequest(const rc_api_request_t *request,
	rc_client_server_callback_t callback, void *callback_data)
{
	std::map<std::string, std::string> fields;
	if (request == nullptr || request->post_data == nullptr) return false;
	if (!ParseForm(request->post_data, &fields)) {
		if (std::strstr(request->post_data, "awardachievement") == nullptr) {
			return false;
		}
		integrity_failure_ = "malformed award request was blocked";
		CompleteSubmissionCallback(callback, callback_data,
			"Malformed achievement submission was blocked");
		return true;
	}
	if (fields["r"] != "awardachievement") return false;

	uint32_t achievement_id = 0;
	uint32_t hardcore = 0;
	uint32_t seconds_since_unlock = 0;
	const rc_client_user_t *user = rc_client_get_user_info(client_);
	const bool valid = callback != nullptr && request->url != nullptr &&
		pending_unlock_store_ != nullptr &&
		user != nullptr && user->username != nullptr && user->token != nullptr &&
		ParseUnsigned(fields["a"], &achievement_id) && achievement_id != 0 &&
		ParseUnsigned(fields["h"], &hardcore) && hardcore <= 1 &&
		(fields.find("o") == fields.end() ||
			ParseUnsigned(fields["o"], &seconds_since_unlock)) &&
		fields["u"] == login_.username && fields["u"] == user->username &&
		fields["t"] == user->token && IsMd5Hex(fields["m"]);
	if (!valid) {
		integrity_failure_ = "award request could not be saved safely";
		CompleteSubmissionCallback(callback, callback_data,
			"Achievement submission could not be saved safely");
		return true;
	}

	RaPendingUnlockRecord record;
	record.account = login_.username;
	record.achievement_id = achievement_id;
	record.hardcore = hardcore != 0;
	record.game_hash = fields["m"];
	const int64_t now = static_cast<int64_t>(std::time(nullptr));
	record.unlocked_at = now - std::min<int64_t>(seconds_since_unlock, now - 1);
	int64_t record_id = 0;
	std::string error;
	if (!pending_unlock_store_->EnqueuePendingUnlock(record, &record_id, &error)) {
		integrity_failure_ = error.empty() ?
			"pending unlock storage failed" : error;
		CompleteSubmissionCallback(callback, callback_data,
			"Achievement submission could not be persisted");
		return true;
	}

	PendingAwardContext *context = new PendingAwardContext();
	context->service = this;
	context->record_id = record_id;
	context->achievement_id = achievement_id;
	context->generation = http_bridge_->CurrentGeneration();
	context->account = record.account;
	context->callback = callback;
	context->callback_data = callback_data;
	const uint64_t request_id = http_bridge_->BeginServerCall(
		request, PendingAwardCallback, context);
	context->request_id = request_id;
	if (context->request_id == 0) {
		delete context;
		return true;
	}
	pending_award_requests_[context->request_id] = context;
	return true;
}

bool RaService::InterceptLeaderboardSubmission(const rc_api_request_t *request,
	rc_client_server_callback_t callback, void *callback_data)
{
	if (request == nullptr || request->post_data == nullptr) return false;
	std::map<std::string, std::string> fields;
	if (!ParseForm(request->post_data, &fields)) {
		if (std::strstr(request->post_data, "submitlbentry") == nullptr) {
			return false;
		}
		CompleteSubmissionCallback(callback, callback_data,
			"Malformed leaderboard submission was blocked");
		return true;
	}
	if (fields["r"] != "submitlbentry") return false;

	uint32_t leaderboard_id = 0;
	int32_t score = 0;
	const rc_client_user_t *user = rc_client_get_user_info(client_);
	const bool valid = callback != nullptr && request->url != nullptr &&
		user != nullptr && user->username != nullptr && user->token != nullptr &&
		fields["u"] == login_.username && fields["u"] == user->username &&
		fields["t"] == user->token &&
		ParseUnsigned(fields["i"], &leaderboard_id) && leaderboard_id != 0 &&
		ParseSigned(fields["s"], &score) &&
		(fields.find("m") == fields.end() || IsMd5Hex(fields["m"]));
	(void)score;
	if (!valid) {
		CompleteSubmissionCallback(callback, callback_data,
			"Leaderboard submission identity validation failed");
		return true;
	}

	PendingLeaderboardContext *context = new PendingLeaderboardContext();
	context->service = this;
	context->generation = http_bridge_->CurrentGeneration();
	context->account = login_.username;
	context->url = request->url;
	context->has_post_data = request->post_data != nullptr;
	if (request->post_data != nullptr) context->post_data = request->post_data;
	if (request->content_type != nullptr) {
		context->content_type = request->content_type;
	}
	context->callback = callback;
	context->callback_data = callback_data;
	if (!StartPendingLeaderboardRequest(context)) {
		CompleteCanceledPendingLeaderboard(context,
			"Leaderboard submission HTTP request did not start");
		delete context;
	}
	return true;
}

bool RaService::StartPendingLeaderboardRequest(
	PendingLeaderboardContext *context)
{
	if (context == nullptr || http_bridge_ == nullptr || context->url.empty()) {
		return false;
	}
	rc_api_request_t request = {};
	request.url = context->url.c_str();
	request.post_data = context->has_post_data ?
		context->post_data.c_str() : nullptr;
	request.content_type = context->content_type.empty() ?
		nullptr : context->content_type.c_str();
	context->request_id = http_bridge_->BeginServerCall(
		&request, PendingLeaderboardCallback, context);
	if (context->request_id == 0) return false;
	pending_leaderboard_requests_[context->request_id] = context;
	return true;
}

void RaService::ProcessPendingLeaderboardRetries()
{
	if (pending_leaderboard_retries_.empty() || monotonic_millis_ == nullptr) {
		return;
	}
	const uint64_t now = monotonic_millis_(monotonic_millis_userdata_);
	for (auto pending = pending_leaderboard_retries_.begin();
		pending != pending_leaderboard_retries_.end();) {
		PendingLeaderboardContext *context = pending->second;
		if (context == nullptr || context->retry_at > now) {
			++pending;
			continue;
		}
		pending = pending_leaderboard_retries_.erase(pending);
		if (!StartPendingLeaderboardRequest(context)) {
			CompleteCanceledPendingLeaderboard(context,
				"Leaderboard retry HTTP request did not start");
			delete context;
		}
	}
}

void RaService::CompleteSubmissionCallback(rc_client_server_callback_t callback,
	void *callback_data, const char *message)
{
	if (callback == nullptr) return;
	const std::string body = std::string("{\"Success\":false,\"Error\":\"") +
		(message != nullptr ? message : "Submission deferred") + "\"}";
	rc_api_server_response_t response = {};
	response.body = body.c_str();
	response.body_length = body.size();
	response.http_status_code = 400;
	const bool was_canceling = canceling_submission_requests_;
	canceling_submission_requests_ = true;
	callback(&response, callback_data);
	canceling_submission_requests_ = was_canceling;
}

void RaService::CompleteCanceledPendingAward(PendingAwardContext *context,
	const char *message)
{
	if (context == nullptr || context->callback == nullptr) return;
	CompleteSubmissionCallback(context->callback, context->callback_data,
		message != nullptr ? message :
		"Achievement submission deferred because the session ended");
}

void RaService::CompleteCanceledPendingLeaderboard(
	PendingLeaderboardContext *context, const char *message)
{
	if (context == nullptr || context->callback == nullptr) return;
	CompleteSubmissionCallback(context->callback, context->callback_data,
		message != nullptr ? message :
		"Leaderboard submission ended with the game session");
}

void RaService::CancelPendingAwardRequests()
{
	const bool had_pending = !pending_award_requests_.empty();
	while (!pending_award_requests_.empty()) {
		auto pending = pending_award_requests_.begin();
		std::unique_ptr<PendingAwardContext> context(pending->second);
		const uint64_t request_id = pending->first;
		pending_award_requests_.erase(pending);
		if (http_bridge_ != nullptr) {
			http_bridge_->Abandon(request_id);
		}
		CompleteCanceledPendingAward(context.get());
	}
	if (had_pending && login_.state == RaLoginState::LoggedIn) {
		unlock_sync_ = RaUnlockSyncSnapshot();
	}
}

void RaService::CancelPendingLeaderboardRequests()
{
	while (!pending_leaderboard_requests_.empty()) {
		auto pending = pending_leaderboard_requests_.begin();
		std::unique_ptr<PendingLeaderboardContext> context(pending->second);
		const uint64_t request_id = pending->first;
		pending_leaderboard_requests_.erase(pending);
		if (http_bridge_ != nullptr) http_bridge_->Abandon(request_id);
		CompleteCanceledPendingLeaderboard(context.get());
	}
	while (!pending_leaderboard_retries_.empty()) {
		auto pending = pending_leaderboard_retries_.begin();
		std::unique_ptr<PendingLeaderboardContext> context(pending->second);
		pending_leaderboard_retries_.erase(pending);
		CompleteCanceledPendingLeaderboard(context.get());
	}
}

void RaService::CancelPendingUnlockSyncRequests()
{
	while (!pending_unlock_sync_requests_.empty()) {
		auto pending = pending_unlock_sync_requests_.begin();
		std::unique_ptr<PendingSyncContext> context(pending->second);
		const uint64_t request_id = pending->first;
		pending_unlock_sync_requests_.erase(pending);
		if (http_bridge_ != nullptr) http_bridge_->Abandon(request_id);
	}
}

void RC_CCONV RaService::PendingLeaderboardCallback(
	const rc_api_server_response_t *server_response, void *userdata)
{
	PendingLeaderboardContext *context =
		static_cast<PendingLeaderboardContext *>(userdata);
	if (context == nullptr || context->service == nullptr) return;
	RaService *service = context->service;
	auto pending = service->pending_leaderboard_requests_.find(
		context->request_id);
	if (pending != service->pending_leaderboard_requests_.end() &&
		pending->second == context) {
		service->pending_leaderboard_requests_.erase(pending);
	}
	if (context->generation != service->http_bridge_->CurrentGeneration() ||
		context->account != service->login_.username) {
		std::unique_ptr<PendingLeaderboardContext> owned(context);
		service->CompleteCanceledPendingLeaderboard(owned.get());
		return;
	}
	const bool retryable = IsRetryableSubmissionResponse(server_response) ||
		server_response == nullptr || server_response->body == nullptr ||
		server_response->body_length == 0;
	if (retryable) {
		++context->retry_count;
		const uint64_t delay = LeaderboardRetryDelayMillis(context->retry_count);
		if (delay == 0) {
			if (!service->StartPendingLeaderboardRequest(context)) {
				std::unique_ptr<PendingLeaderboardContext> owned(context);
				service->CompleteCanceledPendingLeaderboard(owned.get(),
					"Leaderboard retry HTTP request did not start");
			}
		}
		else {
			const uint64_t now = service->monotonic_millis_ != nullptr ?
				service->monotonic_millis_(service->monotonic_millis_userdata_) : 0;
			context->retry_at = now + delay;
			service->pending_leaderboard_retries_[context] = context;
		}
		return;
	}
	std::unique_ptr<PendingLeaderboardContext> owned(context);
	if (context->callback != nullptr) {
		context->callback(server_response, context->callback_data);
	}
}

void RC_CCONV RaService::PendingAwardCallback(
	const rc_api_server_response_t *server_response, void *userdata)
{
	std::unique_ptr<PendingAwardContext> context(
		static_cast<PendingAwardContext *>(userdata));
	if (context == nullptr || context->service == nullptr) return;
	RaService *service = context->service;
	auto pending = service->pending_award_requests_.find(context->request_id);
	if (pending != service->pending_award_requests_.end() &&
		pending->second == context.get()) {
		service->pending_award_requests_.erase(pending);
	}
	if (context->generation != service->http_bridge_->CurrentGeneration() ||
		context->account != service->login_.username) {
		service->CompleteCanceledPendingAward(context.get());
		return;
	}
	rc_api_award_achievement_response_t response = {};
	const int result = rc_api_process_award_achievement_server_response(
		&response, server_response);
	const bool already_awarded = IsAlreadyAwardedResponse(response);
	const bool matching_success = IsSuccessfulHttpResponse(server_response) &&
		result == RC_OK && response.response.succeeded &&
		(already_awarded ||
		response.awarded_achievement_id == context->achievement_id);
	const bool mismatched_success = result == RC_OK &&
		response.response.succeeded && !matching_success;
	const bool retryable = IsRetryableSubmissionResponse(server_response) ||
		(response.response.error_message == nullptr && result != RC_OK);
	std::string store_error;
	if (matching_success) {
		if (!service->pending_unlock_store_->RemovePendingUnlock(
			context->record_id, &store_error)) {
			service->integrity_failure_ = store_error.empty() ?
				"confirmed unlock could not be removed from the outbox" : store_error;
		}
	}
	else {
		const std::string message = mismatched_success ?
			"award response achievement ID mismatch" :
			(response.response.error_message != nullptr ?
				response.response.error_message : rc_error_str(result));
		const RaPendingUnlockStatus status = retryable ?
			RaPendingUnlockStatus::Pending : RaPendingUnlockStatus::Held;
		if (!service->pending_unlock_store_->MarkPendingUnlockAttempt(
			context->record_id, status, message, &store_error)) {
			service->integrity_failure_ = store_error.empty() ?
				"pending unlock attempt could not be recorded" : store_error;
		}
		if (status == RaPendingUnlockStatus::Held) {
			service->unlock_sync_.state = RaUnlockSyncState::Failed;
			service->unlock_sync_.message = message;
		}
	}
	rc_api_destroy_award_achievement_response(&response);
	if (retryable || mismatched_success) {
		if (retryable) {
			service->unlock_sync_ = RaUnlockSyncSnapshot();
		}
		service->CompleteCanceledPendingAward(context.get(),
			retryable ? "Achievement submission retained for later synchronization" :
			"Achievement response identity did not match the request");
		return;
	}
	if (context->callback != nullptr) {
		context->callback(server_response, context->callback_data);
	}
}

void RaService::HandleLoginCallback(int result, const char *error_message)
{
	if (result == RC_ABORTED) {
		return;
	}
	login_async_handle_ = nullptr;
	login_.result = result;
	login_.message = error_message != nullptr ? error_message : "";

	if (result != RC_OK) {
		SetFailed(result, login_.message);
		if (login_kind_ == LoginKind::SavedToken) {
			DeleteCredentialsForRejectedToken();
		}
		login_kind_ = LoginKind::None;
		return;
	}

	const rc_client_user_t *user = rc_client_get_user_info(client_);
	if (user == nullptr || user->username == nullptr || user->token == nullptr) {
		SetFailed(RC_MISSING_VALUE, "Login succeeded without user credentials");
		if (login_kind_ == LoginKind::SavedToken) {
			DeleteCredentialsForRejectedToken();
		}
		login_kind_ = LoginKind::None;
		return;
	}

	login_.state = RaLoginState::LoggedIn;
	login_.username = user->username;
	login_.display_name = user->display_name != nullptr ?
		user->display_name : user->username;

	RaCredentials credentials;
	credentials.username = user->username;
	credentials.token = user->token;
	std::string ignored_error;
	if (credentials_ != nullptr) {
		credentials_->Save(credentials, &ignored_error);
		credentials_->ClearSecret(&credentials);
	}
	BeginPendingUnlockSync(&ignored_error);

	login_kind_ = LoginKind::None;
}

void RaService::HandleLoadGameCallback(int result, const char *error_message)
{
	if (result == RC_ABORTED) {
		return;
	}
	game_session_.result = result;
	game_session_.message = error_message != nullptr ? error_message : "";
	game_session_.load_state = rc_client_get_load_game_state(client_);

	if (result != RC_OK) {
		DisableGameSession(result, game_session_.message);
		return;
	}

	const rc_client_game_t *game = rc_client_get_game_info(client_);
	if (game == nullptr || game->id == 0) {
		DisableGameSession(RC_NO_GAME_LOADED, "RA game information is unavailable");
		return;
	}

	game_session_.state = RaGameSessionState::Loaded;
	game_session_.disabled_for_session = false;
	game_session_.game_id = game->id;
	game_session_.console_id = game->console_id;
	game_session_.title = game->title != nullptr ? game->title : "";
	game_session_.hash = game->hash != nullptr ? game->hash : game_session_.hash;
	game_session_.badge_url = game->badge_url != nullptr ? game->badge_url : "";
	verified_media_game_ids_[game_session_.hash] = game_session_.game_id;
	UpdateRichPresenceEvent();
}

void RaService::HandleResolveMediaHashCallback(
	const rc_api_server_response_t *server_response)
{
	if (!media_change_preflight_pending_ ||
		media_change_.state != RaMediaChangeState::Pending) {
		return;
	}
	media_change_preflight_pending_ = false;
	rc_api_resolve_hash_response_t response = {};
	const int result = rc_api_process_resolve_hash_server_response(
		&response, server_response);
	const std::string message = response.response.error_message != nullptr ?
		response.response.error_message : "";
	if (result != RC_OK || !response.response.succeeded) {
		media_change_.state = RaMediaChangeState::Failed;
		media_change_.result = result != RC_OK ? result : RC_API_FAILURE;
		media_change_.message = message.empty() ?
			rc_error_str(media_change_.result) : message;
		rc_api_destroy_resolve_hash_response(&response);
		return;
	}
	if (response.game_id != game_session_.game_id) {
		media_change_.state = RaMediaChangeState::Failed;
		media_change_.result = RC_INVALID_STATE;
		media_change_.message = response.game_id == 0 ?
			"RA media is not identified" :
			"RA media belongs to another game";
		rc_api_destroy_resolve_hash_response(&response);
		return;
	}
	verified_media_game_ids_[media_change_.hash] = response.game_id;
	rc_api_destroy_resolve_hash_response(&response);
	StartClientMediaChange(nullptr);
}

void RaService::HandleVerifyMediaHashCallback(
	const rc_api_server_response_t *server_response)
{
	if (!media_verification_pending_ ||
		media_verification_.state != RaMediaChangeState::Pending) {
		return;
	}
	media_verification_pending_ = false;
	media_verification_request_id_ = 0;
	rc_api_resolve_hash_response_t response = {};
	const int result = rc_api_process_resolve_hash_server_response(
		&response, server_response);
	const std::string message = response.response.error_message != nullptr ?
		response.response.error_message : "";
	if (result != RC_OK || !response.response.succeeded) {
		media_verification_.state = RaMediaChangeState::Failed;
		media_verification_.failure = RaMediaVerificationFailure::Unavailable;
		media_verification_.result = result != RC_OK ? result : RC_API_FAILURE;
		media_verification_.message = message.empty() ?
			rc_error_str(media_verification_.result) : message;
		rc_api_destroy_resolve_hash_response(&response);
		return;
	}
	if (response.game_id != media_verification_expected_game_id_) {
		media_verification_.state = RaMediaChangeState::Failed;
		media_verification_.failure = RaMediaVerificationFailure::DifferentGame;
		media_verification_.result = RC_INVALID_STATE;
		media_verification_.message = response.game_id == 0 ?
			"RA media is not identified" :
			"RA media belongs to another game";
		rc_api_destroy_resolve_hash_response(&response);
		return;
	}
	verified_media_game_ids_[media_verification_.hash] = response.game_id;
	media_verification_.state = RaMediaChangeState::Succeeded;
	rc_api_destroy_resolve_hash_response(&response);
}

void RaService::HandleMediaChangeCallback(int result,
	const char *error_message)
{
	if (result == RC_ABORTED) {
		return;
	}
	media_change_async_handle_ = nullptr;
	media_change_.result = result;
	media_change_.message = error_message != nullptr ? error_message : "";
	if (result != RC_OK) {
		media_change_.state = RaMediaChangeState::Failed;
		if (media_change_.message.empty()) {
			media_change_.message = rc_error_str(result);
		}
		return;
	}

	media_change_.state = RaMediaChangeState::Succeeded;
	game_session_.hash = media_change_.hash;
}

void RaService::HandleLibraryHashesCallback(int result,
	const char *error_message, rc_client_hash_library_t *list)
{
	library_sync_async_handle_ = nullptr;
	if (library_sync_.state != RaLibrarySyncState::PendingHashes) {
		if (list != nullptr) {
			rc_client_destroy_hash_library(list);
		}
		return;
	}
	if (result != RC_OK || list == nullptr) {
		FailLibrarySync(result, error_message);
		if (list != nullptr) {
			rc_client_destroy_hash_library(list);
		}
		return;
	}

	std::map<std::string, uint32_t> matches;
	std::map<uint32_t, bool> game_ids;
	for (uint32_t i = 0; i < list->num_entries; i++) {
		const rc_client_hash_library_entry_t& source = list->entries[i];
		const std::string hash = source.hash;
		if (library_sync_local_hashes_.find(hash) ==
			library_sync_local_hashes_.end()) {
			continue;
		}
		const auto inserted = matches.emplace(hash, source.game_id);
		if (!inserted.second && inserted.first->second != source.game_id) {
			rc_client_destroy_hash_library(list);
			FailLibrarySync(RC_INVALID_STATE,
				"RA hash library contains conflicting entries");
			return;
		}
		game_ids[source.game_id] = true;
	}
	rc_client_destroy_hash_library(list);
	for (const auto& match : matches) {
		RaLibrarySyncHash item;
		item.hash = match.first;
		item.game_id = match.second;
		library_sync_.hashes.push_back(item);
	}
	for (const auto& game_id : game_ids) {
		library_sync_title_game_ids_.push_back(game_id.first);
	}
	library_sync_title_offset_ = 0;
	if (!library_sync_title_game_ids_.empty()) {
		library_sync_.state = RaLibrarySyncState::PendingTitles;
		StartNextLibraryTitleBatch(nullptr);
	}
	else {
		StartLibraryProgress(nullptr);
	}
}

bool RaService::StartNextLibraryTitleBatch(std::string *error)
{
	if (library_sync_title_offset_ >= library_sync_title_game_ids_.size()) {
		return StartLibraryProgress(error);
	}
	const size_t remaining =
		library_sync_title_game_ids_.size() - library_sync_title_offset_;
	const uint32_t count = static_cast<uint32_t>(remaining > 100 ? 100 : remaining);
	rc_client_async_handle_t *handle = rc_client_begin_fetch_game_titles(client_,
		&library_sync_title_game_ids_[library_sync_title_offset_], count,
		LibraryTitlesCallback, this);
	library_sync_async_handle_ =
		library_sync_.state == RaLibrarySyncState::PendingTitles ? handle : nullptr;
	if (handle == nullptr &&
		library_sync_.state == RaLibrarySyncState::PendingTitles) {
		FailLibrarySync(RC_INVALID_STATE, "RA game titles request did not start");
	}
	if (library_sync_.state == RaLibrarySyncState::Failed) {
		if (error != nullptr) {
			*error = library_sync_.message;
		}
		return false;
	}
	return true;
}

void RaService::HandleLibraryTitlesCallback(int result,
	const char *error_message, rc_client_game_title_list_t *list)
{
	library_sync_async_handle_ = nullptr;
	if (library_sync_.state != RaLibrarySyncState::PendingTitles) {
		if (list != nullptr) {
			rc_client_destroy_game_title_list(list);
		}
		return;
	}
	if (result != RC_OK || list == nullptr) {
		FailLibrarySync(result, error_message);
		if (list != nullptr) {
			rc_client_destroy_game_title_list(list);
		}
		return;
	}
	for (uint32_t i = 0; i < list->num_entries; i++) {
		const rc_client_game_title_entry_t& source = list->entries[i];
		RaLibrarySyncTitle item;
		item.game_id = source.game_id;
		item.title = source.title != nullptr ? source.title : "";
		item.badge_url = source.badge_url != nullptr ? source.badge_url : "";
		library_sync_.titles.push_back(item);
	}
	library_sync_title_offset_ +=
		library_sync_title_game_ids_.size() - library_sync_title_offset_ > 100 ?
		100 : library_sync_title_game_ids_.size() - library_sync_title_offset_;
	rc_client_destroy_game_title_list(list);
	StartNextLibraryTitleBatch(nullptr);
}

bool RaService::StartLibraryProgress(std::string *error)
{
	library_sync_.state = RaLibrarySyncState::PendingProgress;
	rc_client_async_handle_t *handle = rc_client_begin_fetch_all_user_progress(
		client_, RC_CONSOLE_PC8800, LibraryProgressCallback, this);
	library_sync_async_handle_ =
		library_sync_.state == RaLibrarySyncState::PendingProgress ? handle : nullptr;
	if (handle == nullptr &&
		library_sync_.state == RaLibrarySyncState::PendingProgress) {
		FailLibrarySync(RC_INVALID_STATE, "RA progress request did not start");
	}
	if (library_sync_.state == RaLibrarySyncState::Failed) {
		if (error != nullptr) {
			*error = library_sync_.message;
		}
		return false;
	}
	return true;
}

void RaService::HandleLibraryProgressCallback(int result,
	const char *error_message, rc_client_all_user_progress_t *list)
{
	library_sync_async_handle_ = nullptr;
	if (library_sync_.state != RaLibrarySyncState::PendingProgress) {
		if (list != nullptr) {
			rc_client_destroy_all_user_progress(list);
		}
		return;
	}
	if (result != RC_OK || list == nullptr) {
		FailLibrarySync(result, error_message);
		if (list != nullptr) {
			rc_client_destroy_all_user_progress(list);
		}
		return;
	}
	for (uint32_t i = 0; i < list->num_entries; i++) {
		const rc_client_all_user_progress_entry_t& source = list->entries[i];
		RaLibrarySyncProgress item;
		item.game_id = source.game_id;
		item.total = source.num_achievements;
		item.unlocked = source.num_unlocked_achievements;
		item.hardcore_unlocked =
			source.num_unlocked_achievements_hardcore;
		library_sync_.progress.push_back(item);
	}
	rc_client_destroy_all_user_progress(list);
	library_sync_.state = RaLibrarySyncState::Succeeded;
}

void RaService::FailLibrarySync(int result, const char *message)
{
	library_sync_async_handle_ = nullptr;
	library_sync_.state = RaLibrarySyncState::Failed;
	library_sync_.result = result;
	library_sync_.message = message != nullptr && message[0] != '\0' ?
		message : rc_error_str(result);
}

void RaService::HandleLeaderboardEntriesCallback(int result,
	const char *error_message, rc_client_leaderboard_entry_list_t *list)
{
	if (result == RC_ABORTED) {
		return;
	}
	leaderboard_entries_async_handle_ = nullptr;
	leaderboard_entries_.result = result;
	leaderboard_entries_.message = error_message != nullptr ? error_message : "";

	if (result != RC_OK || list == nullptr) {
		leaderboard_entries_.state = RaLeaderboardEntriesState::Failed;
		if (leaderboard_entries_.message.empty()) {
			leaderboard_entries_.message = rc_error_str(result);
		}
		return;
	}

	leaderboard_entries_.state = RaLeaderboardEntriesState::Loaded;
	leaderboard_entries_.total_entries = list->total_entries;
	leaderboard_entries_.user_index = list->user_index;
	leaderboard_entries_.entries.clear();
	for (uint32_t i = 0; i < list->num_entries; i++) {
		const rc_client_leaderboard_entry_t& source = list->entries[i];
		RaLeaderboardEntryItem item;
		item.rank = source.rank;
		item.index = source.index;
		item.username = SafeString(source.user);
		item.display = source.display;
		leaderboard_entries_.entries.push_back(item);
	}
	rc_client_destroy_leaderboard_entry_list(list);
}

void RaService::HandleClientEvent(const rc_client_event_t *event)
{
	if (event == nullptr) {
		return;
	}
	if (canceling_submission_requests_ &&
		event->type == RC_CLIENT_EVENT_SERVER_ERROR) {
		return;
	}

	RaEvent copied;
	copied.raw_type = event->type;
	copied.type = MapEventType(event->type);
	CopyAchievement(&copied.achievement, event->achievement);
	CopyLeaderboard(&copied.leaderboard, event->leaderboard);
	CopyLeaderboardTracker(&copied.leaderboard, event->leaderboard_tracker);
	CopyScoreboard(&copied.scoreboard, event->leaderboard_scoreboard);
	CopyServerError(&copied.server_error, event->server_error);
	CopySubset(&copied.subset, event->subset);
	events_.push_back(copied);
}

void RaService::UpdateRichPresenceEvent()
{
	if (client_ == nullptr || game_session_.state != RaGameSessionState::Loaded ||
		rc_client_has_rich_presence(client_) == 0) {
		return;
	}

	char buffer[256];
	const size_t written = rc_client_get_rich_presence_message(
		client_, buffer, sizeof(buffer));
	buffer[sizeof(buffer) - 1] = '\0';
	std::string current = written > 0 ? buffer : "";
	if (current == rich_presence_) {
		return;
	}

	rich_presence_ = current;
	RaEvent event;
	event.type = RaEventType::RichPresenceChanged;
	event.rich_presence = rich_presence_;
	events_.push_back(event);
}

void RaService::SetFailed(int result, const std::string& message)
{
	login_.state = RaLoginState::Failed;
	login_.result = result;
	login_.message = message;
}

void RaService::DisableGameSession(int result, const std::string& message)
{
	CancelPendingAwardRequests();
	CancelPendingLeaderboardRequests();
	if (client_ != nullptr) {
		rc_client_unload_game(client_);
	}
	game_session_.state = RaGameSessionState::DisabledForSession;
	game_session_.result = result;
	game_session_.message = message;
	game_session_.load_state = client_ != nullptr ?
		rc_client_get_load_game_state(client_) : 0;
	game_session_.disabled_for_session = true;
	rich_presence_.clear();
}

void RaService::DeleteCredentialsForRejectedToken()
{
	std::string ignored_error;
	if (credentials_ != nullptr) {
		credentials_->Delete(&ignored_error);
		login_.credentials_deleted = true;
	}
}

void RaService::AbortLoginInProgress()
{
	InvalidatePendingUnlockSync();
	const bool reset_client_login =
		login_.state == RaLoginState::LoginPending ||
		login_.state == RaLoginState::LoggedIn;
	if (client_ != nullptr && login_async_handle_ != nullptr) {
		rc_client_abort_async(client_, login_async_handle_);
	}
	if (client_ != nullptr && reset_client_login) {
		rc_client_logout(client_);
	}
	login_async_handle_ = nullptr;
	if (login_.state == RaLoginState::LoginPending) {
		login_ = RaLoginSnapshot();
		login_.state = RaLoginState::LoggedOut;
	}
	login_kind_ = LoginKind::None;
}

void RaService::AbortLeaderboardEntriesInProgress()
{
	if (client_ != nullptr && leaderboard_entries_async_handle_ != nullptr) {
		rc_client_abort_async(client_, leaderboard_entries_async_handle_);
	}
	leaderboard_entries_async_handle_ = nullptr;
	if (leaderboard_entries_.state == RaLeaderboardEntriesState::FetchPending) {
		leaderboard_entries_ = RaLeaderboardEntriesSnapshot();
	}
}

void RaService::AbortMediaChangeInProgress()
{
	media_change_preflight_pending_ = false;
	CancelMediaVerification();
	if (client_ != nullptr && media_change_async_handle_ != nullptr) {
		rc_client_abort_async(client_, media_change_async_handle_);
	}
	media_change_async_handle_ = nullptr;
	if (media_change_.state == RaMediaChangeState::Pending) {
		media_change_ = RaMediaChangeSnapshot();
	}
	media_verification_ = RaMediaVerificationSnapshot();
	media_verification_expected_game_id_ = 0;
}

void RaService::AbortLibrarySyncInProgress()
{
	if (client_ != nullptr && library_sync_async_handle_ != nullptr) {
		rc_client_abort_async(client_, library_sync_async_handle_);
	}
	library_sync_async_handle_ = nullptr;
	library_sync_ = RaLibrarySyncSnapshot();
	library_sync_local_hashes_.clear();
	library_sync_title_game_ids_.clear();
	library_sync_title_offset_ = 0;
}

} // namespace Xm8Ra

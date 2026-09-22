//
// eXcellent Multi-platform emulator type 8 - 'XM8'
// based on ePC-8801MA
//
// [ RetroAchievements overlay state ]
//

#ifndef XM8_RA_OVERLAY_H
#define XM8_RA_OVERLAY_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Xm8Ra {

enum class RaOverlayScreen {
	None,
	Library,
	GameDetail,
	Achievements,
	AchievementDetail,
	Leaderboards,
	Login,
};

enum class RaOverlayLoginField {
	Username,
	Password,
};

enum class RaOverlayLoginTarget {
	Username,
	Password,
	Login,
	Cancel,
};

enum class RaOverlayKey {
	Tab,
	Backspace,
	Enter,
	Escape,
	Up,
	Down,
	Left,
	Right,
	PageUp,
	PageDown,
};

enum class RaOverlayAction {
	None,
	SubmitLogin,
	OpenLibraryGame,
	ResolveLibraryConflict,
	Close,
};

enum class RaOverlayConflictKind {
	None = 0,
	Merge = 1,
	Split = 2,
	Manual = 3,
};

enum class RaNoticePriority {
	Low = 0,
	Normal = 1,
	Important = 2,
	Critical = 3,
};

struct RaVisibleNotice {
	std::string text;
	RaNoticePriority priority = RaNoticePriority::Low;
	std::string badge_url;
};

enum class RaStatusPageType {
	None,
	Challenge,
	Progress,
	LeaderboardTracker,
};

struct RaStatusPageSnapshot {
	RaStatusPageType type = RaStatusPageType::None;
	uint32_t id = 0;
	size_t index = 0;
	size_t total = 0;
	std::string title;
	std::string value;
	std::string badge_url;
};

struct RaOverlaySnapshot {
	bool mode_enabled = false;
	bool hardcore_enabled = false;
	std::string status_text;
	std::string user_name;
	std::string game_title;
	std::string rich_presence;
};

struct RaOverlayLoginSnapshot {
	bool active = false;
	RaOverlayLoginField field = RaOverlayLoginField::Username;
	RaOverlayLoginTarget focus = RaOverlayLoginTarget::Username;
	std::string username;
	std::string masked_password;
	std::string status_message;
	bool can_submit = false;
};

struct RaOverlayAchievementItem {
	uint32_t id = 0;
	uint32_t points = 0;
	uint8_t unlocked = 0;
	std::string title;
	std::string description;
	std::string measured_progress;
	std::string badge_url;
	std::string badge_locked_url;
	std::string bucket_label;
};

struct RaOverlayAchievementListSnapshot {
	bool active = false;
	bool game_loaded = false;
	bool has_achievements = false;
	size_t selected_index = 0;
	size_t first_visible_index = 0;
	uint32_t selection_revision = 0;
	std::string game_title;
	std::string status_message;
	std::vector<RaOverlayAchievementItem> achievements;
};

struct RaOverlayLibraryItem {
	int64_t game_id = 0;
	int64_t ra_game_id = 0;
	int identification_state = 0;
	RaOverlayConflictKind conflict_kind = RaOverlayConflictKind::None;
	std::string title;
	int media_count = 0;
	int health_state = 0;
	int64_t last_played_at = 0;
	bool has_progress = false;
	int core_total = 0;
	int core_unlocked = 0;
	int hardcore_unlocked = 0;
	int points_total = 0;
	int points_unlocked = 0;
	std::string badge_url;
};

struct RaOverlayLibraryListSnapshot {
	bool active = false;
	size_t selected_index = 0;
	size_t first_visible_index = 0;
	uint32_t selection_revision = 0;
	std::string status_message;
	std::vector<RaOverlayLibraryItem> games;
};

struct RaOverlayAchievementDetailSnapshot {
	bool active = false;
	size_t selected_index = 0;
	size_t item_count = 0;
	int scroll_offset = 0;
	uint32_t selection_revision = 0;
	std::string game_title;
	RaOverlayAchievementItem achievement;
};

struct RaOverlayLeaderboardItem {
	struct ScoreboardEntry {
		uint32_t rank = 0;
		std::string username;
		std::string score;
	};

	uint32_t id = 0;
	uint8_t state = 0;
	uint8_t format = 0;
	bool lower_is_better = false;
	bool has_scoreboard = false;
	bool entries_pending = false;
	bool entries_failed = false;
	bool has_entries = false;
	uint32_t new_rank = 0;
	uint32_t num_entries = 0;
	uint32_t entry_total = 0;
	std::string title;
	std::string description;
	std::string tracker_value;
	std::string bucket_label;
	std::string submitted_score;
	std::string best_score;
	std::string entries_message;
	std::vector<ScoreboardEntry> top_entries;
	std::vector<ScoreboardEntry> entries;
};

struct RaOverlayLeaderboardListSnapshot {
	bool active = false;
	bool game_loaded = false;
	size_t selected_index = 0;
	size_t first_visible_index = 0;
	uint32_t selection_revision = 0;
	std::string game_title;
	std::string status_message;
	std::vector<RaOverlayLeaderboardItem> leaderboards;
};

size_t FindRaLibraryItemIndex(const RaOverlayLibraryListSnapshot& snapshot,
	int64_t game_id);
size_t FindRaAchievementItemIndex(
	const RaOverlayAchievementListSnapshot& snapshot, uint32_t achievement_id);
size_t FindRaLeaderboardItemIndex(
	const RaOverlayLeaderboardListSnapshot& snapshot, uint32_t leaderboard_id);

class RaOverlay {
public:
	void Clear();
	void ClearNotices();
	void AddNotice(const std::string& text, uint32_t now_ms,
		uint32_t duration_ms = 5000,
		RaNoticePriority priority = RaNoticePriority::Normal,
		const std::string& badge_url = std::string());
	void ReplaceNotice(const std::string& text, uint32_t now_ms,
		uint32_t duration_ms = 5000,
		RaNoticePriority priority = RaNoticePriority::Normal,
		const std::string& badge_url = std::string());
	void SetNoticesPaused(bool paused, uint32_t now_ms);
	bool HasVisibleNotice(uint32_t now_ms);
	std::string VisibleNotice(uint32_t now_ms);
	std::vector<RaVisibleNotice> VisibleNotices(uint32_t now_ms);
	size_t NoticeQueueSize() const;

	void ClearStatusPages();
	void ClearGameplayStatus();
	void SetLastSubmissionError(const std::string& text);
	void ClearLastSubmissionError();
	const std::string& LastSubmissionError() const;
	void ShowChallenge(uint32_t id, const std::string& title,
		const std::string& badge_url, uint32_t now_ms);
	void HideChallenge(uint32_t id, uint32_t now_ms);
	void ShowProgress(uint32_t id, const std::string& title,
		const std::string& value, const std::string& badge_url,
		uint32_t now_ms);
	void UpdateProgress(uint32_t id, const std::string& title,
		const std::string& value, const std::string& badge_url,
		uint32_t now_ms);
	void HideProgress(uint32_t now_ms);
	void ShowLeaderboardTracker(uint32_t id, const std::string& value,
		uint32_t now_ms);
	void UpdateLeaderboardTracker(uint32_t id, const std::string& value,
		uint32_t now_ms);
	void HideLeaderboardTracker(uint32_t id, uint32_t now_ms);
	void SetStatusPagesPaused(bool paused, uint32_t now_ms);
	RaStatusPageSnapshot VisibleStatusPage(uint32_t now_ms);
	bool NextStatusPage(uint32_t now_ms);
	size_t StatusPageCount() const;

	void SetSnapshot(const RaOverlaySnapshot& snapshot);
	const RaOverlaySnapshot& Snapshot() const;

	void OpenLibrary(const RaOverlayLibraryListSnapshot& snapshot);
	void OpenAchievements(const RaOverlayAchievementListSnapshot& snapshot);
	void OpenLeaderboards(const RaOverlayLeaderboardListSnapshot& snapshot);
	void UpdateLeaderboards(const RaOverlayLeaderboardListSnapshot& snapshot);
	void OpenLogin(const std::string& username = std::string());
	bool IsBlocking() const;
	RaOverlayScreen Screen() const;
	RaOverlayLibraryListSnapshot LibraryListSnapshot() const;
	RaOverlayAchievementListSnapshot AchievementListSnapshot() const;
	RaOverlayAchievementDetailSnapshot AchievementDetailSnapshot() const;
	RaOverlayLeaderboardListSnapshot LeaderboardListSnapshot() const;
	RaOverlayLoginSnapshot LoginSnapshot() const;
	RaOverlayAction OnTextInput(const char *text);
	RaOverlayAction OnControlKey(RaOverlayKey key);
	RaOverlayAction OnAchievementPointer(int x, int y, bool activate);
	RaOverlayAction OnListPointer(int x, int y, bool activate);
	bool ListTargetAt(int x, int y, size_t *index) const;
	RaOverlayAction OnListScroll(int delta);
	RaOverlayAction OnLoginTarget(RaOverlayLoginTarget target, bool activate);
	bool LoginTargetAt(int x, int y, RaOverlayLoginTarget *target) const;
	RaOverlayAction OnLoginPointer(int x, int y, bool activate);
	bool ConsumeSubmittedLogin(std::string *username, std::string *password);
	RaOverlayAction SubmitLoginCredentials(const char *username,
		const char *password);
	bool SelectedLibraryGameId(int64_t *game_id) const;
	bool CanLaunchSelectedLibraryGame() const;
	bool CanResolveSelectedLibraryConflict() const;
	bool OpenSelectedLibraryGameDetail();
	void SelectLibraryIndex(size_t index);
	void SelectAchievementIndex(size_t index);
	void SelectLeaderboardIndex(size_t index);
	void SetListFirstVisibleIndex(size_t index);
	bool OpenSelectedAchievementDetail();
	void ShowLibrary();
	void ShowAchievements();
	void ShowLeaderboards();
	void ScrollAchievementDetail(int delta);
	void SetAchievementDetailScroll(int offset);
	void SetLibraryStatusMessage(const std::string& message);
	void SetLoginStatus(const std::string& message);
	void CloseScreen();

private:
	void MoveListSelection(int delta);
	void OpenAchievementDetail();
	void CloseAchievementDetail();
	void MoveAchievementDetailScroll(int delta);
	void NormalizeListSelection();
	size_t ListItemCount() const;
	void MoveLoginFocus(int delta);
	bool IsLoginTextFocused() const;
	void SetLoginFieldFocus(RaOverlayLoginField field);
	void WipeLoginPassword();
	bool AppendLoginText(const char *text);
	bool BackspaceLoginField();
	RaOverlayAction ActivateLoginFocus();
	bool CanSubmitLogin() const;

	struct QueuedNotice {
		std::string text;
		std::string badge_url;
		RaNoticePriority priority = RaNoticePriority::Low;
		uint32_t remaining_ms = 0;
		uint32_t expires_at_ms = 0;
		uint64_t sequence = 0;
		bool active = false;
	};
	void RebalanceNotices(uint32_t now_ms);
	struct StatusPage {
		RaStatusPageType type = RaStatusPageType::None;
		uint32_t id = 0;
		uint64_t sequence = 0;
		std::string title;
		std::string value;
		std::string badge_url;
	};
	std::vector<StatusPage *> OrderedStatusPages();
	std::vector<const StatusPage *> OrderedStatusPages() const;
	void SelectStatusPage(RaStatusPageType type, uint32_t id,
		uint32_t now_ms);
	void NormalizeStatusSelection(uint32_t now_ms);
	void AdvanceStatusPage(uint32_t now_ms);
	StatusPage *FindStatusPage(RaStatusPageType type, uint32_t id);
	const StatusPage *FindStatusPage(RaStatusPageType type, uint32_t id) const;
	std::vector<QueuedNotice> notices_;
	uint64_t next_notice_sequence_ = 1;
	bool notices_paused_ = false;
	std::vector<StatusPage> challenge_pages_;
	StatusPage progress_page_;
	bool progress_page_active_ = false;
	std::vector<StatusPage> tracker_pages_;
	uint64_t next_status_sequence_ = 1;
	RaStatusPageType selected_status_type_ = RaStatusPageType::None;
	uint32_t selected_status_id_ = 0;
	uint32_t status_rotation_remaining_ms_ = 3000;
	uint32_t status_rotation_expires_at_ms_ = 0;
	bool status_pages_paused_ = false;
	std::string last_submission_error_;
	RaOverlaySnapshot snapshot_;
	RaOverlayLibraryListSnapshot library_;
	RaOverlayAchievementListSnapshot achievements_;
	RaOverlayLeaderboardListSnapshot leaderboards_;
	RaOverlayScreen screen_ = RaOverlayScreen::None;
	uint32_t achievement_selection_revision_ = 0;
	int achievement_detail_scroll_ = 0;
	RaOverlayLoginField login_field_ = RaOverlayLoginField::Username;
	RaOverlayLoginTarget login_focus_ = RaOverlayLoginTarget::Username;
	std::string login_username_;
	std::string login_password_;
	std::string login_status_;
	bool login_submit_pending_ = false;
};

} // namespace Xm8Ra

#endif // XM8_RA_OVERLAY_H

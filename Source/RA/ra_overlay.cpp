//
// eXcellent Multi-platform emulator type 8 - 'XM8'
// based on ePC-8801MA
//
// [ RetroAchievements overlay state ]
//

#include "ra_overlay.h"

#include <algorithm>
#include <cstddef>

namespace Xm8Ra {

size_t FindRaLibraryItemIndex(const RaOverlayLibraryListSnapshot& snapshot,
	int64_t game_id)
{
	for (size_t index = 0; index < snapshot.games.size(); ++index) {
		if (snapshot.games[index].game_id == game_id) return index;
	}
	return 0;
}

size_t FindRaAchievementItemIndex(
	const RaOverlayAchievementListSnapshot& snapshot, uint32_t achievement_id)
{
	for (size_t index = 0; index < snapshot.achievements.size(); ++index) {
		if (snapshot.achievements[index].id == achievement_id) return index;
	}
	return 0;
}

size_t FindRaLeaderboardItemIndex(
	const RaOverlayLeaderboardListSnapshot& snapshot, uint32_t leaderboard_id)
{
	for (size_t index = 0; index < snapshot.leaderboards.size(); ++index) {
		if (snapshot.leaderboards[index].id == leaderboard_id) return index;
	}
	return 0;
}

namespace {

const size_t kMaxUsernameBytes = 256;
const size_t kMaxPasswordBytes = 1024;
const size_t kMenuListVisibleRows = 7;
const size_t kMaxVisibleNotices = 1;
const size_t kMaxQueuedNotices = 64;
const uint32_t kStatusRotationMs = 3000;
const int kIdentificationIdentified = 1;

const int kMenuListRowX = 80;
const int kMenuListRowY = 80;
const int kMenuListRowW = 480;
const int kMenuListRowH = 40;
const int kMenuListRowPitch = 40;

const int kLoginUserBoxX = 232;
const int kLoginUserBoxY = 130;
const int kLoginUserBoxW = 272;
const int kLoginUserBoxH = 28;
const int kLoginPassBoxX = 232;
const int kLoginPassBoxY = 170;
const int kLoginPassBoxW = 272;
const int kLoginPassBoxH = 28;
const int kLoginButtonX = 256;
const int kLoginButtonY = 220;
const int kLoginButtonW = 112;
const int kLoginButtonH = 30;
const int kCancelButtonX = 384;
const int kCancelButtonY = 220;
const int kCancelButtonW = 112;
const int kCancelButtonH = 30;

bool IsAllowedLoginByte(unsigned char ch)
{
	return ch >= 0x20 && ch <= 0x7e;
}

bool HitRect(int x, int y, int rx, int ry, int rw, int rh)
{
	return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

} // namespace

void RaOverlay::Clear()
{
	ClearGameplayStatus();
	ClearLastSubmissionError();
	snapshot_ = RaOverlaySnapshot();
	CloseScreen();
}

void RaOverlay::ClearNotices()
{
	notices_.clear();
	next_notice_sequence_ = 1;
	notices_paused_ = false;
}

void RaOverlay::AddNotice(const std::string& text, uint32_t now_ms,
	uint32_t duration_ms, RaNoticePriority priority,
	const std::string& badge_url)
{
	if (text.empty() || duration_ms == 0) {
		return;
	}
	if (!notices_paused_) {
		RebalanceNotices(now_ms);
	}
	QueuedNotice notice;
	notice.text = text;
	notice.badge_url = badge_url;
	notice.priority = priority;
	notice.remaining_ms = duration_ms;
	notice.sequence = next_notice_sequence_++;
	notices_.push_back(notice);
	if (!notices_paused_) {
		RebalanceNotices(now_ms);
	}
	if (notices_.size() > kMaxQueuedNotices) {
		auto drop = std::min_element(notices_.begin(), notices_.end(),
			[](const QueuedNotice& left, const QueuedNotice& right) {
				if (left.priority != right.priority) {
					return left.priority < right.priority;
				}
				return left.sequence > right.sequence;
			});
		if (drop != notices_.end()) {
			notices_.erase(drop);
		}
	}
}

void RaOverlay::ReplaceNotice(const std::string& text, uint32_t now_ms,
	uint32_t duration_ms, RaNoticePriority priority,
	const std::string& badge_url)
{
	ClearNotices();
	AddNotice(text, now_ms, duration_ms, priority, badge_url);
}

void RaOverlay::SetNoticesPaused(bool paused, uint32_t now_ms)
{
	if (paused == notices_paused_) {
		return;
	}
	if (paused) {
		RebalanceNotices(now_ms);
		for (QueuedNotice& notice : notices_) {
			if (notice.active) {
				notice.remaining_ms = notice.expires_at_ms - now_ms;
				notice.active = false;
			}
		}
		notices_paused_ = true;
	}
	else {
		notices_paused_ = false;
		RebalanceNotices(now_ms);
	}
}

bool RaOverlay::HasVisibleNotice(uint32_t now_ms)
{
	return !VisibleNotices(now_ms).empty();
}

std::string RaOverlay::VisibleNotice(uint32_t now_ms)
{
	const std::vector<RaVisibleNotice> visible = VisibleNotices(now_ms);
	return visible.empty() ? std::string() : visible.front().text;
}

std::vector<RaVisibleNotice> RaOverlay::VisibleNotices(uint32_t now_ms)
{
	std::vector<RaVisibleNotice> visible;
	if (notices_paused_) {
		return visible;
	}
	RebalanceNotices(now_ms);
	for (const QueuedNotice& notice : notices_) {
		if (notice.active) {
			visible.push_back({notice.text, notice.priority, notice.badge_url});
		}
	}
	std::stable_sort(visible.begin(), visible.end(),
		[](const RaVisibleNotice& left, const RaVisibleNotice& right) {
			return left.priority > right.priority;
		});
	return visible;
}

void RaOverlay::ClearStatusPages()
{
	challenge_pages_.clear();
	progress_page_ = StatusPage();
	progress_page_active_ = false;
	tracker_pages_.clear();
	next_status_sequence_ = 1;
	selected_status_type_ = RaStatusPageType::None;
	selected_status_id_ = 0;
	status_rotation_remaining_ms_ = kStatusRotationMs;
	status_rotation_expires_at_ms_ = 0;
	status_pages_paused_ = false;
}

void RaOverlay::ClearGameplayStatus()
{
	ClearNotices();
	ClearStatusPages();
}

void RaOverlay::SetLastSubmissionError(const std::string& text)
{
	last_submission_error_ = text;
}

void RaOverlay::ClearLastSubmissionError()
{
	last_submission_error_.clear();
}

const std::string& RaOverlay::LastSubmissionError() const
{
	return last_submission_error_;
}

RaOverlay::StatusPage *RaOverlay::FindStatusPage(RaStatusPageType type,
	uint32_t id)
{
	if (type == RaStatusPageType::Progress) {
		return progress_page_active_ ? &progress_page_ : nullptr;
	}
	if (type != RaStatusPageType::Challenge &&
		type != RaStatusPageType::LeaderboardTracker) {
		return nullptr;
	}
	std::vector<StatusPage>& pages =
		type == RaStatusPageType::Challenge ? challenge_pages_ : tracker_pages_;
	const auto found = std::find_if(pages.begin(), pages.end(),
		[type, id](const StatusPage& page) {
			return page.type == type && page.id == id;
		});
	return found != pages.end() ? &*found : nullptr;
}

const RaOverlay::StatusPage *RaOverlay::FindStatusPage(
	RaStatusPageType type, uint32_t id) const
{
	return const_cast<RaOverlay *>(this)->FindStatusPage(type, id);
}

std::vector<RaOverlay::StatusPage *> RaOverlay::OrderedStatusPages()
{
	std::vector<StatusPage *> pages;
	pages.reserve(challenge_pages_.size() + tracker_pages_.size() +
		(progress_page_active_ ? 1 : 0));
	std::vector<StatusPage *> challenges;
	for (StatusPage& page : challenge_pages_) {
		challenges.push_back(&page);
	}
	std::sort(challenges.begin(), challenges.end(),
		[](const StatusPage *left, const StatusPage *right) {
			return left->sequence < right->sequence;
		});
	pages.insert(pages.end(), challenges.begin(), challenges.end());
	if (progress_page_active_) {
		pages.push_back(&progress_page_);
	}
	std::vector<StatusPage *> trackers;
	for (StatusPage& page : tracker_pages_) {
		trackers.push_back(&page);
	}
	std::sort(trackers.begin(), trackers.end(),
		[](const StatusPage *left, const StatusPage *right) {
			return left->sequence < right->sequence;
		});
	pages.insert(pages.end(), trackers.begin(), trackers.end());
	return pages;
}

std::vector<const RaOverlay::StatusPage *> RaOverlay::OrderedStatusPages() const
{
	const std::vector<StatusPage *> mutable_pages =
		const_cast<RaOverlay *>(this)->OrderedStatusPages();
	return std::vector<const StatusPage *>(mutable_pages.begin(),
		mutable_pages.end());
}

void RaOverlay::SelectStatusPage(RaStatusPageType type, uint32_t id,
	uint32_t now_ms)
{
	selected_status_type_ = type;
	selected_status_id_ = id;
	status_rotation_remaining_ms_ = kStatusRotationMs;
	status_rotation_expires_at_ms_ = status_pages_paused_ ? 0 :
		now_ms + kStatusRotationMs;
}

void RaOverlay::NormalizeStatusSelection(uint32_t now_ms)
{
	const std::vector<StatusPage *> pages = OrderedStatusPages();
	if (pages.empty()) {
		selected_status_type_ = RaStatusPageType::None;
		selected_status_id_ = 0;
		status_rotation_remaining_ms_ = kStatusRotationMs;
		status_rotation_expires_at_ms_ = 0;
		return;
	}
	if (FindStatusPage(selected_status_type_, selected_status_id_) == nullptr) {
		SelectStatusPage(pages.front()->type, pages.front()->id, now_ms);
	}
}

void RaOverlay::AdvanceStatusPage(uint32_t now_ms)
{
	const std::vector<StatusPage *> pages = OrderedStatusPages();
	if (pages.empty()) {
		NormalizeStatusSelection(now_ms);
		return;
	}
	size_t current = 0;
	for (size_t index = 0; index < pages.size(); ++index) {
		if (pages[index]->type == selected_status_type_ &&
			pages[index]->id == selected_status_id_) {
			current = index;
			break;
		}
	}
	StatusPage *next = pages[(current + 1) % pages.size()];
	SelectStatusPage(next->type, next->id, now_ms);
}

void RaOverlay::ShowChallenge(uint32_t id, const std::string& title,
	const std::string& badge_url, uint32_t now_ms)
{
	StatusPage *page = FindStatusPage(RaStatusPageType::Challenge, id);
	if (page == nullptr) {
		StatusPage added;
		added.type = RaStatusPageType::Challenge;
		added.id = id;
		added.sequence = next_status_sequence_++;
		challenge_pages_.push_back(added);
		page = &challenge_pages_.back();
	}
	page->title = title;
	page->badge_url = badge_url;
	SelectStatusPage(page->type, page->id, now_ms);
}

void RaOverlay::HideChallenge(uint32_t id, uint32_t now_ms)
{
	const std::vector<StatusPage *> before = OrderedStatusPages();
	size_t selected_index = 0;
	for (size_t index = 0; index < before.size(); ++index) {
		if (before[index]->type == selected_status_type_ &&
			before[index]->id == selected_status_id_) {
			selected_index = index;
			break;
		}
	}
	const bool selected = selected_status_type_ == RaStatusPageType::Challenge &&
		selected_status_id_ == id;
	challenge_pages_.erase(std::remove_if(challenge_pages_.begin(),
		challenge_pages_.end(), [id](const StatusPage& page) {
			return page.id == id;
		}), challenge_pages_.end());
	if (selected) {
		const std::vector<StatusPage *> after = OrderedStatusPages();
		if (after.empty()) {
			selected_status_type_ = RaStatusPageType::None;
			NormalizeStatusSelection(now_ms);
		}
		else {
			StatusPage *next = after[selected_index % after.size()];
			SelectStatusPage(next->type, next->id, now_ms);
		}
	}
}

void RaOverlay::ShowProgress(uint32_t id, const std::string& title,
	const std::string& value, const std::string& badge_url, uint32_t now_ms)
{
	progress_page_active_ = true;
	progress_page_.type = RaStatusPageType::Progress;
	progress_page_.id = id;
	progress_page_.sequence = next_status_sequence_++;
	progress_page_.title = title;
	progress_page_.value = value;
	progress_page_.badge_url = badge_url;
	SelectStatusPage(RaStatusPageType::Progress, id, now_ms);
}

void RaOverlay::UpdateProgress(uint32_t id, const std::string& title,
	const std::string& value, const std::string& badge_url, uint32_t now_ms)
{
	if (!progress_page_active_) {
		ShowProgress(id, title, value, badge_url, now_ms);
		return;
	}
	const bool selected = selected_status_type_ == RaStatusPageType::Progress;
	progress_page_.id = id;
	progress_page_.title = title;
	progress_page_.value = value;
	progress_page_.badge_url = badge_url;
	if (selected) {
		selected_status_id_ = id;
	}
}

void RaOverlay::HideProgress(uint32_t now_ms)
{
	const std::vector<StatusPage *> before = OrderedStatusPages();
	size_t selected_index = 0;
	for (size_t index = 0; index < before.size(); ++index) {
		if (before[index]->type == selected_status_type_ &&
			before[index]->id == selected_status_id_) {
			selected_index = index;
			break;
		}
	}
	const bool selected = selected_status_type_ == RaStatusPageType::Progress;
	progress_page_ = StatusPage();
	progress_page_active_ = false;
	if (selected) {
		const std::vector<StatusPage *> after = OrderedStatusPages();
		if (after.empty()) {
			selected_status_type_ = RaStatusPageType::None;
			NormalizeStatusSelection(now_ms);
		}
		else {
			StatusPage *next = after[selected_index % after.size()];
			SelectStatusPage(next->type, next->id, now_ms);
		}
	}
}

void RaOverlay::ShowLeaderboardTracker(uint32_t id,
	const std::string& value, uint32_t now_ms)
{
	StatusPage *page = FindStatusPage(RaStatusPageType::LeaderboardTracker, id);
	if (page == nullptr) {
		StatusPage added;
		added.type = RaStatusPageType::LeaderboardTracker;
		added.id = id;
		added.sequence = next_status_sequence_++;
		tracker_pages_.push_back(added);
		page = &tracker_pages_.back();
	}
	page->value = value;
	SelectStatusPage(page->type, page->id, now_ms);
}

void RaOverlay::UpdateLeaderboardTracker(uint32_t id,
	const std::string& value, uint32_t now_ms)
{
	StatusPage *page = FindStatusPage(RaStatusPageType::LeaderboardTracker, id);
	if (page == nullptr) {
		ShowLeaderboardTracker(id, value, now_ms);
		return;
	}
	page->value = value;
}

void RaOverlay::HideLeaderboardTracker(uint32_t id, uint32_t now_ms)
{
	const std::vector<StatusPage *> before = OrderedStatusPages();
	size_t selected_index = 0;
	for (size_t index = 0; index < before.size(); ++index) {
		if (before[index]->type == selected_status_type_ &&
			before[index]->id == selected_status_id_) {
			selected_index = index;
			break;
		}
	}
	const bool selected =
		selected_status_type_ == RaStatusPageType::LeaderboardTracker &&
		selected_status_id_ == id;
	tracker_pages_.erase(std::remove_if(tracker_pages_.begin(),
		tracker_pages_.end(), [id](const StatusPage& page) {
			return page.id == id;
		}), tracker_pages_.end());
	if (selected) {
		const std::vector<StatusPage *> after = OrderedStatusPages();
		if (after.empty()) {
			selected_status_type_ = RaStatusPageType::None;
			NormalizeStatusSelection(now_ms);
		}
		else {
			StatusPage *next = after[selected_index % after.size()];
			SelectStatusPage(next->type, next->id, now_ms);
		}
	}
}

void RaOverlay::SetStatusPagesPaused(bool paused, uint32_t now_ms)
{
	if (paused == status_pages_paused_) {
		return;
	}
	if (paused) {
		const int32_t remaining = static_cast<int32_t>(
			status_rotation_expires_at_ms_ - now_ms);
		status_rotation_remaining_ms_ = remaining > 0 ?
			static_cast<uint32_t>(remaining) : kStatusRotationMs;
		status_rotation_expires_at_ms_ = 0;
		status_pages_paused_ = true;
	}
	else {
		status_pages_paused_ = false;
		status_rotation_expires_at_ms_ = now_ms +
			status_rotation_remaining_ms_;
	}
}

RaStatusPageSnapshot RaOverlay::VisibleStatusPage(uint32_t now_ms)
{
	NormalizeStatusSelection(now_ms);
	std::vector<StatusPage *> pages = OrderedStatusPages();
	if (pages.empty()) {
		return RaStatusPageSnapshot();
	}
	if (!status_pages_paused_ && pages.size() > 1 &&
		static_cast<int32_t>(now_ms - status_rotation_expires_at_ms_) >= 0) {
		AdvanceStatusPage(now_ms);
		pages = OrderedStatusPages();
	}
	for (size_t index = 0; index < pages.size(); ++index) {
		const StatusPage *page = pages[index];
		if (page->type == selected_status_type_ &&
			page->id == selected_status_id_) {
			RaStatusPageSnapshot snapshot;
			snapshot.type = page->type;
			snapshot.id = page->id;
			snapshot.index = index;
			snapshot.total = pages.size();
			snapshot.title = page->title;
			snapshot.value = page->value;
			snapshot.badge_url = page->badge_url;
			return snapshot;
		}
	}
	return RaStatusPageSnapshot();
}

bool RaOverlay::NextStatusPage(uint32_t now_ms)
{
	if (status_pages_paused_ || OrderedStatusPages().size() < 2) {
		return false;
	}
	AdvanceStatusPage(now_ms);
	return true;
}

size_t RaOverlay::StatusPageCount() const
{
	return challenge_pages_.size() + tracker_pages_.size() +
		(progress_page_active_ ? 1U : 0U);
}

size_t RaOverlay::NoticeQueueSize() const
{
	return notices_.size();
}

void RaOverlay::RebalanceNotices(uint32_t now_ms)
{
	if (notices_paused_) {
		return;
	}
	for (QueuedNotice& notice : notices_) {
		if (notice.active) {
			const int32_t remaining = static_cast<int32_t>(
				notice.expires_at_ms - now_ms);
			notice.remaining_ms = remaining > 0 ?
				static_cast<uint32_t>(remaining) : 0;
			notice.active = false;
		}
	}
	notices_.erase(std::remove_if(notices_.begin(), notices_.end(),
		[](const QueuedNotice& notice) { return notice.remaining_ms == 0; }),
		notices_.end());

	std::vector<QueuedNotice *> ranked;
	ranked.reserve(notices_.size());
	for (QueuedNotice& notice : notices_) {
		ranked.push_back(&notice);
	}
	std::sort(ranked.begin(), ranked.end(),
		[](const QueuedNotice *left, const QueuedNotice *right) {
			if (left->priority != right->priority) {
				return left->priority > right->priority;
			}
			return left->sequence < right->sequence;
		});
	const size_t active_count = std::min(kMaxVisibleNotices, ranked.size());
	for (size_t index = 0; index < active_count; ++index) {
		ranked[index]->active = true;
		ranked[index]->expires_at_ms = now_ms + ranked[index]->remaining_ms;
	}
}

void RaOverlay::SetSnapshot(const RaOverlaySnapshot& snapshot)
{
	snapshot_ = snapshot;
}

const RaOverlaySnapshot& RaOverlay::Snapshot() const
{
	return snapshot_;
}

void RaOverlay::OpenLibrary(const RaOverlayLibraryListSnapshot& snapshot)
{
	WipeLoginPassword();
	library_ = snapshot;
	library_.active = true;
	library_.selected_index = 0;
	library_.first_visible_index = 0;
	achievements_ = RaOverlayAchievementListSnapshot();
	leaderboards_ = RaOverlayLeaderboardListSnapshot();
	screen_ = RaOverlayScreen::Library;
	achievement_detail_scroll_ = 0;
	++achievement_selection_revision_;
	NormalizeListSelection();
	login_status_.clear();
	login_submit_pending_ = false;
}

void RaOverlay::OpenAchievements(
	const RaOverlayAchievementListSnapshot& snapshot)
{
	WipeLoginPassword();
	library_ = RaOverlayLibraryListSnapshot();
	achievements_ = snapshot;
	achievements_.active = true;
	achievements_.selected_index = 0;
	achievements_.first_visible_index = 0;
	leaderboards_ = RaOverlayLeaderboardListSnapshot();
	screen_ = RaOverlayScreen::Achievements;
	achievement_detail_scroll_ = 0;
	++achievement_selection_revision_;
	NormalizeListSelection();
	login_status_.clear();
	login_submit_pending_ = false;
}

void RaOverlay::OpenLeaderboards(
	const RaOverlayLeaderboardListSnapshot& snapshot)
{
	WipeLoginPassword();
	library_ = RaOverlayLibraryListSnapshot();
	leaderboards_ = snapshot;
	leaderboards_.active = true;
	leaderboards_.selected_index = 0;
	leaderboards_.first_visible_index = 0;
	achievements_ = RaOverlayAchievementListSnapshot();
	screen_ = RaOverlayScreen::Leaderboards;
	achievement_detail_scroll_ = 0;
	++achievement_selection_revision_;
	NormalizeListSelection();
	login_status_.clear();
	login_submit_pending_ = false;
}

void RaOverlay::UpdateLeaderboards(
	const RaOverlayLeaderboardListSnapshot& snapshot)
{
	if (screen_ != RaOverlayScreen::Leaderboards) {
		return;
	}

	const size_t selected_index = leaderboards_.selected_index;
	const size_t first_visible_index = leaderboards_.first_visible_index;
	leaderboards_ = snapshot;
	leaderboards_.active = true;
	leaderboards_.selected_index = selected_index;
	leaderboards_.first_visible_index = first_visible_index;
	NormalizeListSelection();
}

void RaOverlay::OpenLogin(const std::string& username)
{
	WipeLoginPassword();
	screen_ = RaOverlayScreen::Login;
	login_field_ = username.empty() ? RaOverlayLoginField::Username :
		RaOverlayLoginField::Password;
	login_focus_ = username.empty() ? RaOverlayLoginTarget::Username :
		RaOverlayLoginTarget::Password;
	login_username_ = username.substr(0, kMaxUsernameBytes);
	login_status_.clear();
	login_submit_pending_ = false;
}

bool RaOverlay::IsBlocking() const
{
	return screen_ != RaOverlayScreen::None;
}

RaOverlayScreen RaOverlay::Screen() const
{
	return screen_;
}

RaOverlayLibraryListSnapshot RaOverlay::LibraryListSnapshot() const
{
	RaOverlayLibraryListSnapshot snapshot = library_;
	snapshot.active = screen_ == RaOverlayScreen::Library;
	snapshot.selection_revision = achievement_selection_revision_;
	return snapshot;
}

RaOverlayAchievementListSnapshot RaOverlay::AchievementListSnapshot() const
{
	RaOverlayAchievementListSnapshot snapshot = achievements_;
	snapshot.active = screen_ == RaOverlayScreen::Achievements;
	snapshot.selection_revision = achievement_selection_revision_;
	return snapshot;
}

RaOverlayAchievementDetailSnapshot RaOverlay::AchievementDetailSnapshot() const
{
	RaOverlayAchievementDetailSnapshot snapshot;
	snapshot.active = screen_ == RaOverlayScreen::AchievementDetail;
	snapshot.selected_index = achievements_.selected_index;
	snapshot.item_count = achievements_.achievements.size();
	snapshot.scroll_offset = achievement_detail_scroll_;
	snapshot.selection_revision = achievement_selection_revision_;
	snapshot.game_title = achievements_.game_title;
	if (achievements_.selected_index < achievements_.achievements.size()) {
		snapshot.achievement =
			achievements_.achievements[achievements_.selected_index];
	}
	return snapshot;
}

RaOverlayLeaderboardListSnapshot RaOverlay::LeaderboardListSnapshot() const
{
	RaOverlayLeaderboardListSnapshot snapshot = leaderboards_;
	snapshot.active = screen_ == RaOverlayScreen::Leaderboards;
	snapshot.selection_revision = achievement_selection_revision_;
	return snapshot;
}

RaOverlayLoginSnapshot RaOverlay::LoginSnapshot() const
{
	RaOverlayLoginSnapshot snapshot;
	snapshot.active = screen_ == RaOverlayScreen::Login;
	snapshot.field = login_field_;
	snapshot.focus = login_focus_;
	snapshot.username = login_username_;
	snapshot.masked_password.assign(login_password_.size(), '*');
	snapshot.status_message = login_status_;
	snapshot.can_submit = CanSubmitLogin();
	return snapshot;
}

RaOverlayAction RaOverlay::OnTextInput(const char *text)
{
	if (screen_ != RaOverlayScreen::Login || login_submit_pending_ ||
		!IsLoginTextFocused()) {
		return RaOverlayAction::None;
	}
	AppendLoginText(text);
	return RaOverlayAction::None;
}

RaOverlayAction RaOverlay::OnControlKey(RaOverlayKey key)
{
	if (screen_ == RaOverlayScreen::GameDetail) {
		switch (key) {
		case RaOverlayKey::Escape:
			screen_ = RaOverlayScreen::Library;
			break;
		case RaOverlayKey::Enter:
			if (CanLaunchSelectedLibraryGame()) {
				return RaOverlayAction::OpenLibraryGame;
			}
			if (CanResolveSelectedLibraryConflict()) {
				return RaOverlayAction::ResolveLibraryConflict;
			}
			break;
		case RaOverlayKey::Up:
		case RaOverlayKey::Down:
		case RaOverlayKey::Left:
		case RaOverlayKey::Right:
		case RaOverlayKey::Tab:
		case RaOverlayKey::Backspace:
		case RaOverlayKey::PageUp:
		case RaOverlayKey::PageDown:
			break;
		}
		return RaOverlayAction::None;
	}

	if (screen_ == RaOverlayScreen::AchievementDetail) {
		switch (key) {
		case RaOverlayKey::Escape:
			CloseAchievementDetail();
			break;
		case RaOverlayKey::Backspace:
			break;
		case RaOverlayKey::Up:
		case RaOverlayKey::Left:
			MoveAchievementDetailScroll(-1);
			break;
		case RaOverlayKey::Down:
		case RaOverlayKey::Right:
		case RaOverlayKey::Tab:
			MoveAchievementDetailScroll(1);
			break;
		case RaOverlayKey::PageUp:
			MoveAchievementDetailScroll(-static_cast<int>(kMenuListVisibleRows));
			break;
		case RaOverlayKey::PageDown:
			MoveAchievementDetailScroll(static_cast<int>(kMenuListVisibleRows));
			break;
		case RaOverlayKey::Enter:
			break;
		}
		return RaOverlayAction::None;
	}

	if (screen_ == RaOverlayScreen::Library ||
		screen_ == RaOverlayScreen::Achievements ||
		screen_ == RaOverlayScreen::Leaderboards) {
		switch (key) {
		case RaOverlayKey::Escape:
			CloseScreen();
			return RaOverlayAction::Close;
		case RaOverlayKey::Up:
			MoveListSelection(-1);
			break;
		case RaOverlayKey::Down:
			MoveListSelection(1);
			break;
		case RaOverlayKey::Tab:
			MoveListSelection(1);
			break;
		case RaOverlayKey::Right:
			MoveListSelection(1);
			break;
		case RaOverlayKey::Left:
			MoveListSelection(-1);
			break;
		case RaOverlayKey::Enter:
			if (screen_ == RaOverlayScreen::Library &&
				ListItemCount() > 0) {
				screen_ = RaOverlayScreen::GameDetail;
			}
			else if (screen_ == RaOverlayScreen::Achievements) {
				OpenAchievementDetail();
			}
			break;
		case RaOverlayKey::Backspace:
			break;
		case RaOverlayKey::PageUp:
			MoveListSelection(-static_cast<int>(kMenuListVisibleRows));
			break;
		case RaOverlayKey::PageDown:
			MoveListSelection(static_cast<int>(kMenuListVisibleRows));
			break;
		}
		return RaOverlayAction::None;
	}

	if (screen_ != RaOverlayScreen::Login) {
		return RaOverlayAction::None;
	}
	if (login_submit_pending_) {
		return RaOverlayAction::None;
	}

	switch (key) {
	case RaOverlayKey::Tab:
	case RaOverlayKey::Down:
	case RaOverlayKey::Right:
		MoveLoginFocus(1);
		break;
	case RaOverlayKey::Up:
	case RaOverlayKey::Left:
		MoveLoginFocus(-1);
		break;
	case RaOverlayKey::Backspace:
		if (IsLoginTextFocused()) {
			BackspaceLoginField();
		}
		break;
	case RaOverlayKey::Enter:
		return ActivateLoginFocus();
	case RaOverlayKey::Escape:
		CloseScreen();
		return RaOverlayAction::Close;
	case RaOverlayKey::PageUp:
		MoveLoginFocus(-1);
		break;
	case RaOverlayKey::PageDown:
		MoveLoginFocus(1);
		break;
	}
	return RaOverlayAction::None;
}

RaOverlayAction RaOverlay::OnAchievementPointer(int x, int y, bool activate)
{
	return OnListPointer(x, y, activate);
}

RaOverlayAction RaOverlay::OnListPointer(int x, int y, bool activate)
{
	if (screen_ != RaOverlayScreen::Achievements) {
		if (screen_ != RaOverlayScreen::Library &&
			screen_ != RaOverlayScreen::Leaderboards) {
			return RaOverlayAction::None;
		}
	}

	size_t index = 0;
	if (ListTargetAt(x, y, &index)) {
		if (screen_ == RaOverlayScreen::Library) {
			library_.selected_index = index;
			if (activate) {
				NormalizeListSelection();
				screen_ = RaOverlayScreen::GameDetail;
				return RaOverlayAction::None;
			}
		}
		else if (screen_ == RaOverlayScreen::Achievements) {
			if (achievements_.selected_index != index) {
				achievements_.selected_index = index;
				achievement_detail_scroll_ = 0;
				++achievement_selection_revision_;
			}
			if (activate) {
				NormalizeListSelection();
				OpenAchievementDetail();
				return RaOverlayAction::None;
			}
		}
		else {
			leaderboards_.selected_index = index;
		}
		NormalizeListSelection();
		return RaOverlayAction::None;
	}
	if (ListItemCount() == 0 &&
		HitRect(x, y, kMenuListRowX, kMenuListRowY,
			kMenuListRowW, kMenuListRowH)) {
		return RaOverlayAction::None;
	}
	if (activate) {
		CloseScreen();
		return RaOverlayAction::Close;
	}
	return RaOverlayAction::None;
}

RaOverlayAction RaOverlay::OnListScroll(int delta)
{
	if (screen_ == RaOverlayScreen::GameDetail) {
		return RaOverlayAction::None;
	}
	if (screen_ == RaOverlayScreen::AchievementDetail) {
		MoveAchievementDetailScroll(delta);
		return RaOverlayAction::None;
	}
	if (screen_ != RaOverlayScreen::Achievements &&
		screen_ != RaOverlayScreen::Library &&
		screen_ != RaOverlayScreen::Leaderboards) {
		return RaOverlayAction::None;
	}
	MoveListSelection(delta);
	return RaOverlayAction::None;
}

RaOverlayAction RaOverlay::OnLoginTarget(RaOverlayLoginTarget target,
	bool activate)
{
	if (screen_ != RaOverlayScreen::Login) {
		return RaOverlayAction::None;
	}
	if (login_submit_pending_) {
		return RaOverlayAction::None;
	}

	login_focus_ = target;
	if (target == RaOverlayLoginTarget::Username) {
		login_field_ = RaOverlayLoginField::Username;
	}
	else if (target == RaOverlayLoginTarget::Password) {
		login_field_ = RaOverlayLoginField::Password;
	}

	return activate ? ActivateLoginFocus() : RaOverlayAction::None;
}

bool RaOverlay::LoginTargetAt(int x, int y, RaOverlayLoginTarget *target) const
{
	if (HitRect(x, y, kLoginUserBoxX, kLoginUserBoxY,
		kLoginUserBoxW, kLoginUserBoxH)) {
		if (target != nullptr) {
			*target = RaOverlayLoginTarget::Username;
		}
		return true;
	}
	if (HitRect(x, y, kLoginPassBoxX, kLoginPassBoxY,
		kLoginPassBoxW, kLoginPassBoxH)) {
		if (target != nullptr) {
			*target = RaOverlayLoginTarget::Password;
		}
		return true;
	}
	if (HitRect(x, y, kLoginButtonX, kLoginButtonY,
		kLoginButtonW, kLoginButtonH)) {
		if (target != nullptr) {
			*target = RaOverlayLoginTarget::Login;
		}
		return true;
	}
	if (HitRect(x, y, kCancelButtonX, kCancelButtonY,
		kCancelButtonW, kCancelButtonH)) {
		if (target != nullptr) {
			*target = RaOverlayLoginTarget::Cancel;
		}
		return true;
	}
	return false;
}

RaOverlayAction RaOverlay::OnLoginPointer(int x, int y, bool activate)
{
	RaOverlayLoginTarget target;
	if (!LoginTargetAt(x, y, &target)) {
		return RaOverlayAction::None;
	}
	return OnLoginTarget(target, activate);
}

bool RaOverlay::ConsumeSubmittedLogin(std::string *username,
	std::string *password)
{
	if (!login_submit_pending_ || username == nullptr || password == nullptr) {
		return false;
	}
	*username = login_username_;
	*password = login_password_;
	WipeLoginPassword();
	return true;
}

RaOverlayAction RaOverlay::SubmitLoginCredentials(const char *username,
	const char *password)
{
	if (screen_ != RaOverlayScreen::Login || login_submit_pending_) {
		return RaOverlayAction::None;
	}

	login_username_.clear();
	WipeLoginPassword();
	SetLoginFieldFocus(RaOverlayLoginField::Username);
	AppendLoginText(username);
	SetLoginFieldFocus(RaOverlayLoginField::Password);
	AppendLoginText(password);
	if (!CanSubmitLogin()) {
		login_status_ = "Enter username and password";
		return RaOverlayAction::None;
	}

	login_status_ = "Login pending";
	login_submit_pending_ = true;
	return RaOverlayAction::SubmitLogin;
}

void RaOverlay::SetLibraryStatusMessage(const std::string& message)
{
	library_.status_message = message;
}

void RaOverlay::SetLoginStatus(const std::string& message)
{
	login_status_ = message;
	login_submit_pending_ = false;
}

void RaOverlay::CloseScreen()
{
	WipeLoginPassword();
	screen_ = RaOverlayScreen::None;
	library_ = RaOverlayLibraryListSnapshot();
	achievements_ = RaOverlayAchievementListSnapshot();
	leaderboards_ = RaOverlayLeaderboardListSnapshot();
	achievement_detail_scroll_ = 0;
	login_field_ = RaOverlayLoginField::Username;
	login_focus_ = RaOverlayLoginTarget::Username;
	login_username_.clear();
	login_status_.clear();
	login_submit_pending_ = false;
}

void RaOverlay::MoveListSelection(int delta)
{
	const size_t count = ListItemCount();
	if (count == 0) {
		return;
	}

	size_t *selected = screen_ == RaOverlayScreen::Library ?
		&library_.selected_index : (screen_ == RaOverlayScreen::Achievements ?
			&achievements_.selected_index : &leaderboards_.selected_index);
	const size_t previous = *selected;
	const size_t last = count - 1;
	if (delta < 0) {
		const size_t amount = static_cast<size_t>(-delta);
		*selected = amount > *selected ? 0 : *selected - amount;
	}
	else {
		*selected += static_cast<size_t>(delta);
		if (*selected > last) {
			*selected = last;
		}
	}
	if ((screen_ == RaOverlayScreen::Library ||
		screen_ == RaOverlayScreen::Achievements) && *selected != previous) {
		achievement_detail_scroll_ = 0;
		++achievement_selection_revision_;
	}
	NormalizeListSelection();
}

void RaOverlay::OpenAchievementDetail()
{
	if (screen_ != RaOverlayScreen::Achievements ||
		achievements_.selected_index >= achievements_.achievements.size()) {
		return;
	}
	achievement_detail_scroll_ = 0;
	screen_ = RaOverlayScreen::AchievementDetail;
}

void RaOverlay::CloseAchievementDetail()
{
	if (screen_ == RaOverlayScreen::AchievementDetail) {
		screen_ = RaOverlayScreen::Achievements;
	}
}

void RaOverlay::MoveAchievementDetailScroll(int delta)
{
	if (screen_ != RaOverlayScreen::AchievementDetail) {
		return;
	}
	achievement_detail_scroll_ += delta;
	if (achievement_detail_scroll_ < 0) {
		achievement_detail_scroll_ = 0;
	}
}

bool RaOverlay::ListTargetAt(int x, int y, size_t *index) const
{
	if (screen_ != RaOverlayScreen::Library &&
		screen_ != RaOverlayScreen::Achievements &&
		screen_ != RaOverlayScreen::Leaderboards) {
		return false;
	}
	const size_t count = ListItemCount();
	const size_t first_visible = screen_ == RaOverlayScreen::Library ?
		library_.first_visible_index : (screen_ == RaOverlayScreen::Achievements ?
			achievements_.first_visible_index :
			leaderboards_.first_visible_index);
	if (count == 0) {
		return false;
	}

	for (size_t row = 0; row < kMenuListVisibleRows; ++row) {
		const size_t candidate = first_visible + row;
		if (candidate >= count) {
			break;
		}
		const int row_y = kMenuListRowY +
			static_cast<int>(row) * kMenuListRowPitch;
		if (HitRect(x, y, kMenuListRowX, row_y, kMenuListRowW,
			kMenuListRowH)) {
			if (index != nullptr) {
				*index = candidate;
			}
			return true;
		}
	}
	return false;
}

void RaOverlay::NormalizeListSelection()
{
	size_t *selected = screen_ == RaOverlayScreen::Leaderboards ?
		&leaderboards_.selected_index : (screen_ == RaOverlayScreen::Library ?
			&library_.selected_index : &achievements_.selected_index);
	size_t *first_visible = screen_ == RaOverlayScreen::Leaderboards ?
		&leaderboards_.first_visible_index : (screen_ == RaOverlayScreen::Library ?
			&library_.first_visible_index : &achievements_.first_visible_index);
	const size_t count = screen_ == RaOverlayScreen::Leaderboards ?
		leaderboards_.leaderboards.size() : (screen_ == RaOverlayScreen::Library ?
			library_.games.size() : achievements_.achievements.size());

	if (count == 0) {
		*selected = 0;
		*first_visible = 0;
		return;
	}

	const size_t last = count - 1;
	if (*selected > last) {
		*selected = last;
	}
	if (*first_visible > *selected) {
		*first_visible = *selected;
	}
	if (*selected >= *first_visible + kMenuListVisibleRows) {
		*first_visible = *selected - kMenuListVisibleRows + 1;
	}
	if (*first_visible > last) {
		*first_visible = last;
	}
}

size_t RaOverlay::ListItemCount() const
{
	if (screen_ == RaOverlayScreen::Library ||
		screen_ == RaOverlayScreen::GameDetail) {
		return library_.games.size();
	}
	if (screen_ == RaOverlayScreen::Leaderboards) {
		return leaderboards_.leaderboards.size();
	}
	return achievements_.achievements.size();
}

bool RaOverlay::SelectedLibraryGameId(int64_t *game_id) const
{
	if ((screen_ != RaOverlayScreen::Library &&
		screen_ != RaOverlayScreen::GameDetail) ||
		library_.selected_index >= library_.games.size() ||
		game_id == nullptr) {
		return false;
	}
	*game_id = library_.games[library_.selected_index].game_id;
	return true;
}

bool RaOverlay::CanLaunchSelectedLibraryGame() const
{
	return (screen_ == RaOverlayScreen::Library ||
		screen_ == RaOverlayScreen::GameDetail) &&
		library_.selected_index < library_.games.size() &&
		library_.games[library_.selected_index].identification_state ==
			kIdentificationIdentified &&
		library_.games[library_.selected_index].ra_game_id > 0;
}

bool RaOverlay::CanResolveSelectedLibraryConflict() const
{
	return (screen_ == RaOverlayScreen::Library ||
		screen_ == RaOverlayScreen::GameDetail) &&
		library_.selected_index < library_.games.size() &&
		library_.games[library_.selected_index].identification_state == 4 &&
		(library_.games[library_.selected_index].conflict_kind ==
			RaOverlayConflictKind::Merge ||
		 library_.games[library_.selected_index].conflict_kind ==
			RaOverlayConflictKind::Split);
}

bool RaOverlay::OpenSelectedLibraryGameDetail()
{
	if (screen_ != RaOverlayScreen::Library ||
		library_.selected_index >= library_.games.size()) {
		return false;
	}
	screen_ = RaOverlayScreen::GameDetail;
	return true;
}

void RaOverlay::SelectLibraryIndex(size_t index)
{
	if (index < library_.games.size() &&
		library_.selected_index != index) {
		library_.selected_index = index;
		++achievement_selection_revision_;
		NormalizeListSelection();
	}
}

void RaOverlay::SelectAchievementIndex(size_t index)
{
	if (index < achievements_.achievements.size() &&
		achievements_.selected_index != index) {
		achievements_.selected_index = index;
		achievement_detail_scroll_ = 0;
		++achievement_selection_revision_;
		NormalizeListSelection();
	}
}

void RaOverlay::SelectLeaderboardIndex(size_t index)
{
	if (index < leaderboards_.leaderboards.size() &&
		leaderboards_.selected_index != index) {
		leaderboards_.selected_index = index;
		++achievement_selection_revision_;
		NormalizeListSelection();
	}
}

void RaOverlay::SetListFirstVisibleIndex(size_t index)
{
	if (screen_ == RaOverlayScreen::Library) {
		library_.first_visible_index = index;
	}
	else if (screen_ == RaOverlayScreen::Achievements) {
		achievements_.first_visible_index = index;
	}
	else if (screen_ == RaOverlayScreen::Leaderboards) {
		leaderboards_.first_visible_index = index;
	}
	else {
		return;
	}
	NormalizeListSelection();
}

bool RaOverlay::OpenSelectedAchievementDetail()
{
	if (screen_ != RaOverlayScreen::Achievements ||
		achievements_.selected_index >= achievements_.achievements.size()) {
		return false;
	}
	OpenAchievementDetail();
	return screen_ == RaOverlayScreen::AchievementDetail;
}

void RaOverlay::ShowLibrary()
{
	if (!library_.games.empty() || library_.active) screen_ = RaOverlayScreen::Library;
}

void RaOverlay::ShowAchievements()
{
	if (!achievements_.achievements.empty() || achievements_.active) screen_ = RaOverlayScreen::Achievements;
}

void RaOverlay::ShowLeaderboards()
{
	if (!leaderboards_.leaderboards.empty() || leaderboards_.active) screen_ = RaOverlayScreen::Leaderboards;
}

void RaOverlay::ScrollAchievementDetail(int delta)
{
	MoveAchievementDetailScroll(delta);
}

void RaOverlay::SetAchievementDetailScroll(int offset)
{
	if (screen_ == RaOverlayScreen::AchievementDetail) {
		achievement_detail_scroll_ = offset < 0 ? 0 : offset;
	}
}

void RaOverlay::MoveLoginFocus(int delta)
{
	int focus = static_cast<int>(login_focus_);
	focus = (focus + delta) % 4;
	if (focus < 0) {
		focus += 4;
	}
	OnLoginTarget(static_cast<RaOverlayLoginTarget>(focus), false);
}

bool RaOverlay::IsLoginTextFocused() const
{
	return login_focus_ == RaOverlayLoginTarget::Username ||
		login_focus_ == RaOverlayLoginTarget::Password;
}

void RaOverlay::SetLoginFieldFocus(RaOverlayLoginField field)
{
	login_field_ = field;
	login_focus_ = field == RaOverlayLoginField::Username ?
		RaOverlayLoginTarget::Username : RaOverlayLoginTarget::Password;
}

void RaOverlay::WipeLoginPassword()
{
	std::fill(login_password_.begin(), login_password_.end(), '\0');
	login_password_.clear();
}

bool RaOverlay::AppendLoginText(const char *text)
{
	if (text == nullptr) {
		return false;
	}

	std::string *field = login_field_ == RaOverlayLoginField::Username ?
		&login_username_ : &login_password_;
	const size_t limit = login_field_ == RaOverlayLoginField::Username ?
		kMaxUsernameBytes : kMaxPasswordBytes;

	bool changed = false;
	for (const unsigned char *p =
		reinterpret_cast<const unsigned char *>(text); *p != '\0'; ++p) {
		if (!IsAllowedLoginByte(*p)) {
			continue;
		}
		if (field->size() >= limit) {
			break;
		}
		field->push_back(static_cast<char>(*p));
		changed = true;
	}
	if (changed) {
		login_status_.clear();
	}
	return changed;
}

bool RaOverlay::BackspaceLoginField()
{
	if (!IsLoginTextFocused()) {
		return false;
	}
	std::string *field = login_field_ == RaOverlayLoginField::Username ?
		&login_username_ : &login_password_;
	if (field->empty()) {
		return false;
	}
	field->pop_back();
	login_status_.clear();
	return true;
}

RaOverlayAction RaOverlay::ActivateLoginFocus()
{
	switch (login_focus_) {
	case RaOverlayLoginTarget::Username:
		if (!login_username_.empty()) {
			SetLoginFieldFocus(RaOverlayLoginField::Password);
		}
		else {
			login_status_ = "Enter username";
		}
		break;
	case RaOverlayLoginTarget::Password:
		if (CanSubmitLogin()) {
			login_status_ = "Login pending";
			login_submit_pending_ = true;
			return RaOverlayAction::SubmitLogin;
		}
		login_status_ = "Enter username and password";
		break;
	case RaOverlayLoginTarget::Login:
		if (CanSubmitLogin()) {
			login_status_ = "Login pending";
			login_submit_pending_ = true;
			return RaOverlayAction::SubmitLogin;
		}
		login_status_ = "Enter username and password";
		break;
	case RaOverlayLoginTarget::Cancel:
		CloseScreen();
		return RaOverlayAction::Close;
	}
	return RaOverlayAction::None;
}

bool RaOverlay::CanSubmitLogin() const
{
	return !login_username_.empty() && !login_password_.empty() &&
		!login_submit_pending_;
}

} // namespace Xm8Ra

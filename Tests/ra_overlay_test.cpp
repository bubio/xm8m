#include "ra_overlay.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>

int main()
{
	Xm8Ra::RaOverlay overlay;

	assert(!overlay.HasVisibleNotice(0));
	assert(overlay.VisibleNotice(0).empty());

	overlay.AddNotice("RA: test", 100, 50);
	assert(overlay.HasVisibleNotice(100));
	assert(overlay.VisibleNotice(120) == "RA: test");
	assert(!overlay.HasVisibleNotice(150));
	assert(overlay.VisibleNotice(150).empty());

	overlay.AddNotice("", 200, 50);
	assert(!overlay.HasVisibleNotice(200));

	overlay.AddNotice("normal-1", 300, 100,
		Xm8Ra::RaNoticePriority::Normal);
	overlay.AddNotice("low", 300, 100, Xm8Ra::RaNoticePriority::Low);
	overlay.AddNotice("normal-2", 300, 100,
		Xm8Ra::RaNoticePriority::Normal);
	overlay.AddNotice("queued-low", 300, 100,
		Xm8Ra::RaNoticePriority::Low);
	auto notices = overlay.VisibleNotices(300);
	assert(notices.size() == 1);
	assert(notices[0].text == "normal-1");
	assert(overlay.NoticeQueueSize() == 4);

	overlay.AddNotice("critical", 320, 20,
		Xm8Ra::RaNoticePriority::Critical);
	notices = overlay.VisibleNotices(320);
	assert(notices.size() == 1);
	assert(notices[0].text == "critical");
	notices = overlay.VisibleNotices(340);
	assert(notices.size() == 1);
	assert(notices[0].text == "normal-1");

	overlay.SetNoticesPaused(true, 350);
	assert(overlay.VisibleNotices(1000).empty());
	overlay.AddNotice("paused-important", 1000, 50,
		Xm8Ra::RaNoticePriority::Important);
	overlay.SetNoticesPaused(false, 2000);
	notices = overlay.VisibleNotices(2000);
	assert(notices.size() == 1);
	assert(notices[0].text == "paused-important");
	assert(overlay.HasVisibleNotice(2049));
	notices = overlay.VisibleNotices(2050);
	assert(notices.size() <= 1);

	overlay.AddNotice("stale auth status", 2100, 5000,
		Xm8Ra::RaNoticePriority::Important);
	overlay.ReplaceNotice("latest auth status", 2100, 5000,
		Xm8Ra::RaNoticePriority::Important);
	assert(overlay.NoticeQueueSize() == 1);
	assert(overlay.VisibleNotice(2100) == "latest auth status");

	overlay.Clear();
	overlay.AddNotice("wrap", 0xfffffff0U, 32,
		Xm8Ra::RaNoticePriority::Normal);
	assert(overlay.VisibleNotice(0xfffffff8U) == "wrap");
	assert(overlay.VisibleNotice(0x00000010U).empty());
	overlay.Clear();
	for (int index = 0; index < 70; ++index) {
		overlay.AddNotice("bounded", 100, 5000,
			Xm8Ra::RaNoticePriority::Low);
	}
	assert(overlay.NoticeQueueSize() == 64);

	overlay.ClearGameplayStatus();
	assert(overlay.StatusPageCount() == 0);
	overlay.SetLastSubmissionError("Achievement #42: rejected");
	overlay.AddNotice("discard on game stop", 900, 5000);
	overlay.ClearGameplayStatus();
	assert(overlay.NoticeQueueSize() == 0);
	assert(overlay.LastSubmissionError() == "Achievement #42: rejected");
	overlay.ClearLastSubmissionError();
	assert(overlay.LastSubmissionError().empty());
	overlay.ShowChallenge(10, "Challenge One", "challenge-1.png", 1000);
	auto page = overlay.VisibleStatusPage(1000);
	assert(page.type == Xm8Ra::RaStatusPageType::Challenge);
	assert(page.id == 10);
	assert(page.index == 0 && page.total == 1);
	assert(page.title == "Challenge One");
	assert(page.badge_url == "challenge-1.png");

	overlay.ShowChallenge(20, "Challenge Two", "challenge-2.png", 1100);
	page = overlay.VisibleStatusPage(1100);
	assert(page.id == 20);
	assert(page.index == 1 && page.total == 2);
	page = overlay.VisibleStatusPage(4100);
	assert(page.id == 10);
	assert(overlay.NextStatusPage(4200));
	assert(overlay.VisibleStatusPage(4200).id == 20);

	overlay.ShowProgress(30, "Measured", "3/10", "progress.png", 4300);
	page = overlay.VisibleStatusPage(4300);
	assert(page.type == Xm8Ra::RaStatusPageType::Progress);
	assert(page.id == 30);
	assert(page.index == 2 && page.total == 3);
	assert(page.value == "3/10");
	overlay.UpdateProgress(31, "Other Measured", "40%", "other.png", 4400);
	page = overlay.VisibleStatusPage(4400);
	assert(page.type == Xm8Ra::RaStatusPageType::Progress);
	assert(page.id == 31);
	assert(page.title == "Other Measured");
	assert(page.value == "40%");
	overlay.HideProgress(4500);
	page = overlay.VisibleStatusPage(4500);
	assert(page.type == Xm8Ra::RaStatusPageType::Challenge);
	assert(page.id == 10);

	overlay.ShowLeaderboardTracker(70, "00:10.00", 4600);
	page = overlay.VisibleStatusPage(4600);
	assert(page.type == Xm8Ra::RaStatusPageType::LeaderboardTracker);
	assert(page.id == 70);
	assert(page.value == "00:10.00");
	overlay.ShowLeaderboardTracker(80, "1000", 4700);
	assert(overlay.VisibleStatusPage(4700).id == 80);
	overlay.UpdateLeaderboardTracker(80, "1010", 4800);
	page = overlay.VisibleStatusPage(4800);
	assert(page.id == 80 && page.value == "1010");
	assert(overlay.NextStatusPage(4900));
	assert(overlay.VisibleStatusPage(4900).id == 10);
	overlay.HideChallenge(10, 5000);
	page = overlay.VisibleStatusPage(5000);
	assert(page.id == 20);

	overlay.SetStatusPagesPaused(true, 5100);
	page = overlay.VisibleStatusPage(9000);
	assert(page.id == 20);
	assert(!overlay.NextStatusPage(9000));
	overlay.SetStatusPagesPaused(false, 10000);
	assert(overlay.VisibleStatusPage(12899).id == 20);
	assert(overlay.VisibleStatusPage(12900).id == 70);

	// Updating a page changes its contents without restarting rotation.
	overlay.ClearStatusPages();
	overlay.ShowChallenge(90, "Timed", std::string(), 20000);
	overlay.ShowLeaderboardTracker(91, "1", 20000);
	overlay.UpdateLeaderboardTracker(91, "2", 22999);
	page = overlay.VisibleStatusPage(22999);
	assert(page.id == 91 && page.value == "2");
	assert(overlay.VisibleStatusPage(23000).id == 90);

	// A hidden Progress page is removed even while rotation is paused by UI.
	overlay.ShowProgress(92, "Temporary", "9/10", std::string(), 24000);
	overlay.SetStatusPagesPaused(true, 24100);
	overlay.HideProgress(25000);
	assert(overlay.StatusPageCount() == 2);
	overlay.SetStatusPagesPaused(false, 26000);
	page = overlay.VisibleStatusPage(26000);
	assert(page.type != Xm8Ra::RaStatusPageType::Progress);

	// Rotation uses 32-bit SDL ticks safely across wraparound.
	overlay.ClearStatusPages();
	overlay.ShowChallenge(93, "Wrap", std::string(), 0xfffffff0U);
	overlay.ShowLeaderboardTracker(94, "wrap", 0xfffffff0U);
	assert(overlay.VisibleStatusPage(0xfffffff0U).id == 94);
	assert(overlay.VisibleStatusPage(0x00000ba7U).id == 94);
	assert(overlay.VisibleStatusPage(0x00000ba8U).id == 93);

	overlay.ClearStatusPages();
	assert(overlay.StatusPageCount() == 0);
	assert(overlay.VisibleStatusPage(13001).type ==
		Xm8Ra::RaStatusPageType::None);
	overlay.AddNotice("badge notice", 14000, 100,
		Xm8Ra::RaNoticePriority::Critical, "badge.png");
	notices = overlay.VisibleNotices(14000);
	assert(notices.size() == 1);
	assert(notices[0].badge_url == "badge.png");
	overlay.ClearGameplayStatus();
	assert(overlay.NoticeQueueSize() == 0);

	Xm8Ra::RaOverlaySnapshot snapshot;
	snapshot.mode_enabled = true;
	snapshot.hardcore_enabled = true;
	snapshot.status_text = "RA: logged in";
	snapshot.user_name = "tester";
	snapshot.game_title = "Sample";
	snapshot.rich_presence = "Playing";
	overlay.SetSnapshot(snapshot);

	assert(overlay.Snapshot().mode_enabled);
	assert(overlay.Snapshot().hardcore_enabled);
	assert(overlay.Snapshot().status_text == "RA: logged in");
	assert(overlay.Snapshot().user_name == "tester");
	assert(overlay.Snapshot().game_title == "Sample");
	assert(overlay.Snapshot().rich_presence == "Playing");
	overlay.AddNotice("discard on stop", 100, 5000);
	assert(overlay.NoticeQueueSize() == 1);
	overlay.ClearNotices();
	assert(overlay.NoticeQueueSize() == 0);
	assert(overlay.Snapshot().mode_enabled);
	assert(overlay.Snapshot().game_title == "Sample");

	Xm8Ra::RaOverlayAchievementListSnapshot achievements;
	achievements.game_loaded = true;
	achievements.has_achievements = true;
	achievements.game_title = "Test Game";
	Xm8Ra::RaOverlayAchievementItem achievement;
	achievement.id = 42;
	achievement.points = 5;
	achievement.unlocked = 1;
	achievement.title = "First";
	achievement.description = "Description";
	achievement.bucket_label = "Unlocked";
	achievements.achievements.push_back(achievement);
	for (int i = 0; i < 8; ++i) {
		Xm8Ra::RaOverlayAchievementItem extra;
		extra.id = static_cast<uint32_t>(100 + i);
		extra.points = static_cast<uint32_t>(i + 1);
		extra.title = "Extra";
		extra.description = "Extra description";
		achievements.achievements.push_back(extra);
	}
	Xm8Ra::RaOverlayAchievementListSnapshot reordered_achievements =
		achievements;
	std::swap(reordered_achievements.achievements[1],
		reordered_achievements.achievements[7]);
	assert(Xm8Ra::FindRaAchievementItemIndex(reordered_achievements, 100) == 7);
	overlay.OpenAchievements(achievements);
	assert(overlay.IsBlocking());
	assert(overlay.Screen() == Xm8Ra::RaOverlayScreen::Achievements);
	assert(overlay.AchievementListSnapshot().active);
	assert(overlay.AchievementListSnapshot().game_title == "Test Game");
	assert(overlay.AchievementListSnapshot().achievements.size() == 9);
	assert(overlay.AchievementListSnapshot().achievements[0].title == "First");
	assert(overlay.AchievementListSnapshot().selected_index == 0);
	const uint32_t first_revision =
		overlay.AchievementListSnapshot().selection_revision;
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Down) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.AchievementListSnapshot().selected_index == 1);
	assert(overlay.AchievementListSnapshot().selection_revision !=
		first_revision);
	for (int i = 0; i < 7; ++i) {
		overlay.OnControlKey(Xm8Ra::RaOverlayKey::Down);
	}
	assert(overlay.AchievementListSnapshot().selected_index == 8);
	assert(overlay.AchievementListSnapshot().first_visible_index == 2);
	assert(overlay.OnAchievementPointer(90, 86, false) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.AchievementListSnapshot().selected_index == 2);
	assert(overlay.OnListScroll(3) == Xm8Ra::RaOverlayAction::None);
	assert(overlay.AchievementListSnapshot().selected_index == 5);
	assert(overlay.OnListScroll(-2) == Xm8Ra::RaOverlayAction::None);
	assert(overlay.AchievementListSnapshot().selected_index == 3);
	assert(overlay.OnAchievementPointer(10, 10, false) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.IsBlocking());
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Enter) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.IsBlocking());
	assert(overlay.Screen() ==
		Xm8Ra::RaOverlayScreen::AchievementDetail);
	assert(overlay.AchievementDetailSnapshot().active);
	assert(overlay.AchievementDetailSnapshot().selected_index == 3);
	assert(overlay.AchievementDetailSnapshot().item_count == 9);
	assert(overlay.AchievementDetailSnapshot().scroll_offset == 0);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Up) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.AchievementDetailSnapshot().scroll_offset == 0);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Down) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.AchievementDetailSnapshot().scroll_offset == 1);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::PageDown) ==
		Xm8Ra::RaOverlayAction::None);
	const size_t page_down_offset =
		overlay.AchievementDetailSnapshot().scroll_offset;
	assert(page_down_offset > 1);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::PageUp) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.AchievementDetailSnapshot().scroll_offset <
		page_down_offset);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Escape) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.Screen() == Xm8Ra::RaOverlayScreen::Achievements);
	assert(overlay.OnAchievementPointer(10, 10, true) ==
		Xm8Ra::RaOverlayAction::Close);
	assert(!overlay.IsBlocking());

	overlay.OpenAchievements(achievements);
	assert(overlay.OnAchievementPointer(90, 86, true) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.Screen() ==
		Xm8Ra::RaOverlayScreen::AchievementDetail);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Escape) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.Screen() == Xm8Ra::RaOverlayScreen::Achievements);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Escape) ==
		Xm8Ra::RaOverlayAction::Close);
	assert(!overlay.IsBlocking());

	Xm8Ra::RaOverlayAchievementListSnapshot status_only;
	status_only.status_message = "No RA game loaded";
	overlay.OpenAchievements(status_only);
	assert(overlay.Screen() == Xm8Ra::RaOverlayScreen::Achievements);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Down) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.AchievementListSnapshot().selected_index == 0);
	assert(overlay.OnAchievementPointer(90, 86, true) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.IsBlocking());
	assert(overlay.Screen() == Xm8Ra::RaOverlayScreen::Achievements);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Enter) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.Screen() == Xm8Ra::RaOverlayScreen::Achievements);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Escape) ==
		Xm8Ra::RaOverlayAction::Close);

	Xm8Ra::RaOverlayLibraryListSnapshot empty_library;
	empty_library.status_message = "No RetroAchievements games in library";
	overlay.OpenLibrary(empty_library);
	assert(overlay.Screen() == Xm8Ra::RaOverlayScreen::Library);
	assert(overlay.LibraryListSnapshot().active);
	assert(overlay.LibraryListSnapshot().status_message ==
		"No RetroAchievements games in library");
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Enter) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.OnListPointer(90, 86, true) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.IsBlocking());
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Escape) ==
		Xm8Ra::RaOverlayAction::Close);

	Xm8Ra::RaOverlayLibraryListSnapshot library;
	for (int i = 0; i < 9; ++i) {
		Xm8Ra::RaOverlayLibraryItem game;
		game.game_id = 100 + i;
		game.ra_game_id = 1234 + i;
		game.identification_state = 1;
		game.title = i == 0 ? "Library First" : "Library Extra";
		game.media_count = i + 1;
		game.health_state = 0;
		game.has_progress = i == 0;
		game.core_total = 10;
		game.core_unlocked = 4;
		library.games.push_back(game);
	}
	library.games[8].game_id = INT64_C(0x100000008);
	Xm8Ra::RaOverlayLibraryListSnapshot reordered_library = library;
	std::swap(reordered_library.games[2], reordered_library.games[8]);
	assert(Xm8Ra::FindRaLibraryItemIndex(reordered_library,
		INT64_C(0x100000008)) == 2);
	overlay.OpenLibrary(library);
	assert(overlay.Screen() == Xm8Ra::RaOverlayScreen::Library);
	assert(overlay.LibraryListSnapshot().games.size() == 9);
	assert(overlay.LibraryListSnapshot().selected_index == 0);
	overlay.SetLibraryStatusMessage("START failed: test failure");
	assert(overlay.LibraryListSnapshot().status_message ==
		"START failed: test failure");
	int64_t game_id = 0;
	assert(overlay.SelectedLibraryGameId(&game_id));
	assert(game_id == 100);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Down) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.LibraryListSnapshot().selected_index == 1);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::PageDown) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.LibraryListSnapshot().selected_index == 8);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::PageUp) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.LibraryListSnapshot().selected_index == 1);
	assert(overlay.OnListScroll(7) == Xm8Ra::RaOverlayAction::None);
	assert(overlay.LibraryListSnapshot().selected_index == 8);
	assert(overlay.LibraryListSnapshot().first_visible_index == 2);
	size_t list_target = 0;
	assert(overlay.ListTargetAt(90, 86, &list_target));
	assert(list_target == 2);
	assert(overlay.OnListPointer(90, 86, false) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.LibraryListSnapshot().selected_index == 2);
	assert(overlay.SelectedLibraryGameId(&game_id));
	assert(game_id == 102);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Enter) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.Screen() == Xm8Ra::RaOverlayScreen::GameDetail);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Escape) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.Screen() == Xm8Ra::RaOverlayScreen::Library);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Right) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.Screen() == Xm8Ra::RaOverlayScreen::Library);
	assert(overlay.OpenSelectedLibraryGameDetail());
	assert(overlay.Screen() == Xm8Ra::RaOverlayScreen::GameDetail);
	assert(overlay.SelectedLibraryGameId(&game_id));
	assert(game_id == 103);
	assert(overlay.OnListScroll(1) == Xm8Ra::RaOverlayAction::None);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Enter) ==
		Xm8Ra::RaOverlayAction::OpenLibraryGame);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Left) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.Screen() == Xm8Ra::RaOverlayScreen::GameDetail);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Escape) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.Screen() == Xm8Ra::RaOverlayScreen::Library);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Right) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.Screen() == Xm8Ra::RaOverlayScreen::Library);
	assert(overlay.OnListPointer(90, 86, true) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.Screen() == Xm8Ra::RaOverlayScreen::GameDetail);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Escape) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.Screen() == Xm8Ra::RaOverlayScreen::Library);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Backspace) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.Screen() == Xm8Ra::RaOverlayScreen::Library);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Escape) ==
		Xm8Ra::RaOverlayAction::Close);

	Xm8Ra::RaOverlayLibraryListSnapshot conflicts;
	Xm8Ra::RaOverlayLibraryItem conflict;
	conflict.game_id = 999;
	conflict.title = "Conflicting Game";
	conflict.identification_state = 4;
	conflict.conflict_kind = Xm8Ra::RaOverlayConflictKind::Merge;
	conflicts.games.push_back(conflict);
	overlay.OpenLibrary(conflicts);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Enter) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.Screen() == Xm8Ra::RaOverlayScreen::GameDetail);
	assert(!overlay.CanLaunchSelectedLibraryGame());
	assert(overlay.CanResolveSelectedLibraryConflict());
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Enter) ==
		Xm8Ra::RaOverlayAction::ResolveLibraryConflict);
	assert(overlay.Screen() == Xm8Ra::RaOverlayScreen::GameDetail);
	conflict.conflict_kind = Xm8Ra::RaOverlayConflictKind::Split;
	conflicts.games[0] = conflict;
	overlay.OpenLibrary(conflicts);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Enter) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.CanResolveSelectedLibraryConflict());
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Enter) ==
		Xm8Ra::RaOverlayAction::ResolveLibraryConflict);
	conflict.conflict_kind = Xm8Ra::RaOverlayConflictKind::Manual;
	conflicts.games[0] = conflict;
	overlay.OpenLibrary(conflicts);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Enter) ==
		Xm8Ra::RaOverlayAction::None);
	assert(!overlay.CanResolveSelectedLibraryConflict());
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Enter) ==
		Xm8Ra::RaOverlayAction::None);

	Xm8Ra::RaOverlayLeaderboardListSnapshot leaderboards;
	leaderboards.game_loaded = true;
	leaderboards.game_title = "Test Game";
	for (int i = 0; i < 8; ++i) {
		Xm8Ra::RaOverlayLeaderboardItem leaderboard;
		leaderboard.id = static_cast<uint32_t>(200 + i);
		leaderboard.title = i == 0 ? "Fastest Clear" : "Extra Board";
		leaderboard.description = "Finish quickly";
		leaderboard.lower_is_better = i == 0;
		leaderboard.bucket_label = i == 0 ? "Active" : "Inactive";
		leaderboards.leaderboards.push_back(leaderboard);
	}
	Xm8Ra::RaOverlayLeaderboardListSnapshot reordered_leaderboards =
		leaderboards;
	std::swap(reordered_leaderboards.leaderboards[1],
		reordered_leaderboards.leaderboards[6]);
	assert(Xm8Ra::FindRaLeaderboardItemIndex(reordered_leaderboards, 201) == 6);
	overlay.OpenLeaderboards(leaderboards);
	assert(overlay.Screen() == Xm8Ra::RaOverlayScreen::Leaderboards);
	assert(overlay.LeaderboardListSnapshot().active);
	assert(overlay.LeaderboardListSnapshot().leaderboards.size() == 8);
	assert(overlay.LeaderboardListSnapshot().leaderboards[0].title ==
		"Fastest Clear");
	assert(overlay.OnListPointer(90, 86, true) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.IsBlocking());
	assert(overlay.OnListScroll(1) == Xm8Ra::RaOverlayAction::None);
	assert(overlay.LeaderboardListSnapshot().selected_index == 1);
	assert(overlay.OnListScroll(7) == Xm8Ra::RaOverlayAction::None);
	assert(overlay.LeaderboardListSnapshot().selected_index == 7);
	assert(overlay.LeaderboardListSnapshot().first_visible_index == 1);
	Xm8Ra::RaOverlayLeaderboardListSnapshot updated_leaderboards =
		leaderboards;
	updated_leaderboards.leaderboards[7].has_scoreboard = true;
	updated_leaderboards.leaderboards[7].new_rank = 2;
	updated_leaderboards.leaderboards[7].num_entries = 10;
	updated_leaderboards.leaderboards[7].submitted_score = "900";
	updated_leaderboards.leaderboards[7].best_score = "1000";
	Xm8Ra::RaOverlayLeaderboardItem::ScoreboardEntry entry;
	entry.rank = 1;
	entry.username = "first";
	entry.score = "1000";
	updated_leaderboards.leaderboards[7].top_entries.push_back(entry);
	updated_leaderboards.leaderboards[7].has_entries = true;
	updated_leaderboards.leaderboards[7].entry_total = 10;
	updated_leaderboards.leaderboards[7].entries.push_back(entry);
	overlay.UpdateLeaderboards(updated_leaderboards);
	assert(overlay.LeaderboardListSnapshot().selected_index == 7);
	assert(overlay.LeaderboardListSnapshot().leaderboards[7].has_scoreboard);
	assert(overlay.LeaderboardListSnapshot().leaderboards[7].has_entries);
	assert(overlay.LeaderboardListSnapshot()
		.leaderboards[7].top_entries[0].username == "first");
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Escape) ==
		Xm8Ra::RaOverlayAction::Close);
	assert(!overlay.IsBlocking());

	Xm8Ra::RaOverlayLeaderboardListSnapshot empty_leaderboards;
	empty_leaderboards.game_loaded = true;
	empty_leaderboards.game_title = "Test Game";
	empty_leaderboards.status_message = "No leaderboards";
	overlay.OpenLeaderboards(empty_leaderboards);
	assert(overlay.Screen() == Xm8Ra::RaOverlayScreen::Leaderboards);
	assert(overlay.OnListPointer(90, 86, true) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.IsBlocking());
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Escape) ==
		Xm8Ra::RaOverlayAction::Close);

	overlay.OpenLogin();
	assert(overlay.IsBlocking());
	assert(overlay.Screen() == Xm8Ra::RaOverlayScreen::Login);
	assert(overlay.LoginSnapshot().field ==
		Xm8Ra::RaOverlayLoginField::Username);
	assert(!overlay.LoginSnapshot().can_submit);

	overlay.OnTextInput("player");
	assert(overlay.LoginSnapshot().username == "player");
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Enter) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.LoginSnapshot().field ==
		Xm8Ra::RaOverlayLoginField::Password);
	overlay.OnTextInput("secret");
	assert(overlay.LoginSnapshot().masked_password == "******");
	assert(overlay.LoginSnapshot().can_submit);

	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Backspace) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.LoginSnapshot().masked_password == "*****");
	overlay.OnTextInput("t");
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Enter) ==
		Xm8Ra::RaOverlayAction::SubmitLogin);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Enter) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Escape) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.OnLoginTarget(Xm8Ra::RaOverlayLoginTarget::Cancel,
		true) == Xm8Ra::RaOverlayAction::None);
	assert(overlay.IsBlocking());

	std::string username;
	std::string password;
	assert(overlay.ConsumeSubmittedLogin(&username, &password));
	assert(username == "player");
	assert(password == "secret");
	assert(overlay.LoginSnapshot().masked_password.empty());
	assert(overlay.LoginSnapshot().username == "player");
	overlay.SetLoginStatus("Invalid username or password");
	assert(overlay.LoginSnapshot().status_message ==
		"Invalid username or password");
	assert(overlay.LoginSnapshot().username == "player");
	assert(overlay.LoginSnapshot().masked_password.empty());

	overlay.OpenLogin("saved-user");
	assert(overlay.LoginSnapshot().username == "saved-user");
	assert(overlay.LoginSnapshot().field ==
		Xm8Ra::RaOverlayLoginField::Password);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Escape) ==
		Xm8Ra::RaOverlayAction::Close);
	assert(!overlay.IsBlocking());

	overlay.OpenLogin();
	assert(overlay.OnLoginPointer(240, 136, true) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.LoginSnapshot().focus ==
		Xm8Ra::RaOverlayLoginTarget::Username);
	overlay.OnTextInput("paduser");
	assert(overlay.OnLoginPointer(240, 176, true) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.LoginSnapshot().focus ==
		Xm8Ra::RaOverlayLoginTarget::Password);
	overlay.OnTextInput("padpass");
	Xm8Ra::RaOverlayLoginTarget target =
		Xm8Ra::RaOverlayLoginTarget::Username;
	assert(overlay.LoginTargetAt(260, 224, &target));
	assert(target == Xm8Ra::RaOverlayLoginTarget::Login);
	assert(overlay.LoginTargetAt(390, 224, &target));
	assert(target == Xm8Ra::RaOverlayLoginTarget::Cancel);
	assert(!overlay.LoginTargetAt(10, 10, &target));
	assert(overlay.OnLoginPointer(260, 224, false) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.LoginSnapshot().focus ==
		Xm8Ra::RaOverlayLoginTarget::Login);
	assert(overlay.OnLoginPointer(260, 224, true) ==
		Xm8Ra::RaOverlayAction::SubmitLogin);
	assert(overlay.ConsumeSubmittedLogin(&username, &password));
	assert(username == "paduser");
	assert(password == "padpass");
	overlay.SetLoginStatus("retry");

	overlay.OpenLogin();
	overlay.OnControlKey(Xm8Ra::RaOverlayKey::PageDown);
	assert(overlay.LoginSnapshot().focus ==
		Xm8Ra::RaOverlayLoginTarget::Password);
	overlay.OnControlKey(Xm8Ra::RaOverlayKey::PageUp);
	assert(overlay.LoginSnapshot().focus ==
		Xm8Ra::RaOverlayLoginTarget::Username);
	overlay.OnControlKey(Xm8Ra::RaOverlayKey::Down);
	assert(overlay.LoginSnapshot().focus ==
		Xm8Ra::RaOverlayLoginTarget::Password);
	overlay.OnControlKey(Xm8Ra::RaOverlayKey::Down);
	assert(overlay.LoginSnapshot().focus ==
		Xm8Ra::RaOverlayLoginTarget::Login);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Enter) ==
		Xm8Ra::RaOverlayAction::None);
	assert(overlay.LoginSnapshot().status_message ==
		"Enter username and password");
	overlay.OnControlKey(Xm8Ra::RaOverlayKey::Right);
	assert(overlay.LoginSnapshot().focus ==
		Xm8Ra::RaOverlayLoginTarget::Cancel);
	assert(overlay.OnControlKey(Xm8Ra::RaOverlayKey::Enter) ==
		Xm8Ra::RaOverlayAction::Close);
	assert(!overlay.IsBlocking());

	overlay.Clear();
	assert(!overlay.HasVisibleNotice(201));
	assert(overlay.LastSubmissionError().empty());
	assert(!overlay.Snapshot().mode_enabled);
	assert(overlay.Snapshot().status_text.empty());
	return 0;
}

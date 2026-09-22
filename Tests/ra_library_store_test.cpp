#include "Fixtures/d88_fixture.h"
#include "ra_file_util.h"
#include "ra_library.h"
#include "ra_media_store.h"
#include "ra_paths.h"
#include "sqlite3.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
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

std::string JoinPath(const std::string& dir, const char *name)
{
	if (!dir.empty() && dir.back() == '/') {
		return dir + name;
	}
	return dir + "/" + name;
}

bool PathExists(const std::string& path)
{
	return Xm8Ra::RaPathExists(path);
}

bool MakeDirectoryTree(const std::string& path, std::string *error)
{
	return Xm8Ra::EnsureRaDirectoryTree(path, error);
}

void RemoveTree(const std::string& path)
{
	Xm8Ra::RemoveRaTree(path);
}

std::vector<char> ReadFile(const std::string& path)
{
	std::ifstream stream(path, std::ios::binary);
	return std::vector<char>(std::istreambuf_iterator<char>(stream),
		std::istreambuf_iterator<char>());
}

bool AppendByte(const std::string& path, char value)
{
	std::ofstream stream(path, std::ios::binary | std::ios::app);
	if (!stream.is_open()) {
		return false;
	}
	stream.put(value);
	return stream.good();
}

bool WriteTextFile(const std::string& path, const char *text)
{
	std::ofstream stream(path, std::ios::binary | std::ios::trunc);
	if (!stream.is_open()) {
		return false;
	}
	stream << text;
	return stream.good();
}

bool CopyFileBytes(const std::string& source, const std::string& destination)
{
	std::ifstream input(source, std::ios::binary);
	std::ofstream output(destination, std::ios::binary | std::ios::trunc);
	if (!input.is_open() || !output.is_open()) {
		return false;
	}
	output << input.rdbuf();
	return input.good() && output.good();
}

bool ExecSql(const std::string& database_path, const char *sql,
	std::string *error)
{
	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(database_path.c_str(), &db,
		SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
		if (error != nullptr) {
			*error = db != nullptr ? sqlite3_errmsg(db) : "open sqlite failed";
		}
		if (db != nullptr) {
			sqlite3_close(db);
		}
		return false;
	}
	char *message = nullptr;
	const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &message);
	if (rc != SQLITE_OK) {
		if (error != nullptr) {
			*error = message != nullptr ? message : sqlite3_errmsg(db);
		}
		sqlite3_free(message);
		sqlite3_close(db);
		return false;
	}
	sqlite3_close(db);
	return true;
}

int QueryInt(const std::string& database_path, const char *sql)
{
	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(database_path.c_str(), &db, SQLITE_OPEN_READONLY,
		nullptr) != SQLITE_OK) {
		if (db != nullptr) {
			sqlite3_close(db);
		}
		return -1;
	}
	sqlite3_stmt *stmt = nullptr;
	int value = -1;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK &&
		sqlite3_step(stmt) == SQLITE_ROW) {
		value = sqlite3_column_int(stmt, 0);
	}
	if (stmt != nullptr) {
		sqlite3_finalize(stmt);
	}
	sqlite3_close(db);
	return value;
}

std::string QueryText(const std::string& database_path, const char *sql)
{
	sqlite3 *db = nullptr;
	if (sqlite3_open_v2(database_path.c_str(), &db, SQLITE_OPEN_READONLY,
		nullptr) != SQLITE_OK) {
		if (db != nullptr) {
			sqlite3_close(db);
		}
		return "";
	}
	sqlite3_stmt *stmt = nullptr;
	std::string value;
	if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK &&
		sqlite3_step(stmt) == SQLITE_ROW) {
		const unsigned char *text = sqlite3_column_text(stmt, 0);
		if (text != nullptr) {
			value = reinterpret_cast<const char *>(text);
		}
	}
	if (stmt != nullptr) {
		sqlite3_finalize(stmt);
	}
	sqlite3_close(db);
	return value;
}

bool DirectoryHasPrefix(const std::string& path, const char *prefix)
{
	std::vector<Xm8Ra::RaDirectoryEntry> entries;
	if (!Xm8Ra::ListRaDirectoryNoFollow(path, &entries)) {
		return false;
	}
	const std::string prefix_string = prefix;
	for (const Xm8Ra::RaDirectoryEntry& entry : entries) {
		const size_t slash = entry.path.find_last_of("/\\");
		const std::string name = slash == std::string::npos ?
			entry.path : entry.path.substr(slash + 1);
		if (name.compare(0, prefix_string.size(), prefix_string) == 0) {
			return true;
		}
	}
	return false;
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
	) + "/xm8-ra-library-store-" + std::to_string(unique);
	const std::string source_dir = JoinPath(base, "source");
	const std::string ra_root = JoinPath(base, "ra");

	std::string error;
	Check(MakeDirectoryTree(base, &error), "create test root");
	if (!error.empty()) {
		std::cerr << error << '\n';
	}
	Check(Xm8Ra::RootFromSettingDir((base + "/").c_str()) == ra_root,
		"RA root is under setting directory");
	Check(Xm8Ra::RootFromSettingDir("") == "",
		"empty setting directory has no RA root");
	Check(D88Fixture::GenerateStandardSet(source_dir, &error),
		"generate fixture set");
	if (!error.empty()) {
		std::cerr << error << '\n';
	}

	Xm8Ra::RaLibrary library;
	Check(library.Open(ra_root, &error), "open RA library");
	if (!error.empty()) {
		std::cerr << error << '\n';
	}
	Check(PathExists(JoinPath(ra_root, "library.sqlite3")),
		"library DB exists");
	Xm8Ra::RaPendingUnlockRecord pending;
	pending.account = "outbox-user";
	pending.achievement_id = 321;
	pending.hardcore = true;
	pending.game_hash = "0123456789abcdef0123456789abcdef";
	pending.unlocked_at = 1700000000;
	int64_t pending_id = 0;
	Check(library.EnqueuePendingUnlock(pending, &pending_id, &error) &&
		pending_id > 0, "enqueue pending unlock before network submission");
	int64_t duplicate_id = 0;
	pending.unlocked_at += 60;
	Check(library.EnqueuePendingUnlock(pending, &duplicate_id, &error) &&
		duplicate_id == pending_id, "deduplicate pending unlock identity");
	size_t pending_count = 0;
	Check(library.CountPendingUnlocks(pending.account, &pending_count, &error) &&
		pending_count == 1, "pending unlock unique key enforced");
	std::vector<Xm8Ra::RaPendingUnlockRecord> pending_records;
	Check(library.ListPendingUnlocks(pending.account, &pending_records, &error) &&
		pending_records.size() == 1 &&
		pending_records[0].unlocked_at == 1700000000,
		"pending unlock keeps earliest unlock time");
	Check(library.MarkPendingUnlockAttempt(pending_id,
		Xm8Ra::RaPendingUnlockStatus::Pending, "transport", &error),
		"record pending unlock retry");

	Xm8Ra::RaSettings settings;
	Check(library.LoadSettings(&settings, &error), "load default RA settings");
	Check(!settings.enabled, "RA settings default disabled");
	Check(settings.last_mode == Xm8Ra::kRaModeHardcore,
		"RA settings default hardcore");
	settings.enabled = true;
	settings.last_mode = Xm8Ra::kRaModeSoftcore;
	settings.unofficial_enabled = true;
	settings.notification_seconds = 8;
	settings.image_cache_limit_mib = 256;
	Check(library.SaveSettings(settings, &error), "save RA settings");

	Xm8Ra::RaMediaStore store(&library);
	Xm8Ra::ImportedMedia first;
	const std::string single = JoinPath(source_dir, "single.d88");
	const auto original_single = ReadFile(single);
	Check(store.ImportDesktopD88(single, &first, &error), "import single D88");
	if (!error.empty()) {
		std::cerr << error << '\n';
	}
	Check(first.record.md5 == "5c50ca4f9e3a7afbe4d6666e8974949d",
		"single md5 registered");
	Check(first.record.game_id > 0, "game id assigned");
	Check(first.copied, "first import created working copy");
	Check(ReadFile(first.working_path) == original_single,
		"working copy equals original at creation");
	Check(ReadFile(single) == original_single, "original remains unchanged");
	Check(first.working_path.find("/ra/media/") != std::string::npos,
		"working copy is under RA media root");

	std::vector<Xm8Ra::RaLibraryGameListItem> games;
	Check(library.ListGames(&games, &error), "list games after first import");
	Check(games.empty(), "unidentified import is hidden from RA library");
	Check(library.MarkGameIdentified(first.record.game_id, 1234,
		"RA Single Game", "https://media.example/badge.png", &error),
		"mark first game identified");
	Check(library.ListGames(&games, &error),
		"list games after RA identification");
	Check(games.size() == 1, "library list has identified game");
	if (!games.empty()) {
		Check(games[0].game_id == first.record.game_id,
			"library list game id");
		Check(games[0].ra_game_id == 1234, "library list RA game id");
		Check(games[0].title == "RA Single Game",
			"library list uses RA title");
		Check(games[0].badge_url == "https://media.example/badge.png",
			"library list badge URL");
		Check(games[0].media_count == 1,
			"library list media count");
		Check(games[0].health_state == Xm8Ra::kRaMediaHealthOk,
			"library list health");
		Check(!games[0].has_progress,
			"library list progress initially absent");
	}

	Xm8Ra::ImportedMedia duplicate;
	Check(store.ImportDesktopD88(single, &duplicate, &error),
		"duplicate import");
	Check(duplicate.record.md5 == first.record.md5,
		"duplicate md5 reused");
	Check(duplicate.record.game_id == first.record.game_id,
		"duplicate game reused");
	Check(!duplicate.copied, "duplicate did not overwrite working copy");
	Check(ReadFile(duplicate.working_path) == original_single,
		"duplicate working copy unchanged");

	Xm8Ra::MediaHealthStatus health;
	Check(store.CheckMediaHealth(first.record.md5, &health, &error),
		"check healthy media");
	Check(health.health_state == Xm8Ra::kRaMediaHealthOk,
		"healthy media state");
	Check(health.source_exists, "healthy media source exists");
	Check(health.working_exists, "healthy media working copy exists");
	Check(health.working_probe_ok, "healthy media working copy probes");

	Check(Xm8Ra::RemoveRaFile(first.working_path),
		"remove working copy for health check");
	Check(store.CheckMediaHealth(first.record.md5, &health, &error),
		"check missing working copy");
	Check(health.health_state == Xm8Ra::kRaMediaHealthWorkingMissing,
		"missing working copy health state");
	std::string reset_path;
	Check(store.ResetWorkingCopy(single, first.record.md5, &reset_path, &error),
		"restore missing working copy from matching original");
	Check(ReadFile(first.working_path) == original_single,
		"restored missing working copy bytes");

	Check(AppendByte(first.working_path, '\x24'),
		"simulate save data in working copy");
	Check(ReadFile(single) == original_single,
		"simulated save data does not change original");
	Check(store.ResetWorkingCopy(single, first.record.md5, &reset_path, &error),
		"reset working copy from matching original");
	Check(reset_path == first.working_path, "reset targets same working path");
	Check(ReadFile(first.working_path) == original_single,
		"reset working copy restores original bytes");

	Xm8Ra::ImportedPlaylist playlist;
	Check(store.ImportM3U(JoinPath(source_dir, "pair.m3u"), &playlist, &error),
		"import M3U playlist");
	Check(playlist.media.size() == 2, "M3U imports two media entries");
	Check(playlist.game_id == first.record.game_id,
		"M3U reuses first media game");
	Check(playlist.anchor_md5 == first.record.md5,
		"M3U anchor is first media");
	if (playlist.media.size() == 2) {
		Check(playlist.media[0].record.game_id == playlist.game_id,
			"M3U first media game id");
		Check(playlist.media[1].record.game_id == playlist.game_id,
			"M3U second media game id");
		Check(playlist.media[1].record.md5 ==
			"ff400f51a2567419b3778691a905952e",
			"M3U second media md5");
		Check(playlist.media[1].working_path.find("/ra/media/") !=
			std::string::npos,
			"M3U second working copy is under RA media root");

		Xm8Ra::LaunchProfile profile;
		Check(library.LoadLaunchProfile(playlist.game_id, &profile, &error),
			"load default launch profile");
		Check(profile.drives[0].assigned,
			"default launch profile assigns drive 1");
		Check(profile.drives[0].media_md5 == playlist.anchor_md5,
			"default launch profile drive 1 uses anchor media");
		Check(profile.drives[0].is_ra_anchor,
			"default launch profile marks drive 1 as RA anchor");
		Check(!profile.drives[1].assigned,
			"default launch profile leaves drive 2 empty");

		profile.drives[1].assigned = true;
		profile.drives[1].media_md5 = playlist.media[1].record.md5;
		profile.drives[1].bank_index = 0;
		profile.drives[1].is_ra_anchor = false;
		Check(library.SaveLaunchProfile(profile, &error),
			"save two-drive launch profile");
		Xm8Ra::LaunchProfile saved_profile;
		Check(library.LoadLaunchProfile(playlist.game_id, &saved_profile,
			&error), "reload two-drive launch profile");
		Check(saved_profile.drives[1].assigned,
			"two-drive launch profile assigns drive 2");
		Check(saved_profile.drives[1].media_md5 ==
			playlist.media[1].record.md5,
			"two-drive launch profile drive 2 media");

		Xm8Ra::ResolvedLaunchProfile resolved;
		Check(store.ResolveLaunchProfile(playlist.game_id, &resolved,
			&error), "resolve two-drive launch profile");
		Check(resolved.drives[0].assigned && resolved.drives[1].assigned,
			"resolved launch profile has two drives");
		Check(resolved.drives[0].working_path.find("/ra/media/") !=
			std::string::npos,
			"resolved drive 1 uses RA working copy");
		Check(resolved.drives[1].working_path.find("/ra/media/") !=
			std::string::npos,
			"resolved drive 2 uses RA working copy");
		Check(resolved.drives[0].working_path != single,
			"resolved drive 1 does not use original path");
		Check(resolved.anchor_md5 == playlist.anchor_md5,
			"resolved launch profile exposes RA anchor");

		Check(library.ListGames(&games, &error),
			"list games after M3U import");
		Check(!games.empty() && games[0].media_count == 2,
			"library list counts grouped media");

		Check(Xm8Ra::RemoveRaFile(resolved.drives[1].working_path),
			"remove drive 2 working copy before resolve");
		Check(store.ResolveLaunchProfile(playlist.game_id, &resolved,
			&error), "resolve recreates missing working copy");
		Check(PathExists(resolved.drives[1].working_path),
			"resolved launch profile recreated drive 2 working copy");

		saved_profile.drives[0].is_ra_anchor = false;
		Check(!library.SaveLaunchProfile(saved_profile, &error),
			"reject launch profile without RA anchor");
		Xm8Ra::LaunchProfile preserved_profile;
		Check(library.LoadLaunchProfile(playlist.game_id, &preserved_profile,
			&error), "reload profile after rejected replacement");
		Check(preserved_profile.drives[0].is_ra_anchor &&
			preserved_profile.drives[1].assigned,
			"rejected replacement preserves both previous drives");
		preserved_profile.drives[1] = Xm8Ra::LaunchDrive();
		Check(library.SaveLaunchProfile(preserved_profile, &error),
			"atomically clear Drive 2 from launch profile");
		Xm8Ra::LaunchProfile single_drive_profile;
		Check(library.LoadLaunchProfile(playlist.game_id,
			&single_drive_profile, &error),
			"reload single-drive launch profile");
		Check(single_drive_profile.drives[0].is_ra_anchor &&
			!single_drive_profile.drives[1].assigned,
			"single-drive replacement keeps anchor and clears Drive 2");
	}

	Xm8Ra::ImportedMedia standalone_multi;
	Check(store.ImportDesktopD88(JoinPath(source_dir, "multi.d88"),
		&standalone_multi, &error), "import standalone multi-bank D88");
	Check(standalone_multi.record.game_id != first.record.game_id,
		"standalone D88 creates a separate game before merge");
	Check(library.MergeGameMedia(first.record.game_id,
		standalone_multi.record.game_id, &error),
		"merge standalone game media into existing game");
	Xm8Ra::MediaHealthRecord merged_record;
	Check(library.LoadMediaHealthRecord(standalone_multi.record.md5,
		&merged_record, &error), "load merged media record");
	Check(merged_record.game_id == first.record.game_id,
		"merged media belongs to target game");
	Xm8Ra::ResolvedLaunchProfile merged_profile;
	Check(store.ResolveLaunchProfile(first.record.game_id, &merged_profile,
		&error), "resolve launch profile after media merge");
	Check(merged_profile.anchor_md5 == first.record.md5,
		"media merge keeps target launch anchor");

	const std::string folder_dir = JoinPath(base, "folder-scan");
	const std::string nested_dir = JoinPath(folder_dir, "nested");
	Check(MakeDirectoryTree(nested_dir, &error), "create folder scan tree");
	Check(CopyFileBytes(JoinPath(source_dir, "single.d88"),
		JoinPath(nested_dir, "alpha.D88")), "copy uppercase D88");
	Check(CopyFileBytes(JoinPath(source_dir, "second.d88"),
		JoinPath(nested_dir, "beta.d88")), "copy second D88");
	Check(WriteTextFile(JoinPath(nested_dir, "group.M3U"),
		"# recursive import fixture\nalpha.D88#0\nbeta.d88#0\n"),
		"write uppercase M3U");
	Xm8Ra::ImportedFolder folder_import;
	Check(store.ImportFolderRecursive(folder_dir, &folder_import, &error),
		"recursive folder import");
	Check(folder_import.scanned_candidates == 3,
		"recursive folder scans D88 and M3U candidates");
	Check(folder_import.playlists.size() == 1,
		"recursive folder imports playlist");
	Check(folder_import.standalone_media.size() == 2,
		"recursive folder imports standalone D88 after playlist");
	if (!folder_import.playlists.empty()) {
		Check(folder_import.playlists[0].media.size() == 2,
			"recursive playlist has two media");
		Check(folder_import.playlists[0].anchor_md5 ==
			"5c50ca4f9e3a7afbe4d6666e8974949d",
			"recursive playlist anchor uses first D88");
	}

	Check(AppendByte(single, '\x55'), "modify source fixture");
	Check(!store.ResetWorkingCopy(single, first.record.md5, &reset_path, &error),
		"reset rejects modified original");
	Check(store.CheckMediaHealth(first.record.md5, &health, &error),
		"check modified original health");
	Check(health.health_state == Xm8Ra::kRaMediaHealthSourceChanged,
		"modified original health state");
	std::vector<Xm8Ra::RaMediaBankHash> local_hashes;
	Check(library.ListMediaBankHashes(&local_hashes, &error),
		"list bank hashes for library sync");
	Check(local_hashes.size() >= 4,
		"library sync sees single and multi-bank hashes");
	Xm8Ra::RaLibrarySyncPayload sync;
	sync.username = "tester";
	std::set<std::string> unique_sync_hashes;
	for (const Xm8Ra::RaMediaBankHash& local : local_hashes) {
		if (!unique_sync_hashes.insert(local.ra_hash).second) {
			continue;
		}
		Xm8Ra::RaLibraryHashMatch match;
		match.hash = local.ra_hash;
		match.ra_game_id = 1234;
		sync.hashes.push_back(match);
	}
	Xm8Ra::RaLibraryGameTitle sync_title;
	sync_title.ra_game_id = 1234;
	sync_title.title = "RA Synced Game";
	sync_title.badge_url = "https://media.example/synced.png";
	sync.titles.push_back(sync_title);
	Xm8Ra::RaLibraryProgress sync_progress;
	sync_progress.ra_game_id = 1234;
	sync_progress.core_total = 10;
	sync_progress.core_unlocked = 4;
	sync_progress.hardcore_unlocked = 2;
	sync.progress.push_back(sync_progress);
	const bool sync_ok = library.ApplyLibrarySync(sync, &error);
	Check(sync_ok, "apply complete library sync transaction");
	if (!sync_ok) {
		std::cerr << "library sync error: " << error << '\n';
	}
	Check(QueryInt(library.DatabasePath(),
		"SELECT COUNT(*) FROM sync_state WHERE sync_key ="
		" 'library:tester:pc8800'") == 1,
		"successful sync records completion");

	Xm8Ra::RaLibrarySyncPayload incomplete = sync;
	incomplete.username = "incomplete";
	incomplete.titles.clear();
	Check(!library.ApplyLibrarySync(incomplete, &error),
		"reject partial sync before database mutation");
	Check(QueryInt(library.DatabasePath(),
		"SELECT COUNT(*) FROM sync_state WHERE sync_key ="
		" 'library:incomplete:pc8800'") == 0,
		"partial sync does not advance completion state");

	Check(ExecSql(library.DatabasePath(),
		"CREATE TRIGGER fail_library_sync BEFORE INSERT ON progress"
		" WHEN NEW.username = 'rollback' BEGIN"
		" SELECT RAISE(ABORT, 'forced sync failure'); END;", &error),
		"install forced sync failure trigger");
	Xm8Ra::RaLibrarySyncPayload rollback_sync = sync;
	rollback_sync.username = "rollback";
	rollback_sync.titles[0].title = "Should Roll Back";
	Check(!library.ApplyLibrarySync(rollback_sync, &error),
		"database failure rolls back library sync");
	Check(QueryText(library.DatabasePath(),
		"SELECT title FROM games WHERE id = 1") == "RA Synced Game",
		"failed sync rolls back title update");
	Check(QueryInt(library.DatabasePath(),
		"SELECT COUNT(*) FROM sync_state WHERE sync_key ="
		" 'library:rollback:pc8800'") == 0,
		"failed transaction does not record completion");
	Check(ExecSql(library.DatabasePath(), "DROP TRIGGER fail_library_sync",
		&error), "remove forced sync failure trigger");

	Xm8Ra::RaLibrarySyncPayload other_user = sync;
	other_user.username = "other";
	other_user.progress[0].core_unlocked = 1;
	Check(library.ApplyLibrarySync(other_user, &error),
		"sync a second user independently");
	Check(QueryInt(library.DatabasePath(),
		"SELECT COUNT(*) FROM progress WHERE ra_game_id = 1234") == 2,
		"progress rows remain separated by username");
	std::vector<Xm8Ra::RaLibraryGameListItem> tester_games;
	std::vector<Xm8Ra::RaLibraryGameListItem> other_games;
	Check(library.ListGamesForUser("tester", &tester_games, &error),
		"list library progress for first user");
	Check(library.ListGamesForUser("other", &other_games, &error),
		"list library progress for second user");
	Check(!tester_games.empty() && tester_games[0].core_unlocked == 4,
		"first user sees only first user progress");
	Check(!other_games.empty() && other_games[0].core_unlocked == 1,
		"second user sees only second user progress");
	Check(library.ListGames(&games, &error),
		"list games with progress and health error");
	bool found_first_game = false;
	for (const Xm8Ra::RaLibraryGameListItem& item : games) {
		if (item.game_id == first.record.game_id) {
			found_first_game = true;
			Check(item.ra_game_id == 1234, "library list RA game id");
			Check(item.has_progress, "library list progress present");
			Check(item.core_total == 10 && item.core_unlocked == 4,
				"library list progress values");
			Check(item.hardcore_unlocked == 2,
				"library list hardcore progress");
			Check(item.points_total == 0 && item.points_unlocked == 0,
				"all-progress sync leaves unavailable points null");
			Check(item.health_state ==
				Xm8Ra::kRaMediaHealthSourceChanged,
				"library list reports health error");
		}
	}
	Check(found_first_game, "library list contains first game after progress");
	Check(library.MarkGamePlayed(first.record.game_id, &error),
		"mark game played");
	Check(library.ListGames(&games, &error),
		"list games after mark played");
	Check(!games.empty() && games[0].game_id == first.record.game_id,
		"recently played game sorts first");
	Check(ExecSql(library.DatabasePath(),
		"UPDATE games SET ra_game_id = NULL, identification_state = 4"
		" WHERE id = 1", &error),
		"create media conflict launch fixture");
	int64_t conflict_ra_game_id = -1;
	int conflict_state = -1;
	Check(library.LoadGameIdentification(first.record.game_id,
		&conflict_ra_game_id, &conflict_state, &error),
		"load conflicted game identification");
	Check(conflict_ra_game_id == 0 &&
		conflict_state == Xm8Ra::kRaIdentificationConflict,
		"conflicted game has no launchable RA game id");
	Check(library.ListGames(&games, &error),
		"list library including media conflict");
	bool found_conflict = false;
	for (const Xm8Ra::RaLibraryGameListItem& item : games) {
		if (item.game_id == first.record.game_id) {
			found_conflict = item.identification_state ==
				Xm8Ra::kRaIdentificationConflict;
		}
	}
	Check(found_conflict, "media conflict remains visible in RA library");
	Xm8Ra::ResolvedLaunchProfile conflict_profile;
	Check(!store.ResolveLaunchProfile(first.record.game_id,
		&conflict_profile, &error),
		"media store rejects conflicted game launch");
	Check(error == "media conflict must be resolved before launch",
		"conflicted launch reports explicit reason");
	Check(ExecSql(library.DatabasePath(),
		"UPDATE games SET ra_game_id = 1234, identification_state = 1"
		" WHERE id = 1", &error),
		"restore identified game after conflict test");

	Xm8Ra::ImportedMedia modified;
	Check(store.ImportDesktopD88(single, &modified, &error),
		"modified source imports as new medium");
	Check(modified.record.md5 != first.record.md5,
		"modified source has distinct md5");
	Check(library.ListGames(&games, &error),
		"list games after unidentified modified import");
	bool found_modified_game = false;
	for (const Xm8Ra::RaLibraryGameListItem& item : games) {
		if (item.game_id == modified.record.game_id) {
			found_modified_game = true;
		}
	}
	Check(!found_modified_game,
		"unidentified modified import is hidden from RA library");
	Check(ReadFile(first.working_path) == original_single,
		"existing working save is not overwritten");

	static const uint8_t cache_png_bytes[] = {
		0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
		0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
		0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
		0x08, 0x04, 0x00, 0x00, 0x00, 0xb5, 0x1c, 0x0c,
		0x02, 0x00, 0x00, 0x00, 0x0b, 0x49, 0x44, 0x41,
		0x54, 0x78, 0xda, 0x63, 0x64, 0xf8, 0x0f, 0x00,
		0x01, 0x05, 0x01, 0x01, 0x27, 0x18, 0xe3, 0x66,
		0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44,
		0xae, 0x42, 0x60, 0x82
	};
	const std::vector<uint8_t> cache_png(cache_png_bytes,
		cache_png_bytes + sizeof(cache_png_bytes));
	const std::string cache_url_1 = "https://media.example/cache-1.png";
	const std::string cache_url_2 = "https://media.example/cache-2.png";
	std::vector<uint8_t> cached_data;
	std::string cached_type;
	Check(library.LoadCachedImage(cache_url_1, 1, &cached_data,
		&cached_type, &error) == Xm8Ra::RaImageCacheLoadResult::Miss,
		"image cache miss");
	Check(library.StoreCachedImage(cache_url_1, Xm8Ra::RaImageKind::GameBadge,
		"Image/PNG; charset=binary", cache_png, 10, 1024, {}, &error),
		"store validated PNG image");
	Check(library.StoreCachedImage(cache_url_2,
		Xm8Ra::RaImageKind::AchievementBadge, "image/png", cache_png, 20,
		1024, {}, &error), "store second PNG image");
	Check(library.LoadCachedImage(cache_url_1, 30, &cached_data, &cached_type,
		&error) == Xm8Ra::RaImageCacheLoadResult::Hit,
		"image cache hit");
	Check(cached_data == cache_png && cached_type == "image/png",
		"image cache preserves validated bytes and normalized type");
	Check(library.PruneImageCache(static_cast<int64_t>(cache_png.size()), {},
		&error), "prune image cache to one image");
	Check(QueryInt(library.DatabasePath(),
		"SELECT COUNT(*) FROM image_cache WHERE url ="
		" 'https://media.example/cache-1.png'") == 1,
		"LRU keeps recently used image");
	Check(QueryInt(library.DatabasePath(),
		"SELECT COUNT(*) FROM image_cache WHERE url ="
		" 'https://media.example/cache-2.png'") == 0,
		"LRU evicts oldest image");
	Check(library.StoreCachedImage(cache_url_2,
		Xm8Ra::RaImageKind::AchievementBadge, "image/png", cache_png, 40,
		1024, {}, &error), "restore second image for protected eviction");
	Check(library.PruneImageCache(0, {cache_url_1}, &error),
		"prune skips protected image");
	Check(QueryInt(library.DatabasePath(),
		"SELECT COUNT(*) FROM image_cache") == 1,
		"only protected image remains above zero-byte limit");
	Check(ExecSql(library.DatabasePath(),
		"UPDATE image_cache SET relative_path = 'images/999.png'"
		" WHERE url = 'https://media.example/cache-1.png'", &error),
		"tamper cached relative path");
	Check(WriteTextFile(JoinPath(ra_root, "images/999.png"), "sentinel"),
		"create non-owned image path");
	Check(library.LoadCachedImage(cache_url_1, 45, &cached_data, &cached_type,
		&error) == Xm8Ra::RaImageCacheLoadResult::Miss,
		"mismatched cache id becomes a miss");
	Check(PathExists(JoinPath(ra_root, "images/999.png")),
		"mismatched cache id does not delete another file");
	Check(library.StoreCachedImage(cache_url_1, Xm8Ra::RaImageKind::GameBadge,
		"image/png", cache_png, 46, 1024, {}, &error),
		"restore image after mismatched cache id");
	const std::string cached_relative = QueryText(library.DatabasePath(),
		"SELECT relative_path FROM image_cache WHERE url ="
		" 'https://media.example/cache-1.png'");
	Check(WriteTextFile(JoinPath(ra_root, cached_relative.c_str()), "broken"),
		"corrupt cached image file");
	Check(library.LoadCachedImage(cache_url_1, 50, &cached_data, &cached_type,
		&error) == Xm8Ra::RaImageCacheLoadResult::Miss,
		"corrupt cached image becomes a miss");
	Check(QueryInt(library.DatabasePath(),
		"SELECT COUNT(*) FROM image_cache") == 0,
		"corrupt cached image metadata is removed");
	Check(!library.StoreCachedImage("https://media.example/text",
		Xm8Ra::RaImageKind::Other, "text/plain", cache_png, 60, 1024, {},
		&error), "reject non-image content type");
	std::vector<uint8_t> oversized(1024U * 1024U + 1U, 0);
	Check(!library.StoreCachedImage("https://media.example/oversize.png",
		Xm8Ra::RaImageKind::Other, "image/png", oversized, 70,
		2 * 1024 * 1024, {}, &error), "reject oversized encoded image");
	Check(library.StoreCachedImage(cache_url_1, Xm8Ra::RaImageKind::GameBadge,
		"image/png", cache_png, 80, 1024, {}, &error),
		"store image before library reopen");

	library.Close();

	Xm8Ra::RaLibrary reopened;
	Check(reopened.Open(ra_root, &error), "reopen RA library");
	Xm8Ra::RaSettings persisted;
	Check(reopened.LoadSettings(&persisted, &error),
		"load persisted RA settings");
	cached_data.clear();
	cached_type.clear();
	Check(reopened.LoadCachedImage(cache_url_1, 90, &cached_data, &cached_type,
		&error) == Xm8Ra::RaImageCacheLoadResult::Hit,
		"persistent image cache survives library reopen");
	Check(cached_data == cache_png,
		"reopened image cache preserves encoded bytes");
	Check(persisted.enabled, "RA enabled setting persisted");
	Check(persisted.last_mode == Xm8Ra::kRaModeSoftcore,
		"saved RA softcore setting overrides the default and persists");
	Check(persisted.unofficial_enabled,
		"RA unofficial setting persisted");
	Check(persisted.notification_seconds == 8,
		"RA notification setting persisted");
	Check(persisted.image_cache_limit_mib == 256,
		"RA image cache setting persisted");
	pending_records.clear();
	Check(reopened.ListPendingUnlocks(pending.account, &pending_records, &error) &&
		pending_records.size() == 1 &&
		pending_records[0].attempt_count == 1 &&
		pending_records[0].last_error == "transport",
		"pending unlock survives process restart without token data");
	Check(reopened.RemovePendingUnlocksForAccount(pending.account, &error),
		"confirmed logout removes pending unlocks");
	Check(reopened.CountPendingUnlocks(pending.account, &pending_count, &error) &&
		pending_count == 0, "pending unlock removal is complete");
	Xm8Ra::RaSettings invalid = persisted;
	invalid.last_mode = 99;
	Check(!reopened.SaveSettings(invalid, &error),
		"reject invalid RA mode");
	reopened.Close();

	const std::string legacy_root = JoinPath(base, "legacy-ra");
	Xm8Ra::RaLibrary legacy_seed;
	Check(legacy_seed.Open(legacy_root, &error),
		"create library before v1 migration fixture");
	legacy_seed.Close();
	Check(ExecSql(JoinPath(legacy_root, "library.sqlite3"),
		"PRAGMA foreign_keys=OFF; BEGIN;"
		"CREATE TABLE media_banks_v1("
		" media_md5 TEXT NOT NULL REFERENCES media(md5) ON DELETE CASCADE,"
		" bank_index INTEGER NOT NULL, label TEXT NOT NULL,"
		" PRIMARY KEY(media_md5, bank_index), CHECK(bank_index >= 0));"
		"DROP TABLE media_banks;"
		"ALTER TABLE media_banks_v1 RENAME TO media_banks;"
		"UPDATE schema_meta SET schema_version = 1 WHERE singleton = 1;"
		"COMMIT; PRAGMA foreign_keys=ON;", &error),
		"downgrade empty fixture to schema v1");
	Xm8Ra::RaLibrary migrated;
	Check(migrated.Open(legacy_root, &error), "migrate schema v1 to v4");
	Check(QueryInt(migrated.DatabasePath(),
		"SELECT schema_version FROM schema_meta WHERE singleton = 1") == 4,
		"migration advances schema version");
	Check(QueryInt(migrated.DatabasePath(),
		"SELECT COUNT(*) FROM pragma_table_info('media_banks')"
		" WHERE name IN ('ra_hash','ra_game_id','identification_state')") == 3,
		"migration adds bank-level RA columns");
	migrated.Close();

	const std::string unknown_version_root = JoinPath(base, "unknown-version-ra");
	Xm8Ra::RaLibrary unknown_version_seed;
	Check(unknown_version_seed.Open(unknown_version_root, &error),
		"create library before unknown-version fixtures");
	unknown_version_seed.Close();
	const std::string unknown_version_db =
		JoinPath(unknown_version_root, "library.sqlite3");
	Check(ExecSql(unknown_version_db,
		"UPDATE schema_meta SET schema_version = 0 WHERE singleton = 1;", &error),
		"set zero schema version fixture");
	Xm8Ra::RaLibrary zero_version;
	Check(!zero_version.Open(unknown_version_root, &error),
		"reject zero schema version");
	Check(QueryInt(unknown_version_db,
		"SELECT schema_version FROM schema_meta WHERE singleton = 1") == 0,
		"zero schema version is not silently migrated");
	Check(ExecSql(unknown_version_db,
		"UPDATE schema_meta SET schema_version = -1 WHERE singleton = 1;", &error),
		"set negative schema version fixture");
	Xm8Ra::RaLibrary negative_version;
	Check(!negative_version.Open(unknown_version_root, &error),
		"reject negative schema version");
	Check(QueryInt(unknown_version_db,
		"SELECT schema_version FROM schema_meta WHERE singleton = 1") == -1,
		"negative schema version is not silently migrated");

	// Duplicate local games that resolve to one RA ID are merged atomically.
	const std::string merge_root = JoinPath(base, "merge-ra");
	Xm8Ra::RaLibrary merge_library;
	Check(merge_library.Open(merge_root, &error),
		"open merge conflict library");
	Xm8Ra::RaMediaStore merge_store(&merge_library);
	Xm8Ra::ImportedMedia merge_first;
	Xm8Ra::ImportedMedia merge_second;
	Check(merge_store.ImportDesktopD88(JoinPath(source_dir, "single.d88"),
		&merge_first, &error), "import first merge medium");
	Check(merge_store.ImportDesktopD88(JoinPath(source_dir, "second.d88"),
		&merge_second, &error), "import second merge medium");
	const std::vector<char> merge_first_bytes =
		ReadFile(merge_first.working_path);
	const std::vector<char> merge_second_bytes =
		ReadFile(merge_second.working_path);
	Xm8Ra::RaLibrarySyncPayload merge_sync;
	merge_sync.username = "merge-user";
	for (const Xm8Ra::ImportedMedia *item : {&merge_first, &merge_second}) {
		Xm8Ra::RaLibraryHashMatch match;
		match.hash = item->media_info.bank_md5s[0];
		match.ra_game_id = 5001;
		merge_sync.hashes.push_back(match);
	}
	Xm8Ra::RaLibraryGameTitle merge_title;
	merge_title.ra_game_id = 5001;
	merge_title.title = "Merged RA Game";
	merge_sync.titles.push_back(merge_title);
	Check(merge_library.ApplyLibrarySync(merge_sync, &error),
		"create duplicate-game merge conflict");
	Xm8Ra::RaGameConflictInfo merge_info;
	Check(merge_library.InspectGameConflict(merge_second.record.game_id,
		&merge_info, &error), "inspect merge conflict");
	Check(merge_info.kind == Xm8Ra::RaGameConflictKind::Merge &&
		merge_info.related_game_count == 2,
		"duplicate RA games classify as merge");
	Check(ExecSql(merge_library.DatabasePath(),
		"CREATE TRIGGER fail_conflict_merge BEFORE UPDATE OF game_id ON media"
		" WHEN OLD.game_id != NEW.game_id BEGIN"
		" SELECT RAISE(ABORT, 'forced merge rollback'); END;", &error),
		"install merge rollback trigger");
	Check(!merge_library.ResolveGameConflict(merge_second.record.game_id,
		&merge_info, &error), "merge failure rolls back transaction");
	Check(QueryInt(merge_library.DatabasePath(),
		"SELECT COUNT(*) FROM games") == 2,
		"failed merge preserves both games");
	Check(QueryInt(merge_library.DatabasePath(),
		"SELECT COUNT(DISTINCT game_id) FROM media") == 2,
		"failed merge preserves media ownership");
	Check(QueryInt(merge_library.DatabasePath(),
		"SELECT COUNT(*) FROM launch_profiles") == 2,
		"failed merge preserves launch profiles");
	Check(QueryInt(merge_library.DatabasePath(),
		"SELECT COUNT(*) FROM pragma_foreign_key_check") == 0,
		"failed merge leaves all foreign keys valid");
	Check(ReadFile(merge_first.working_path) == merge_first_bytes &&
		ReadFile(merge_second.working_path) == merge_second_bytes,
		"failed merge does not alter working media");
	Check(ExecSql(merge_library.DatabasePath(),
		"DROP TRIGGER fail_conflict_merge", &error),
		"remove merge rollback trigger");
	Check(merge_library.ResolveGameConflict(merge_second.record.game_id,
		&merge_info, &error), "resolve duplicate games by merge");
	Check(QueryInt(merge_library.DatabasePath(),
		"SELECT COUNT(*) FROM games") == 1,
		"merge removes redundant game");
	Check(QueryInt(merge_library.DatabasePath(),
		"SELECT COUNT(*) FROM media WHERE game_id = 1") == 2,
		"merge moves all media to oldest game");
	Check(QueryInt(merge_library.DatabasePath(),
		"SELECT COUNT(*) FROM launch_profiles WHERE game_id = 1") == 1,
		"merge preserves oldest launch profile only");
	Check(QueryInt(merge_library.DatabasePath(),
		"SELECT identification_state FROM games WHERE id = 1") ==
		Xm8Ra::kRaIdentificationIdentified,
		"merged game becomes identified");
	Check(QueryInt(merge_library.DatabasePath(),
		"SELECT ra_game_id FROM games WHERE id = 1") == 5001,
		"merged game stores common RA game id");
	Check(QueryInt(merge_library.DatabasePath(),
		"SELECT COUNT(*) FROM pragma_foreign_key_check") == 0,
		"merged database has no broken foreign keys");
	Check(QueryInt(merge_library.DatabasePath(),
		"SELECT COUNT(*) FROM (SELECT game_id FROM media GROUP BY game_id"
		" HAVING MIN(ordinal) != 0 OR MAX(ordinal) != COUNT(*) - 1"
		" OR COUNT(DISTINCT ordinal) != COUNT(*))") == 0,
		"merged media ordinals are compact and unique");
	Check(QueryInt(merge_library.DatabasePath(),
		"SELECT COUNT(*) FROM games g WHERE (SELECT COUNT(*)"
		" FROM launch_profiles lp WHERE lp.game_id = g.id"
		" AND lp.is_ra_anchor = 1) != 1") == 0,
		"merged game has exactly one RA anchor");
	Check(ReadFile(merge_first.working_path) == merge_first_bytes &&
		ReadFile(merge_second.working_path) == merge_second_bytes,
		"merge only changes metadata, not working media");
	Xm8Ra::ResolvedLaunchProfile resolved_merge;
	Check(merge_store.ResolveLaunchProfile(1, &resolved_merge, &error),
		"merged game launch profile remains resolvable");
	Check(resolved_merge.anchor_md5 == merge_first.record.md5,
		"merge keeps oldest game anchor");
	merge_library.Close();

	// A grouped game whose physical media map to different IDs is split.
	const std::string split_root = JoinPath(base, "split-ra");
	Xm8Ra::RaLibrary split_library;
	Check(split_library.Open(split_root, &error),
		"open split conflict library");
	Xm8Ra::RaMediaStore split_store(&split_library);
	Xm8Ra::ImportedPlaylist split_playlist;
	Check(split_store.ImportM3U(JoinPath(source_dir, "pair.m3u"),
		&split_playlist, &error), "import split conflict playlist");
	std::vector<std::vector<char>> split_working_bytes;
	for (const Xm8Ra::ImportedMedia& item : split_playlist.media) {
		split_working_bytes.push_back(ReadFile(item.working_path));
	}
	Xm8Ra::RaLibrarySyncPayload split_sync;
	split_sync.username = "split-user";
	for (size_t index = 0; index < split_playlist.media.size(); ++index) {
		Xm8Ra::RaLibraryHashMatch match;
		match.hash = split_playlist.media[index].media_info.bank_md5s[0];
		match.ra_game_id = static_cast<uint32_t>(6001 + index);
		split_sync.hashes.push_back(match);
		Xm8Ra::RaLibraryGameTitle title;
		title.ra_game_id = match.ra_game_id;
		title.title = "Split RA Game " + std::to_string(index + 1);
		split_sync.titles.push_back(title);
	}
	Check(split_library.ApplyLibrarySync(split_sync, &error),
		"create grouped-media split conflict");
	Xm8Ra::RaGameConflictInfo split_info;
	Check(split_library.InspectGameConflict(split_playlist.game_id,
		&split_info, &error), "inspect split conflict");
	Check(split_info.kind == Xm8Ra::RaGameConflictKind::Split &&
		split_info.resulting_game_count == 2,
		"different physical media classify as split");
	Check(ExecSql(split_library.DatabasePath(),
		"CREATE TRIGGER fail_conflict_split BEFORE INSERT ON games"
		" WHEN NEW.ra_game_id = 6002 BEGIN"
		" SELECT RAISE(ABORT, 'forced split rollback'); END;", &error),
		"install split rollback trigger");
	Check(!split_library.ResolveGameConflict(split_playlist.game_id,
		&split_info, &error), "split failure rolls back transaction");
	Check(QueryInt(split_library.DatabasePath(),
		"SELECT COUNT(*) FROM games") == 1,
		"failed split preserves original game");
	Check(QueryInt(split_library.DatabasePath(),
		"SELECT COUNT(DISTINCT game_id) FROM media") == 1,
		"failed split preserves grouped media");
	Check(QueryInt(split_library.DatabasePath(),
		"SELECT COUNT(*) FROM launch_profiles") == 1,
		"failed split restores original launch profile");
	Check(QueryInt(split_library.DatabasePath(),
		"SELECT COUNT(*) FROM pragma_foreign_key_check") == 0,
		"failed split leaves all foreign keys valid");
	for (size_t index = 0; index < split_playlist.media.size(); ++index) {
		Check(ReadFile(split_playlist.media[index].working_path) ==
			split_working_bytes[index],
			"failed split does not alter working media");
	}
	Check(ExecSql(split_library.DatabasePath(),
		"DROP TRIGGER fail_conflict_split", &error),
		"remove split rollback trigger");
	Check(split_library.ResolveGameConflict(split_playlist.game_id,
		&split_info, &error), "resolve grouped media by split");
	Check(QueryInt(split_library.DatabasePath(),
		"SELECT COUNT(*) FROM games") == 2,
		"split creates one game per RA id");
	Check(QueryInt(split_library.DatabasePath(),
		"SELECT COUNT(DISTINCT game_id) FROM media") == 2,
		"split separates physical media ownership");
	Check(QueryInt(split_library.DatabasePath(),
		"SELECT COUNT(*) FROM launch_profiles") == 2,
		"split creates one launch profile per game");
	Check(QueryInt(split_library.DatabasePath(),
		"SELECT COUNT(*) FROM games WHERE identification_state = 1"
		" AND ra_game_id IN (6001, 6002)") == 2,
		"split results are identified and uniquely mapped");
	Check(QueryInt(split_library.DatabasePath(),
		"SELECT COUNT(*) FROM pragma_foreign_key_check") == 0,
		"split database has no broken foreign keys");
	Check(QueryInt(split_library.DatabasePath(),
		"SELECT COUNT(*) FROM (SELECT game_id FROM media GROUP BY game_id"
		" HAVING MIN(ordinal) != 0 OR MAX(ordinal) != COUNT(*) - 1"
		" OR COUNT(DISTINCT ordinal) != COUNT(*))") == 0,
		"split media ordinals are compact and unique");
	Check(QueryInt(split_library.DatabasePath(),
		"SELECT COUNT(*) FROM games g WHERE (SELECT COUNT(*)"
		" FROM launch_profiles lp WHERE lp.game_id = g.id"
		" AND lp.is_ra_anchor = 1) != 1") == 0,
		"each split game has exactly one RA anchor row");
	for (size_t index = 0; index < split_playlist.media.size(); ++index) {
		Check(ReadFile(split_playlist.media[index].working_path) ==
			split_working_bytes[index],
			"split only changes metadata, not working media");
	}
	Check(split_library.ListGames(&games, &error),
		"list split result games");
	Check(games.size() == 2, "both split games are visible");
	for (const Xm8Ra::RaLibraryGameListItem& game : games) {
		Xm8Ra::ResolvedLaunchProfile profile;
		Check(split_store.ResolveLaunchProfile(game.game_id, &profile, &error),
			"each split game has a resolvable launch profile");
		Check(profile.drives[0].assigned &&
			profile.drives[0].is_ra_anchor,
			"each split game has exactly one drive-1 anchor");
	}
	split_library.Close();

	// One physical multi-bank D88 cannot be split into separate local games.
	const std::string manual_root = JoinPath(base, "manual-conflict-ra");
	Xm8Ra::RaLibrary manual_library;
	Check(manual_library.Open(manual_root, &error),
		"open manual conflict library");
	Xm8Ra::RaMediaStore manual_store(&manual_library);
	Xm8Ra::ImportedMedia manual_media;
	Check(manual_store.ImportDesktopD88(JoinPath(source_dir, "multi.d88"),
		&manual_media, &error), "import multi-bank manual conflict");
	const std::vector<char> manual_working_bytes =
		ReadFile(manual_media.working_path);
	Xm8Ra::RaLibrarySyncPayload manual_sync;
	manual_sync.username = "manual-user";
	for (size_t index = 0; index < manual_media.media_info.bank_md5s.size();
		++index) {
		Xm8Ra::RaLibraryHashMatch match;
		match.hash = manual_media.media_info.bank_md5s[index];
		match.ra_game_id = static_cast<uint32_t>(7001 + index);
		manual_sync.hashes.push_back(match);
		Xm8Ra::RaLibraryGameTitle title;
		title.ra_game_id = match.ra_game_id;
		title.title = "Manual RA Game " + std::to_string(index + 1);
		manual_sync.titles.push_back(title);
	}
	Check(manual_library.ApplyLibrarySync(manual_sync, &error),
		"create multi-bank manual conflict");
	Xm8Ra::RaGameConflictInfo manual_info;
	Check(manual_library.InspectGameConflict(manual_media.record.game_id,
		&manual_info, &error), "inspect manual conflict");
	Check(manual_info.kind == Xm8Ra::RaGameConflictKind::Manual,
		"multi-ID physical D88 requires manual configuration");
	Check(!manual_library.ResolveGameConflict(manual_media.record.game_id,
		&manual_info, &error), "manual conflict rejects automatic resolution");
	Check(QueryInt(manual_library.DatabasePath(),
		"SELECT COUNT(*) FROM games WHERE identification_state = 4") == 1,
		"manual conflict remains unchanged after rejection");
	Check(QueryInt(manual_library.DatabasePath(),
		"SELECT COUNT(*) FROM launch_profiles") == 1,
		"manual conflict rejection preserves launch profile");
	Check(QueryInt(manual_library.DatabasePath(),
		"SELECT COUNT(*) FROM pragma_foreign_key_check") == 0,
		"manual conflict rejection keeps foreign keys valid");
	Check(ReadFile(manual_media.working_path) == manual_working_bytes,
		"manual conflict rejection does not alter working media");
	manual_library.Close();

	const std::string corrupt_root = JoinPath(base, "corrupt-ra");
	Check(MakeDirectoryTree(corrupt_root, &error), "create corrupt DB root");
	Check(WriteTextFile(JoinPath(corrupt_root, "library.sqlite3"),
		"this is not a sqlite database"),
		"write corrupt DB");
	Xm8Ra::RaLibrary recovered;
	Check(recovered.Open(corrupt_root, &error), "recover corrupt RA DB");
	Check(DirectoryHasPrefix(corrupt_root, "library.sqlite3.corrupt."),
		"corrupt DB quarantined");
	Check(recovered.LoadSettings(&settings, &error),
		"new settings exist after corrupt DB recovery");
	Check(!settings.enabled, "recovered DB settings default disabled");
	std::string recovery_reason;
	Check(recovered.RecoveryRequired(&recovery_reason) &&
		!recovery_reason.empty(),
		"corrupt DB recovery requires explicit pending-unlock acknowledgement");
	Check(recovered.ConfirmDiscardRecovery(&error),
		"acknowledge possible pending unlock loss");
	Check(!recovered.RecoveryRequired(nullptr),
		"recovery acknowledgement clears the durable marker");
	recovered.Close();

	const std::string marked_corrupt_root =
		JoinPath(base, "marked-corrupt-ra");
	Check(MakeDirectoryTree(marked_corrupt_root, &error),
		"create marked corrupt DB root");
	Check(WriteTextFile(JoinPath(marked_corrupt_root, "library.sqlite3"),
		"this is also not a sqlite database"),
		"write marked corrupt DB");
	const std::string existing_marker =
		JoinPath(marked_corrupt_root, "pending-unlock-recovery-required");
	Check(WriteTextFile(existing_marker, "existing recovery marker\n"),
		"write existing pending-unlock recovery marker");
	const std::vector<char> existing_marker_bytes = ReadFile(existing_marker);
	Xm8Ra::RaLibrary marked_recovery;
	Check(marked_recovery.Open(marked_corrupt_root, &error),
		"recover corrupt RA DB with existing marker");
	Check(marked_recovery.RecoveryRequired(nullptr),
		"existing marker remains fail-closed after quarantine");
	Check(ReadFile(existing_marker) == existing_marker_bytes,
		"quarantine preserves an existing recovery marker");
	Check(marked_recovery.ConfirmDiscardRecovery(&error),
		"acknowledge preserved recovery marker");
	marked_recovery.Close();

	const std::string empty_root = JoinPath(base, "empty-ra");
	Xm8Ra::RaLibrary empty_library;
	Check(empty_library.Open(empty_root, &error), "open empty library");
	Check(empty_library.ListGames(&games, &error), "list empty library");
	Check(games.empty(), "empty library has no games");
	empty_library.Close();

	RemoveTree(base);

	if (failures != 0) {
		std::cerr << failures << " test(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "RA library/store tests passed\n";
	return EXIT_SUCCESS;
}

#include "ra_library.h"

#include "ra_build_info.h"
#include "ra_file_util.h"
#include "sqlite3.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace Xm8Ra {
namespace {

constexpr int kSchemaVersion = 4;

std::string JoinPath(const std::string& base, const char *child)
{
	if (!base.empty() && base.back() == '/') {
		return base + child;
	}
	return base + "/" + child;
}

bool RenameIfExists(const std::string& source, const std::string& destination,
	std::string *error)
{
	if (!RaPathExists(source)) {
		return true;
	}
	return MoveRaFile(source, destination, false, error);
}

bool EnsurePendingUnlockRecoveryMarker(const std::string& path,
	std::string *error)
{
	if (RaPathExists(path)) {
		return true;
	}

	static const uint8_t marker[] =
		"A quarantined RA database may contain pending unlocks.\n";
	const std::string temporary = path + ".tmp";
	RemoveRaFile(temporary, nullptr);
	if (!WriteRaFile(temporary, marker, sizeof(marker) - 1, error)) {
		RemoveRaFile(temporary, nullptr);
		return false;
	}
	if (!MoveRaFile(temporary, path, false, error)) {
		RemoveRaFile(temporary, nullptr);
		// Another process may have created the conservative marker first.
		if (RaPathExists(path)) {
			if (error != nullptr) error->clear();
			return true;
		}
		return false;
	}
	return true;
}

bool Prepare(sqlite3 *db, const char *sql, sqlite3_stmt **stmt,
	std::string *error)
{
	const int rc = sqlite3_prepare_v2(db, sql, -1, stmt, nullptr);
	if (rc != SQLITE_OK) {
		if (error != nullptr) {
			*error = sqlite3_errmsg(db);
		}
		return false;
	}
	return true;
}

bool StepDone(sqlite3 *db, sqlite3_stmt *stmt, std::string *error)
{
	const int rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		if (error != nullptr) {
			*error = sqlite3_errmsg(db);
		}
		sqlite3_finalize(stmt);
		return false;
	}
	sqlite3_finalize(stmt);
	return true;
}

std::string ColumnText(sqlite3_stmt *stmt, int column)
{
	const unsigned char *text = sqlite3_column_text(stmt, column);
	return text != nullptr ? reinterpret_cast<const char*>(text) : "";
}

std::string SortTitle(const std::string& title)
{
	std::string result = title;
	for (char& ch : result) {
		if (ch >= 'A' && ch <= 'Z') {
			ch = static_cast<char>(ch - 'A' + 'a');
		}
	}
	return result;
}

bool IsMd5Hex(const std::string& value)
{
	if (value.size() != 32) {
		return false;
	}
	for (char ch : value) {
		if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) {
			return false;
		}
	}
	return true;
}

std::string NormalizeImageContentType(const std::string& value)
{
	const size_t separator = value.find(';');
	std::string normalized = value.substr(0, separator);
	while (!normalized.empty() &&
		(normalized.back() == ' ' || normalized.back() == '\t')) {
		normalized.pop_back();
	}
	size_t first = 0;
	while (first < normalized.size() &&
		(normalized[first] == ' ' || normalized[first] == '\t')) {
		++first;
	}
	normalized.erase(0, first);
	std::transform(normalized.begin(), normalized.end(), normalized.begin(),
		[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
	return normalized;
}

bool IsSafeImageRelativePath(const std::string& value)
{
	const std::string prefix = "images/";
	if (value.compare(0, prefix.size(), prefix) != 0) {
		return false;
	}
	const std::string name = value.substr(prefix.size());
	const size_t dot = name.find('.');
	if (dot == 0 || dot == std::string::npos ||
		name.find('.', dot + 1) != std::string::npos) {
		return false;
	}
	for (size_t index = 0; index < dot; ++index) {
		if (name[index] < '0' || name[index] > '9') {
			return false;
		}
	}
	const std::string extension = name.substr(dot);
	return extension == ".png" || extension == ".jpg";
}

bool IsOwnedImageRelativePath(int64_t id, const std::string& value)
{
	if (id <= 0 || !IsSafeImageRelativePath(value)) {
		return false;
	}
	const std::string prefix = "images/" + std::to_string(id);
	return value == prefix + ".png" || value == prefix + ".jpg";
}

} // namespace

RaLibrary::RaLibrary() : db_(nullptr)
{
}

RaLibrary::~RaLibrary()
{
	Close();
}

bool RaLibrary::Open(const std::string& ra_root, std::string *error)
{
	Close();
	root_ = ra_root;

	if (!EnsureRaDirectoryTree(root_, error) ||
		!EnsureRaDirectoryTree(MediaRoot(), error) ||
		!EnsureRaDirectoryTree(TempRoot(), error) ||
		!EnsureRaDirectoryTree(JoinPath(root_, "images"), error) ||
		!EnsureRaDirectoryTree(JoinPath(root_, "states"), error)) {
		return false;
	}

	for (int attempt = 0; attempt < 2; attempt++) {
		const int rc = sqlite3_open_v2(DatabasePath().c_str(), &db_,
			SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
			nullptr);
		if (rc != SQLITE_OK) {
			if (error != nullptr) {
				*error = db_ != nullptr ? sqlite3_errmsg(db_) : "cannot open DB";
			}
			CloseDatabaseOnly();
			return false;
		}
		sqlite3_extended_result_codes(db_, 1);

		std::string setup_error;
		if (SetupDatabase(&setup_error)) {
			if (error != nullptr) {
				error->clear();
			}
			return true;
		}

		const bool can_retry = attempt == 0 && IsDatabaseDamage();
		CloseDatabaseOnly();
		if (!can_retry) {
			if (error != nullptr) {
				*error = setup_error;
			}
			root_.clear();
			return false;
		}
		if (!QuarantineDatabase(error)) {
			root_.clear();
			return false;
		}
	}

	if (error != nullptr) {
		*error = "cannot initialize RA library database";
	}
	root_.clear();
	return false;
}

void RaLibrary::Close()
{
	CloseDatabaseOnly();
	root_.clear();
}

void RaLibrary::CloseDatabaseOnly()
{
	if (db_ != nullptr) {
		sqlite3_close(db_);
		db_ = nullptr;
	}
}

std::string RaLibrary::MediaRoot() const
{
	return JoinPath(root_, "media");
}

std::string RaLibrary::TempRoot() const
{
	return JoinPath(root_, "temp");
}

std::string RaLibrary::DatabasePath() const
{
	return JoinPath(root_, "library.sqlite3");
}

std::string RaLibrary::PendingUnlockRecoveryPath() const
{
	return JoinPath(root_, "pending-unlock-recovery-required");
}

bool RaLibrary::Exec(const char *sql, std::string *error)
{
	char *message = nullptr;
	const int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &message);
	if (rc != SQLITE_OK) {
		if (error != nullptr) {
			*error = message != nullptr ? message : sqlite3_errmsg(db_);
		}
		sqlite3_free(message);
		return false;
	}
	return true;
}

bool RaLibrary::SetupDatabase(std::string *error)
{
	return Exec("PRAGMA foreign_keys = ON", error) &&
		Exec("PRAGMA journal_mode = WAL", error) &&
		Exec("PRAGMA synchronous = FULL", error) &&
		Exec("PRAGMA busy_timeout = 3000", error) &&
		CheckIntegrity(error) &&
		InitializeSchema(error) &&
		EnsureSettingsRow(error);
}

bool RaLibrary::CheckIntegrity(std::string *error)
{
	sqlite3_stmt *stmt = nullptr;
	if (!Prepare(db_, "PRAGMA integrity_check", &stmt, error)) {
		return false;
	}
	const int rc = sqlite3_step(stmt);
	if (rc != SQLITE_ROW) {
		if (error != nullptr) {
			*error = sqlite3_errmsg(db_);
		}
		sqlite3_finalize(stmt);
		return false;
	}
	const unsigned char *text = sqlite3_column_text(stmt, 0);
	const std::string result = text != nullptr ?
		reinterpret_cast<const char*>(text) : "";
	sqlite3_finalize(stmt);
	if (result == "ok") {
		return true;
	}
	if (error != nullptr) {
		*error = "RA library database integrity check failed: " + result;
	}
	return false;
}

bool RaLibrary::InitializeSchema(std::string *error)
{
	const char *schema =
		"BEGIN IMMEDIATE;"
		"CREATE TABLE IF NOT EXISTS schema_meta ("
		" singleton INTEGER PRIMARY KEY CHECK(singleton = 1),"
		" schema_version INTEGER NOT NULL,"
		" created_at INTEGER NOT NULL,"
		" migrated_at INTEGER NOT NULL"
		");"
		"CREATE TABLE IF NOT EXISTS ra_settings ("
		" singleton INTEGER PRIMARY KEY CHECK(singleton = 1),"
		" enabled INTEGER NOT NULL DEFAULT 0 CHECK(enabled IN (0, 1)),"
		" last_mode INTEGER NOT NULL DEFAULT 2 CHECK(last_mode IN (1, 2)),"
		" unofficial_enabled INTEGER NOT NULL DEFAULT 0 CHECK(unofficial_enabled IN (0, 1)),"
		" encore_enabled INTEGER NOT NULL DEFAULT 0 CHECK(encore_enabled IN (0, 1)),"
		" spectator_enabled INTEGER NOT NULL DEFAULT 0 CHECK(spectator_enabled IN (0, 1)),"
		" notification_seconds INTEGER NOT NULL DEFAULT 5 CHECK(notification_seconds IN (3, 5, 8)),"
		" image_cache_limit_mib INTEGER NOT NULL DEFAULT 128 CHECK(image_cache_limit_mib IN (64, 128, 256)),"
		" updated_at INTEGER NOT NULL"
		");"
		"CREATE TABLE IF NOT EXISTS games ("
		" id INTEGER PRIMARY KEY,"
		" ra_game_id INTEGER UNIQUE,"
		" title TEXT NOT NULL,"
		" sort_title TEXT NOT NULL,"
		" title_source INTEGER NOT NULL DEFAULT 0,"
		" badge_url TEXT,"
		" rich_presence TEXT,"
		" identification_state INTEGER NOT NULL DEFAULT 0,"
		" created_at INTEGER NOT NULL,"
		" updated_at INTEGER NOT NULL,"
		" last_played_at INTEGER,"
		" CHECK(ra_game_id IS NULL OR ra_game_id > 0),"
		" CHECK(title_source IN (0, 1, 2)),"
		" CHECK(identification_state BETWEEN 0 AND 4)"
		");"
		"CREATE TABLE IF NOT EXISTS media ("
		" md5 TEXT PRIMARY KEY,"
		" game_id INTEGER REFERENCES games(id) ON DELETE SET NULL,"
		" ra_game_id INTEGER,"
		" identification_state INTEGER NOT NULL DEFAULT 0,"
		" source_kind INTEGER NOT NULL,"
		" source_locator TEXT NOT NULL,"
		" source_display_name TEXT NOT NULL,"
		" source_size INTEGER NOT NULL,"
		" source_mtime INTEGER,"
		" bank_count INTEGER NOT NULL,"
		" working_relpath TEXT NOT NULL,"
		" ordinal INTEGER,"
		" health_state INTEGER NOT NULL DEFAULT 0,"
		" detached_at INTEGER,"
		" created_at INTEGER NOT NULL,"
		" verified_at INTEGER NOT NULL,"
		" CHECK(length(md5) = 32),"
		" CHECK(md5 NOT GLOB '*[^0-9a-f]*'),"
		" CHECK(ra_game_id IS NULL OR ra_game_id > 0),"
		" CHECK(identification_state BETWEEN 0 AND 4),"
		" CHECK(source_kind IN (0, 1)),"
		" CHECK(source_size > 0 AND source_size <= 1073741824),"
		" CHECK(bank_count > 0),"
		" CHECK(ordinal IS NULL OR ordinal >= 0),"
		" CHECK(health_state BETWEEN 0 AND 4),"
		" CHECK((game_id IS NULL AND ordinal IS NULL AND detached_at IS NOT NULL) OR"
		"       (game_id IS NOT NULL AND ordinal IS NOT NULL AND detached_at IS NULL)),"
		" UNIQUE(game_id, ordinal),"
		" UNIQUE(game_id, md5)"
		");"
		"CREATE TABLE IF NOT EXISTS media_banks ("
		" media_md5 TEXT NOT NULL REFERENCES media(md5) ON DELETE CASCADE,"
		" bank_index INTEGER NOT NULL,"
		" label TEXT NOT NULL,"
		" ra_hash TEXT,"
		" ra_game_id INTEGER,"
		" identification_state INTEGER NOT NULL DEFAULT 0,"
		" PRIMARY KEY(media_md5, bank_index),"
		" CHECK(bank_index >= 0),"
		" CHECK(ra_hash IS NULL OR (length(ra_hash) = 32 AND"
		"  ra_hash NOT GLOB '*[^0-9a-f]*')),"
		" CHECK(ra_game_id IS NULL OR ra_game_id > 0),"
		" CHECK(identification_state BETWEEN 0 AND 4)"
		");"
		"CREATE TABLE IF NOT EXISTS launch_profiles ("
		" game_id INTEGER NOT NULL REFERENCES games(id) ON DELETE CASCADE,"
		" drive INTEGER NOT NULL,"
		" media_md5 TEXT NOT NULL REFERENCES media(md5),"
		" bank_index INTEGER NOT NULL,"
		" is_ra_anchor INTEGER NOT NULL DEFAULT 0,"
		" PRIMARY KEY(game_id, drive),"
		" CHECK(drive IN (0, 1)),"
		" CHECK(is_ra_anchor IN (0, 1)),"
		" FOREIGN KEY(game_id, media_md5) REFERENCES media(game_id, md5),"
		" FOREIGN KEY(media_md5, bank_index) REFERENCES media_banks(media_md5, bank_index)"
		");"
		"CREATE UNIQUE INDEX IF NOT EXISTS one_ra_anchor_per_game "
		"ON launch_profiles(game_id) WHERE is_ra_anchor = 1;"
		"CREATE TABLE IF NOT EXISTS progress ("
		" username TEXT NOT NULL,"
		" ra_game_id INTEGER NOT NULL,"
		" core_total INTEGER NOT NULL,"
		" core_unlocked INTEGER NOT NULL,"
		" hardcore_unlocked INTEGER NOT NULL,"
		" points_total INTEGER,"
		" points_unlocked INTEGER,"
		" synced_at INTEGER NOT NULL,"
		" PRIMARY KEY(username, ra_game_id)"
		");"
		"CREATE TABLE IF NOT EXISTS image_cache ("
		" id INTEGER PRIMARY KEY,"
		" url TEXT NOT NULL UNIQUE,"
		" image_kind INTEGER NOT NULL,"
		" relative_path TEXT NOT NULL UNIQUE,"
		" content_type TEXT NOT NULL,"
		" byte_size INTEGER NOT NULL,"
		" last_used_at INTEGER NOT NULL,"
		" etag TEXT,"
		" modified TEXT,"
		" CHECK(image_kind BETWEEN 0 AND 3),"
		" CHECK(byte_size > 0)"
		");"
		"CREATE TABLE IF NOT EXISTS sync_state ("
		" sync_key TEXT PRIMARY KEY,"
		" completed_at INTEGER NOT NULL,"
		" result_code INTEGER NOT NULL"
		");"
		"CREATE TABLE IF NOT EXISTS pending_unlocks ("
		" id INTEGER PRIMARY KEY,"
		" account TEXT NOT NULL,"
		" achievement_id INTEGER NOT NULL CHECK(achievement_id > 0),"
		" hardcore INTEGER NOT NULL CHECK(hardcore IN (0, 1)),"
		" game_hash TEXT NOT NULL,"
		" unlocked_at INTEGER NOT NULL,"
		" status INTEGER NOT NULL DEFAULT 0 CHECK(status IN (0, 1)),"
		" attempt_count INTEGER NOT NULL DEFAULT 0,"
		" last_error TEXT NOT NULL DEFAULT '',"
		" UNIQUE(account, achievement_id, hardcore),"
		" CHECK(length(game_hash) = 32),"
		" CHECK(game_hash NOT GLOB '*[^0-9a-f]*')"
		");"
		"CREATE INDEX IF NOT EXISTS games_sort_title ON games(sort_title);"
		"CREATE INDEX IF NOT EXISTS games_last_played ON games(last_played_at DESC);"
		"CREATE INDEX IF NOT EXISTS media_game_order ON media(game_id, ordinal);";

	if (!Exec(schema, error)) {
		return false;
	}

	sqlite3_stmt *stmt = nullptr;
	if (!Prepare(db_, "SELECT schema_version FROM schema_meta WHERE singleton = 1",
		&stmt, error)) {
		Exec("ROLLBACK", nullptr);
		return false;
	}
	const int step = sqlite3_step(stmt);
	if (step == SQLITE_ROW) {
		const int version = sqlite3_column_int(stmt, 0);
		sqlite3_finalize(stmt);
		if (version == 1) {
			const char *migration =
				"ALTER TABLE media_banks ADD COLUMN ra_hash TEXT;"
				"ALTER TABLE media_banks ADD COLUMN ra_game_id INTEGER;"
				"ALTER TABLE media_banks ADD COLUMN identification_state"
				" INTEGER NOT NULL DEFAULT 0;"
				"UPDATE media_banks SET ra_hash = media_md5"
				" WHERE media_md5 IN (SELECT md5 FROM media WHERE bank_count = 1);"
				"UPDATE schema_meta SET schema_version = 2, migrated_at ="
				" strftime('%s','now') WHERE singleton = 1;";
			if (!Exec(migration, error)) {
				Exec("ROLLBACK", nullptr);
				return false;
			}
		}
		if (version == 1 || version == 2) {
			const char *migration =
				"CREATE TABLE IF NOT EXISTS pending_unlocks ("
				" id INTEGER PRIMARY KEY, account TEXT NOT NULL,"
				" achievement_id INTEGER NOT NULL CHECK(achievement_id > 0),"
				" hardcore INTEGER NOT NULL CHECK(hardcore IN (0, 1)),"
				" game_hash TEXT NOT NULL, unlocked_at INTEGER NOT NULL,"
				" status INTEGER NOT NULL DEFAULT 0 CHECK(status IN (0, 1)),"
				" attempt_count INTEGER NOT NULL DEFAULT 0,"
				" last_error TEXT NOT NULL DEFAULT '',"
				" UNIQUE(account, achievement_id, hardcore),"
				" CHECK(length(game_hash) = 32),"
				" CHECK(game_hash NOT GLOB '*[^0-9a-f]*'));"
				"UPDATE schema_meta SET schema_version = 4, migrated_at ="
				" strftime('%s','now') WHERE singleton = 1;";
			if (!Exec(migration, error)) {
				Exec("ROLLBACK", nullptr);
				return false;
			}
		}
		else if (version == 3) {
			const char *migration =
				"ALTER TABLE pending_unlocks ADD COLUMN status INTEGER"
				" NOT NULL DEFAULT 0 CHECK(status IN (0, 1));"
				"UPDATE schema_meta SET schema_version = 4, migrated_at ="
				" strftime('%s','now') WHERE singleton = 1;";
			if (!Exec(migration, error)) {
				Exec("ROLLBACK", nullptr);
				return false;
			}
		}
		else if (version != kSchemaVersion) {
			if (error != nullptr) {
				*error = "unsupported RA library schema version";
			}
			Exec("ROLLBACK", nullptr);
			return false;
		}
	}
	else if (step == SQLITE_DONE) {
		sqlite3_finalize(stmt);
		stmt = nullptr;
		if (!Prepare(db_,
			"INSERT INTO schema_meta(singleton, schema_version, created_at, migrated_at)"
			" VALUES(1, ?, ?, ?)",
			&stmt, error)) {
			Exec("ROLLBACK", nullptr);
			return false;
		}
		const int64_t now = NowUnixTime();
		sqlite3_bind_int(stmt, 1, kSchemaVersion);
		sqlite3_bind_int64(stmt, 2, now);
		sqlite3_bind_int64(stmt, 3, now);
		if (!StepDone(db_, stmt, error)) {
			Exec("ROLLBACK", nullptr);
			return false;
		}
	}
	else {
		if (error != nullptr) {
			*error = sqlite3_errmsg(db_);
		}
		sqlite3_finalize(stmt);
		Exec("ROLLBACK", nullptr);
		return false;
	}

	return Exec("COMMIT", error);
}

bool RaLibrary::EnsureSettingsRow(std::string *error)
{
	sqlite3_stmt *stmt = nullptr;
	if (!Prepare(db_,
		"INSERT OR IGNORE INTO ra_settings(singleton, last_mode, updated_at) "
		"VALUES(1, 2, ?)",
		&stmt, error)) {
		return false;
	}
	sqlite3_bind_int64(stmt, 1, NowUnixTime());
	return StepDone(db_, stmt, error);
}

bool RaLibrary::IsDatabaseDamage() const
{
	if (db_ == nullptr) {
		return false;
	}
	const int code = sqlite3_extended_errcode(db_);
	return code == SQLITE_CORRUPT || code == SQLITE_NOTADB ||
		(code & 0xff) == SQLITE_CORRUPT ||
		(code & 0xff) == SQLITE_NOTADB;
}

bool RaLibrary::QuarantineDatabase(std::string *error)
{
	const std::string base = DatabasePath();
	const std::string suffix = ".corrupt." + std::to_string(NowUnixTime());
	// Persist the fail-closed marker before moving any database file. If a
	// later rename fails or the process stops partway through quarantine, the
	// next launch must still require explicit recovery acknowledgement.
	if (!EnsurePendingUnlockRecoveryMarker(PendingUnlockRecoveryPath(), error)) {
		return false;
	}
	if (!RenameIfExists(base, base + suffix, error) ||
		!RenameIfExists(base + "-wal", base + "-wal" + suffix, error) ||
		!RenameIfExists(base + "-shm", base + "-shm" + suffix, error)) {
		return false;
	}
	return true;
}

int64_t RaLibrary::NowUnixTime() const
{
	return std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
}

bool RaLibrary::ValidateSettings(const RaSettings& settings,
	std::string *error) const
{
	if (settings.last_mode != kRaModeSoftcore &&
		settings.last_mode != kRaModeHardcore) {
		if (error != nullptr) {
			*error = "invalid RA mode";
		}
		return false;
	}
	if (settings.notification_seconds != 3 &&
		settings.notification_seconds != 5 &&
		settings.notification_seconds != 8) {
		if (error != nullptr) {
			*error = "invalid RA notification duration";
		}
		return false;
	}
	if (settings.image_cache_limit_mib != 64 &&
		settings.image_cache_limit_mib != 128 &&
		settings.image_cache_limit_mib != 256) {
		if (error != nullptr) {
			*error = "invalid RA image cache limit";
		}
		return false;
	}
	return true;
}

bool RaLibrary::LoadSettings(RaSettings *settings, std::string *error)
{
	if (settings == nullptr) {
		if (error != nullptr) {
			*error = "invalid argument";
		}
		return false;
	}

	sqlite3_stmt *stmt = nullptr;
	if (!Prepare(db_,
		"SELECT enabled, last_mode, unofficial_enabled, encore_enabled,"
		" spectator_enabled, notification_seconds, image_cache_limit_mib"
		" FROM ra_settings WHERE singleton = 1",
		&stmt, error)) {
		return false;
	}
	const int rc = sqlite3_step(stmt);
	if (rc != SQLITE_ROW) {
		if (error != nullptr) {
			*error = rc == SQLITE_DONE ? "RA settings row is missing" :
				sqlite3_errmsg(db_);
		}
		sqlite3_finalize(stmt);
		return false;
	}

	RaSettings loaded;
	loaded.enabled = sqlite3_column_int(stmt, 0) != 0;
	loaded.last_mode = sqlite3_column_int(stmt, 1);
	loaded.unofficial_enabled = sqlite3_column_int(stmt, 2) != 0;
	loaded.encore_enabled = sqlite3_column_int(stmt, 3) != 0;
	loaded.spectator_enabled = sqlite3_column_int(stmt, 4) != 0;
	loaded.notification_seconds = sqlite3_column_int(stmt, 5);
	loaded.image_cache_limit_mib = sqlite3_column_int(stmt, 6);
	sqlite3_finalize(stmt);

	if (!ValidateSettings(loaded, error)) {
		return false;
	}
	*settings = loaded;
	return true;
}

bool RaLibrary::SaveSettings(const RaSettings& settings, std::string *error)
{
	if (!ValidateSettings(settings, error)) {
		return false;
	}

	sqlite3_stmt *stmt = nullptr;
	if (!Prepare(db_,
		"UPDATE ra_settings SET enabled = ?, last_mode = ?,"
		" unofficial_enabled = ?, encore_enabled = ?, spectator_enabled = ?,"
		" notification_seconds = ?, image_cache_limit_mib = ?, updated_at = ?"
		" WHERE singleton = 1",
		&stmt, error)) {
		return false;
	}
	sqlite3_bind_int(stmt, 1, settings.enabled ? 1 : 0);
	sqlite3_bind_int(stmt, 2, settings.last_mode);
	sqlite3_bind_int(stmt, 3, settings.unofficial_enabled ? 1 : 0);
	sqlite3_bind_int(stmt, 4, settings.encore_enabled ? 1 : 0);
	sqlite3_bind_int(stmt, 5, settings.spectator_enabled ? 1 : 0);
	sqlite3_bind_int(stmt, 6, settings.notification_seconds);
	sqlite3_bind_int(stmt, 7, settings.image_cache_limit_mib);
	sqlite3_bind_int64(stmt, 8, NowUnixTime());
	return StepDone(db_, stmt, error);
}

bool RaLibrary::FindMedia(const std::string& md5, MediaRecord *record,
	std::string *error)
{
	sqlite3_stmt *stmt = nullptr;
	if (!Prepare(db_,
		"SELECT md5, game_id, working_relpath FROM media WHERE md5 = ?",
		&stmt, error)) {
		return false;
	}
	sqlite3_bind_text(stmt, 1, md5.c_str(), -1, SQLITE_TRANSIENT);
	const int rc = sqlite3_step(stmt);
	if (rc == SQLITE_DONE) {
		sqlite3_finalize(stmt);
		return false;
	}
	if (rc != SQLITE_ROW) {
		if (error != nullptr) {
			*error = sqlite3_errmsg(db_);
		}
		sqlite3_finalize(stmt);
		return false;
	}
	if (record != nullptr) {
		record->md5 = ColumnText(stmt, 0);
		record->game_id = sqlite3_column_int64(stmt, 1);
		record->working_relpath = ColumnText(stmt, 2);
		record->inserted = false;
	}
	sqlite3_finalize(stmt);
	return true;
}

bool RaLibrary::FindMediaByWorkingRelpath(const std::string& working_relpath,
	MediaRecord *record, std::string *error)
{
	if (working_relpath.empty() || record == nullptr) {
		if (error != nullptr) *error = "invalid working media lookup";
		return false;
	}

	sqlite3_stmt *stmt = nullptr;
	if (!Prepare(db_,
		"SELECT md5, game_id, working_relpath FROM media"
		" WHERE working_relpath = ?", &stmt, error)) {
		return false;
	}
	sqlite3_bind_text(stmt, 1, working_relpath.c_str(), -1, SQLITE_TRANSIENT);
	const int rc = sqlite3_step(stmt);
	if (rc != SQLITE_ROW) {
		if (error != nullptr) {
			*error = rc == SQLITE_DONE ? "working media is not registered" :
				sqlite3_errmsg(db_);
		}
		sqlite3_finalize(stmt);
		return false;
	}
	record->md5 = ColumnText(stmt, 0);
	record->game_id = sqlite3_column_int64(stmt, 1);
	record->working_relpath = ColumnText(stmt, 2);
	record->inserted = false;
	sqlite3_finalize(stmt);
	return true;
}

bool RaLibrary::LoadMediaBankHash(const std::string& media_md5,
	int bank_index, std::string *ra_hash, std::string *error)
{
	if (!IsMd5Hex(media_md5) || bank_index < 0 || ra_hash == nullptr) {
		if (error != nullptr) *error = "invalid media bank hash lookup";
		return false;
	}

	sqlite3_stmt *stmt = nullptr;
	if (!Prepare(db_,
		"SELECT ra_hash FROM media_banks WHERE media_md5 = ?"
		" AND bank_index = ?", &stmt, error)) {
		return false;
	}
	sqlite3_bind_text(stmt, 1, media_md5.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 2, bank_index);
	const int rc = sqlite3_step(stmt);
	if (rc != SQLITE_ROW || sqlite3_column_type(stmt, 0) == SQLITE_NULL) {
		if (error != nullptr) {
			*error = rc == SQLITE_ROW || rc == SQLITE_DONE ?
				"RA media bank hash is not available" : sqlite3_errmsg(db_);
		}
		sqlite3_finalize(stmt);
		return false;
	}
	*ra_hash = ColumnText(stmt, 0);
	sqlite3_finalize(stmt);
	if (!IsMd5Hex(*ra_hash)) {
		if (error != nullptr) *error = "invalid stored RA media bank hash";
		return false;
	}
	return true;
}

bool RaLibrary::LoadMediaHealthRecord(const std::string& md5,
	MediaHealthRecord *record, std::string *error)
{
	if (record == nullptr) {
		if (error != nullptr) {
			*error = "invalid argument";
		}
		return false;
	}

	sqlite3_stmt *stmt = nullptr;
	if (!Prepare(db_,
		"SELECT md5, game_id, source_locator, source_size, source_mtime,"
		" working_relpath, health_state FROM media WHERE md5 = ?",
		&stmt, error)) {
		return false;
	}
	sqlite3_bind_text(stmt, 1, md5.c_str(), -1, SQLITE_TRANSIENT);
	const int rc = sqlite3_step(stmt);
	if (rc == SQLITE_DONE) {
		sqlite3_finalize(stmt);
		if (error != nullptr) {
			*error = "media is not registered";
		}
		return false;
	}
	if (rc != SQLITE_ROW) {
		if (error != nullptr) {
			*error = sqlite3_errmsg(db_);
		}
		sqlite3_finalize(stmt);
		return false;
	}

	record->md5 = ColumnText(stmt, 0);
	record->game_id = sqlite3_column_int64(stmt, 1);
	record->source_locator = ColumnText(stmt, 2);
	record->source_size = sqlite3_column_int64(stmt, 3);
	record->source_mtime = sqlite3_column_type(stmt, 4) == SQLITE_NULL ?
		-1 : sqlite3_column_int64(stmt, 4);
	record->working_relpath = ColumnText(stmt, 5);
	record->health_state = sqlite3_column_int(stmt, 6);
	sqlite3_finalize(stmt);
	return true;
}

bool RaLibrary::UpdateMediaHealth(const MediaHealthStatus& status,
	std::string *error)
{
	if (status.md5.size() != 32 ||
		status.health_state < kRaMediaHealthOk ||
		status.health_state > kRaMediaHealthWorkingCorrupt) {
		if (error != nullptr) {
			*error = "invalid media health status";
		}
		return false;
	}

	sqlite3_stmt *stmt = nullptr;
	if (!Prepare(db_,
		"UPDATE media SET health_state = ?,"
		" source_size = CASE WHEN ? THEN ? ELSE source_size END,"
		" source_mtime = CASE WHEN ? THEN ? ELSE source_mtime END,"
		" verified_at = ? WHERE md5 = ?",
		&stmt, error)) {
		return false;
	}
	sqlite3_bind_int(stmt, 1, status.health_state);
	const bool update_source_metadata =
		status.source_exists && !status.source_hash_changed &&
		status.source_size > 0;
	sqlite3_bind_int(stmt, 2, update_source_metadata ? 1 : 0);
	sqlite3_bind_int64(stmt, 3,
		update_source_metadata ?
			static_cast<sqlite3_int64>(status.source_size) : 0);
	sqlite3_bind_int(stmt, 4, update_source_metadata ? 1 : 0);
	if (update_source_metadata && status.source_mtime >= 0) {
		sqlite3_bind_int64(stmt, 5, status.source_mtime);
	}
	else {
		sqlite3_bind_null(stmt, 5);
	}
	sqlite3_bind_int64(stmt, 6, NowUnixTime());
	sqlite3_bind_text(stmt, 7, status.md5.c_str(), -1, SQLITE_TRANSIENT);
	if (!StepDone(db_, stmt, error)) {
		return false;
	}
	if (sqlite3_changes(db_) != 1) {
		if (error != nullptr) {
			*error = "media is not registered";
		}
		return false;
	}
	return true;
}

bool RaLibrary::LoadLaunchProfile(int64_t game_id, LaunchProfile *profile,
	std::string *error)
{
	if (game_id <= 0 || profile == nullptr) {
		if (error != nullptr) {
			*error = "invalid argument";
		}
		return false;
	}

	LaunchProfile loaded;
	loaded.game_id = game_id;
	sqlite3_stmt *stmt = nullptr;
	if (!Prepare(db_,
		"SELECT drive, media_md5, bank_index, is_ra_anchor"
		" FROM launch_profiles WHERE game_id = ? ORDER BY drive",
		&stmt, error)) {
		return false;
	}
	sqlite3_bind_int64(stmt, 1, game_id);
	while (true) {
		const int rc = sqlite3_step(stmt);
		if (rc == SQLITE_DONE) {
			break;
		}
		if (rc != SQLITE_ROW) {
			if (error != nullptr) {
				*error = sqlite3_errmsg(db_);
			}
			sqlite3_finalize(stmt);
			return false;
		}
		const int drive = sqlite3_column_int(stmt, 0);
		if (drive < 0 || drive > 1) {
			sqlite3_finalize(stmt);
			if (error != nullptr) {
				*error = "invalid launch profile drive";
			}
			return false;
		}
		LaunchDrive& slot = loaded.drives[drive];
		slot.assigned = true;
		slot.media_md5 = ColumnText(stmt, 1);
		slot.bank_index = sqlite3_column_int(stmt, 2);
		slot.is_ra_anchor = sqlite3_column_int(stmt, 3) != 0;
	}
	sqlite3_finalize(stmt);
	*profile = loaded;
	return true;
}

bool RaLibrary::SaveLaunchProfile(const LaunchProfile& profile,
	std::string *error)
{
	if (profile.game_id <= 0) {
		if (error != nullptr) {
			*error = "invalid launch profile game";
		}
		return false;
	}

	int assigned_count = 0;
	int anchor_count = 0;
	for (int drive = 0; drive < 2; drive++) {
		const LaunchDrive& slot = profile.drives[drive];
		if (!slot.assigned) {
			continue;
		}
		assigned_count++;
		if (slot.media_md5.size() != 32 || slot.bank_index < 0) {
			if (error != nullptr) {
				*error = "invalid launch profile media";
			}
			return false;
		}
		if (slot.is_ra_anchor) {
			anchor_count++;
		}
	}
	if (assigned_count == 0 || anchor_count != 1) {
		if (error != nullptr) {
			*error = "launch profile must contain exactly one RA anchor";
		}
		return false;
	}

	if (!Exec("BEGIN IMMEDIATE", error)) {
		return false;
	}

	sqlite3_stmt *stmt = nullptr;
	if (!Prepare(db_, "DELETE FROM launch_profiles WHERE game_id = ?",
		&stmt, error)) {
		Exec("ROLLBACK", nullptr);
		return false;
	}
	sqlite3_bind_int64(stmt, 1, profile.game_id);
	if (!StepDone(db_, stmt, error)) {
		Exec("ROLLBACK", nullptr);
		return false;
	}

	for (int drive = 0; drive < 2; drive++) {
		const LaunchDrive& slot = profile.drives[drive];
		if (!slot.assigned) {
			continue;
		}
		if (!Prepare(db_,
			"INSERT INTO launch_profiles(game_id, drive, media_md5,"
			" bank_index, is_ra_anchor) VALUES(?, ?, ?, ?, ?)",
			&stmt, error)) {
			Exec("ROLLBACK", nullptr);
			return false;
		}
		sqlite3_bind_int64(stmt, 1, profile.game_id);
		sqlite3_bind_int(stmt, 2, drive);
		sqlite3_bind_text(stmt, 3, slot.media_md5.c_str(), -1,
			SQLITE_TRANSIENT);
		sqlite3_bind_int(stmt, 4, slot.bank_index);
		sqlite3_bind_int(stmt, 5, slot.is_ra_anchor ? 1 : 0);
		if (!StepDone(db_, stmt, error)) {
			Exec("ROLLBACK", nullptr);
			return false;
		}
	}

	if (!Prepare(db_,
		"SELECT COUNT(*) FROM launch_profiles"
		" WHERE game_id = ? AND is_ra_anchor = 1",
		&stmt, error)) {
		Exec("ROLLBACK", nullptr);
		return false;
	}
	sqlite3_bind_int64(stmt, 1, profile.game_id);
	const int rc = sqlite3_step(stmt);
	if (rc != SQLITE_ROW || sqlite3_column_int(stmt, 0) != 1) {
		if (error != nullptr) {
			*error = rc == SQLITE_ROW ? "launch profile anchor invariant failed" :
				sqlite3_errmsg(db_);
		}
		sqlite3_finalize(stmt);
		Exec("ROLLBACK", nullptr);
		return false;
	}
	sqlite3_finalize(stmt);

	return Exec("COMMIT", error);
}

bool RaLibrary::LoadGameIdentification(int64_t game_id,
	int64_t *ra_game_id, int *identification_state, std::string *error)
{
	if (game_id <= 0 || ra_game_id == nullptr ||
		identification_state == nullptr) {
		if (error != nullptr) {
			*error = "invalid game identification request";
		}
		return false;
	}

	sqlite3_stmt *stmt = nullptr;
	if (!Prepare(db_,
		"SELECT ra_game_id, identification_state FROM games WHERE id = ?",
		&stmt, error)) {
		return false;
	}
	sqlite3_bind_int64(stmt, 1, game_id);
	const int rc = sqlite3_step(stmt);
	if (rc != SQLITE_ROW) {
		if (error != nullptr) {
			*error = rc == SQLITE_DONE ? "game does not exist" :
				sqlite3_errmsg(db_);
		}
		sqlite3_finalize(stmt);
		return false;
	}
	*ra_game_id = sqlite3_column_type(stmt, 0) == SQLITE_NULL ?
		0 : sqlite3_column_int64(stmt, 0);
	*identification_state = sqlite3_column_int(stmt, 1);
	sqlite3_finalize(stmt);
	return true;
}

bool RaLibrary::InspectGameConflict(int64_t game_id,
	RaGameConflictInfo *info, std::string *error)
{
	if (game_id <= 0 || info == nullptr) {
		if (error != nullptr) {
			*error = "invalid game conflict request";
		}
		return false;
	}

	RaGameConflictInfo inspected;
	inspected.game_id = game_id;
	int64_t stored_ra_game_id = 0;
	int state = kRaIdentificationUnidentified;
	if (!LoadGameIdentification(game_id, &stored_ra_game_id, &state, error)) {
		return false;
	}
	if (state != kRaIdentificationConflict) {
		*info = inspected;
		return true;
	}

	sqlite3_stmt *stmt = nullptr;
	if (!Prepare(db_,
		"SELECT DISTINCT mb.ra_game_id FROM media m"
		" JOIN media_banks mb ON mb.media_md5 = m.md5"
		" WHERE m.game_id = ? AND mb.ra_game_id IS NOT NULL"
		" ORDER BY mb.ra_game_id",
		&stmt, error)) {
		return false;
	}
	sqlite3_bind_int64(stmt, 1, game_id);
	std::vector<int64_t> ids;
	while (true) {
		const int rc = sqlite3_step(stmt);
		if (rc == SQLITE_DONE) {
			break;
		}
		if (rc != SQLITE_ROW) {
			if (error != nullptr) {
				*error = sqlite3_errmsg(db_);
			}
			sqlite3_finalize(stmt);
			return false;
		}
		ids.push_back(sqlite3_column_int64(stmt, 0));
	}
	sqlite3_finalize(stmt);
	if (ids.empty()) {
		inspected.kind = RaGameConflictKind::Manual;
		*info = inspected;
		return true;
	}
	inspected.primary_ra_game_id = ids.front();

	if (ids.size() == 1) {
		if (!Prepare(db_,
			"SELECT COUNT(*) FROM ("
			" SELECT m.game_id FROM media m"
			" JOIN games g ON g.id = m.game_id"
			" JOIN media_banks mb ON mb.media_md5 = m.md5"
			" WHERE g.identification_state = 4"
			" GROUP BY m.game_id"
			" HAVING COUNT(DISTINCT mb.ra_game_id) = 1"
			" AND MIN(mb.ra_game_id) = ?"
			" AND MIN(mb.identification_state) = 1"
			" AND MAX(mb.identification_state) = 1)",
			&stmt, error)) {
			return false;
		}
		sqlite3_bind_int64(stmt, 1, ids.front());
		const int rc = sqlite3_step(stmt);
		if (rc == SQLITE_ROW) {
			inspected.related_game_count = sqlite3_column_int(stmt, 0);
		}
		else if (error != nullptr) {
			*error = sqlite3_errmsg(db_);
		}
		sqlite3_finalize(stmt);
		if (rc != SQLITE_ROW) {
			return false;
		}
		inspected.kind = inspected.related_game_count >= 2 ?
			RaGameConflictKind::Merge : RaGameConflictKind::Manual;
		inspected.resulting_game_count = 1;
		*info = inspected;
		return true;
	}

	// SPLIT is safe only when every physical D88 maps wholly to one RA ID.
	if (!Prepare(db_,
		"SELECT COUNT(*) FROM ("
		" SELECT m.md5 FROM media m"
		" JOIN media_banks mb ON mb.media_md5 = m.md5"
		" WHERE m.game_id = ? GROUP BY m.md5"
		" HAVING COUNT(DISTINCT mb.ra_game_id) != 1"
		" OR MIN(mb.identification_state) != 1"
		" OR MAX(mb.identification_state) != 1)",
		&stmt, error)) {
		return false;
	}
	sqlite3_bind_int64(stmt, 1, game_id);
	const int unsafe_rc = sqlite3_step(stmt);
	const int unsafe_media = unsafe_rc == SQLITE_ROW ?
		sqlite3_column_int(stmt, 0) : -1;
	sqlite3_finalize(stmt);
	if (unsafe_rc != SQLITE_ROW) {
		if (error != nullptr) {
			*error = sqlite3_errmsg(db_);
		}
		return false;
	}

	bool external_match = false;
	if (!Prepare(db_,
		"SELECT 1 FROM media m JOIN media_banks mb ON mb.media_md5 = m.md5"
		" WHERE m.game_id != ? AND mb.ra_game_id = ? LIMIT 1",
		&stmt, error)) {
		return false;
	}
	for (int64_t id : ids) {
		sqlite3_reset(stmt);
		sqlite3_clear_bindings(stmt);
		sqlite3_bind_int64(stmt, 1, game_id);
		sqlite3_bind_int64(stmt, 2, id);
		const int rc = sqlite3_step(stmt);
		if (rc == SQLITE_ROW) {
			external_match = true;
			break;
		}
		if (rc != SQLITE_DONE) {
			if (error != nullptr) *error = sqlite3_errmsg(db_);
			sqlite3_finalize(stmt);
			return false;
		}
	}
	sqlite3_finalize(stmt);
	inspected.kind = unsafe_media == 0 && !external_match ?
		RaGameConflictKind::Split : RaGameConflictKind::Manual;
	inspected.related_game_count = 1;
	inspected.resulting_game_count = static_cast<int>(ids.size());
	*info = inspected;
	return true;
}

bool RaLibrary::ResolveGameConflict(int64_t game_id,
	RaGameConflictInfo *result, std::string *error)
{
	RaGameConflictInfo conflict;
	if (!InspectGameConflict(game_id, &conflict, error)) {
		return false;
	}
	if (conflict.kind != RaGameConflictKind::Merge &&
		conflict.kind != RaGameConflictKind::Split) {
		if (error != nullptr) {
			*error = conflict.kind == RaGameConflictKind::Manual ?
				"media conflict requires manual configuration" :
				"game has no resolvable media conflict";
		}
		return false;
	}
	if (!Exec("BEGIN IMMEDIATE", error)) {
		return false;
	}
	auto rollback = [&]() {
		Exec("ROLLBACK", nullptr);
		return false;
	};
	RaGameConflictInfo locked_conflict;
	if (!InspectGameConflict(game_id, &locked_conflict, error)) {
		return rollback();
	}
	if (locked_conflict.kind != conflict.kind ||
		(locked_conflict.kind != RaGameConflictKind::Merge &&
		 locked_conflict.kind != RaGameConflictKind::Split)) {
		if (error != nullptr) *error = "media conflict changed before resolution";
		return rollback();
	}
	conflict = locked_conflict;
	sqlite3_stmt *stmt = nullptr;
	const int64_t now = NowUnixTime();

	if (conflict.kind == RaGameConflictKind::Merge) {
		std::vector<int64_t> game_ids;
		if (!Prepare(db_,
			"SELECT m.game_id FROM media m"
			" JOIN games g ON g.id = m.game_id"
			" JOIN media_banks mb ON mb.media_md5 = m.md5"
			" WHERE g.identification_state = 4"
			" GROUP BY m.game_id"
			" HAVING COUNT(DISTINCT mb.ra_game_id) = 1"
			" AND MIN(mb.ra_game_id) = ?"
			" AND MIN(mb.identification_state) = 1"
			" AND MAX(mb.identification_state) = 1"
			" ORDER BY m.game_id",
			&stmt, error)) {
			return rollback();
		}
		sqlite3_bind_int64(stmt, 1, conflict.primary_ra_game_id);
		while (true) {
			const int rc = sqlite3_step(stmt);
			if (rc == SQLITE_DONE) break;
			if (rc != SQLITE_ROW) {
				if (error != nullptr) *error = sqlite3_errmsg(db_);
				sqlite3_finalize(stmt);
				return rollback();
			}
			game_ids.push_back(sqlite3_column_int64(stmt, 0));
		}
		sqlite3_finalize(stmt);
		if (game_ids.size() < 2) {
			if (error != nullptr) *error = "merge conflict changed before resolution";
			return rollback();
		}
		const int64_t target = game_ids.front();
		int next_ordinal = 0;
		if (!Prepare(db_,
			"SELECT COALESCE(MAX(ordinal), -1) + 1 FROM media WHERE game_id = ?",
			&stmt, error)) return rollback();
		sqlite3_bind_int64(stmt, 1, target);
		if (sqlite3_step(stmt) != SQLITE_ROW) {
			if (error != nullptr) *error = sqlite3_errmsg(db_);
			sqlite3_finalize(stmt);
			return rollback();
		}
		next_ordinal = sqlite3_column_int(stmt, 0);
		sqlite3_finalize(stmt);
		for (size_t index = 1; index < game_ids.size(); ++index) {
			const int64_t source = game_ids[index];
			if (!Prepare(db_, "DELETE FROM launch_profiles WHERE game_id = ?",
				&stmt, error)) return rollback();
			sqlite3_bind_int64(stmt, 1, source);
			if (!StepDone(db_, stmt, error)) return rollback();

			std::vector<std::string> media;
			if (!Prepare(db_,
				"SELECT md5 FROM media WHERE game_id = ? ORDER BY ordinal, md5",
				&stmt, error)) return rollback();
			sqlite3_bind_int64(stmt, 1, source);
			while (true) {
				const int rc = sqlite3_step(stmt);
				if (rc == SQLITE_DONE) break;
				if (rc != SQLITE_ROW) {
					if (error != nullptr) *error = sqlite3_errmsg(db_);
					sqlite3_finalize(stmt);
					return rollback();
				}
				media.push_back(ColumnText(stmt, 0));
			}
			sqlite3_finalize(stmt);
			for (const std::string& md5 : media) {
				if (!Prepare(db_,
					"UPDATE media SET game_id = ?, ordinal = ?"
					" WHERE md5 = ? AND game_id = ?", &stmt, error)) {
					return rollback();
				}
				sqlite3_bind_int64(stmt, 1, target);
				sqlite3_bind_int(stmt, 2, next_ordinal++);
				sqlite3_bind_text(stmt, 3, md5.c_str(), -1, SQLITE_TRANSIENT);
				sqlite3_bind_int64(stmt, 4, source);
				if (!StepDone(db_, stmt, error)) return rollback();
				if (sqlite3_changes(db_) != 1) {
					if (error != nullptr) *error = "merge media ownership changed";
					return rollback();
				}
			}
			if (!Prepare(db_, "DELETE FROM games WHERE id = ?", &stmt, error))
				return rollback();
			sqlite3_bind_int64(stmt, 1, source);
			if (!StepDone(db_, stmt, error)) return rollback();
			if (sqlite3_changes(db_) != 1) {
				if (error != nullptr) *error = "merge source game changed";
				return rollback();
			}
		}
		if (!Prepare(db_,
			"UPDATE games SET ra_game_id = ?, identification_state = 1,"
			" updated_at = ? WHERE id = ?", &stmt, error)) return rollback();
		sqlite3_bind_int64(stmt, 1, conflict.primary_ra_game_id);
		sqlite3_bind_int64(stmt, 2, now);
		sqlite3_bind_int64(stmt, 3, target);
		if (!StepDone(db_, stmt, error)) return rollback();
		if (sqlite3_changes(db_) != 1) {
			if (error != nullptr) *error = "merge target game changed";
			return rollback();
		}
		conflict.game_id = target;
	}
	else {
		struct MediaGroup {
			std::string md5;
			std::string title;
			int ordinal = 0;
			int bank = 0;
			int64_t ra_game_id = 0;
		};
		std::vector<MediaGroup> media;
		if (!Prepare(db_,
			"SELECT m.md5, m.source_display_name, m.ordinal,"
			" MIN(mb.bank_index), MIN(mb.ra_game_id)"
			" FROM media m JOIN media_banks mb ON mb.media_md5 = m.md5"
			" WHERE m.game_id = ? GROUP BY m.md5"
			" ORDER BY m.ordinal, m.md5", &stmt, error)) return rollback();
		sqlite3_bind_int64(stmt, 1, game_id);
		while (true) {
			const int rc = sqlite3_step(stmt);
			if (rc == SQLITE_DONE) break;
			if (rc != SQLITE_ROW) {
				if (error != nullptr) *error = sqlite3_errmsg(db_);
				sqlite3_finalize(stmt);
				return rollback();
			}
			MediaGroup item;
			item.md5 = ColumnText(stmt, 0);
			item.title = ColumnText(stmt, 1);
			item.ordinal = sqlite3_column_int(stmt, 2);
			item.bank = sqlite3_column_int(stmt, 3);
			item.ra_game_id = sqlite3_column_int64(stmt, 4);
			media.push_back(item);
		}
		sqlite3_finalize(stmt);

		std::string anchor_md5;
		int anchor_bank = 0;
		if (!Prepare(db_,
			"SELECT media_md5, bank_index FROM launch_profiles"
			" WHERE game_id = ? AND is_ra_anchor = 1",
			&stmt, error)) return rollback();
		sqlite3_bind_int64(stmt, 1, game_id);
		if (sqlite3_step(stmt) != SQLITE_ROW) {
			if (error != nullptr) *error = "split conflict has no RA anchor";
			sqlite3_finalize(stmt);
			return rollback();
		}
		anchor_md5 = ColumnText(stmt, 0);
		anchor_bank = sqlite3_column_int(stmt, 1);
		sqlite3_finalize(stmt);
		int64_t anchor_ra_game_id = 0;
		for (const MediaGroup& item : media) {
			if (item.md5 == anchor_md5) anchor_ra_game_id = item.ra_game_id;
		}
		if (anchor_ra_game_id <= 0) {
			if (error != nullptr) *error = "split conflict anchor is ambiguous";
			return rollback();
		}

		if (!Prepare(db_, "DELETE FROM launch_profiles WHERE game_id = ?",
			&stmt, error)) return rollback();
		sqlite3_bind_int64(stmt, 1, game_id);
		if (!StepDone(db_, stmt, error)) return rollback();

		std::map<int64_t, int64_t> destination;
		destination[anchor_ra_game_id] = game_id;
		for (const MediaGroup& item : media) {
			if (destination.find(item.ra_game_id) != destination.end()) continue;
			if (!Prepare(db_,
				"INSERT INTO games(ra_game_id, title, sort_title, title_source,"
				" identification_state, created_at, updated_at)"
				" VALUES(?, ?, ?, 0, 1, ?, ?)", &stmt, error)) return rollback();
			sqlite3_bind_int64(stmt, 1, item.ra_game_id);
			sqlite3_bind_text(stmt, 2, item.title.c_str(), -1, SQLITE_TRANSIENT);
			const std::string sort_title = SortTitle(item.title);
			sqlite3_bind_text(stmt, 3, sort_title.c_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_int64(stmt, 4, now);
			sqlite3_bind_int64(stmt, 5, now);
			if (!StepDone(db_, stmt, error)) return rollback();
			if (sqlite3_changes(db_) != 1) {
				if (error != nullptr) *error = "split game creation failed";
				return rollback();
			}
			destination[item.ra_game_id] = sqlite3_last_insert_rowid(db_);
		}
		std::map<int64_t, int> ordinals;
		std::map<int64_t, MediaGroup> first_media;
		for (const MediaGroup& item : media) {
			const int64_t dest = destination[item.ra_game_id];
			if (!Prepare(db_, "UPDATE media SET game_id = ?, ordinal = ?"
				" WHERE md5 = ? AND game_id = ?", &stmt, error)) return rollback();
			sqlite3_bind_int64(stmt, 1, dest);
			sqlite3_bind_int(stmt, 2, ordinals[item.ra_game_id]++);
			sqlite3_bind_text(stmt, 3, item.md5.c_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_int64(stmt, 4, game_id);
			if (!StepDone(db_, stmt, error)) return rollback();
			if (sqlite3_changes(db_) != 1) {
				if (error != nullptr) *error = "split media ownership changed";
				return rollback();
			}
			if (first_media.find(item.ra_game_id) == first_media.end())
				first_media[item.ra_game_id] = item;
		}
		if (!Prepare(db_,
			"UPDATE games SET ra_game_id = ?, identification_state = 1,"
			" updated_at = ? WHERE id = ?", &stmt, error)) return rollback();
		sqlite3_bind_int64(stmt, 1, anchor_ra_game_id);
		sqlite3_bind_int64(stmt, 2, now);
		sqlite3_bind_int64(stmt, 3, game_id);
		if (!StepDone(db_, stmt, error)) return rollback();
		if (sqlite3_changes(db_) != 1) {
			if (error != nullptr) *error = "split source game changed";
			return rollback();
		}
		for (const auto& entry : destination) {
			const int64_t id = entry.first;
			const int64_t dest = entry.second;
			const MediaGroup& anchor = id == anchor_ra_game_id ?
				*std::find_if(media.begin(), media.end(), [&](const MediaGroup& item) {
					return item.md5 == anchor_md5;
				}) : first_media[id];
			const int bank = id == anchor_ra_game_id ? anchor_bank : anchor.bank;
			if (!Prepare(db_,
				"INSERT INTO launch_profiles(game_id, drive, media_md5,"
				" bank_index, is_ra_anchor) VALUES(?, 0, ?, ?, 1)",
				&stmt, error)) return rollback();
			sqlite3_bind_int64(stmt, 1, dest);
			sqlite3_bind_text(stmt, 2, anchor.md5.c_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_int(stmt, 3, bank);
			if (!StepDone(db_, stmt, error)) return rollback();
		}
	}

	if (!Exec("COMMIT", error)) {
		return rollback();
	}
	if (result != nullptr) {
		*result = conflict;
	}
	return true;
}

bool RaLibrary::MergeGameMedia(int64_t target_game_id,
	int64_t source_game_id, std::string *error)
{
	if (target_game_id <= 0 || source_game_id <= 0 ||
		target_game_id == source_game_id) {
		if (error != nullptr) {
			*error = "invalid game merge target";
		}
		return false;
	}
	if (!Exec("BEGIN IMMEDIATE", error)) {
		return false;
	}

	sqlite3_stmt *stmt = nullptr;
	if (!Prepare(db_, "SELECT 1 FROM games WHERE id = ?",
		&stmt, error)) {
		Exec("ROLLBACK", nullptr);
		return false;
	}
	sqlite3_bind_int64(stmt, 1, target_game_id);
	const int target_rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	if (target_rc != SQLITE_ROW) {
		if (error != nullptr) {
			*error = "target game does not exist";
		}
		Exec("ROLLBACK", nullptr);
		return false;
	}

	if (!Prepare(db_, "SELECT 1 FROM games WHERE id = ?",
		&stmt, error)) {
		Exec("ROLLBACK", nullptr);
		return false;
	}
	sqlite3_bind_int64(stmt, 1, source_game_id);
	const int source_rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	if (source_rc != SQLITE_ROW) {
		if (error != nullptr) {
			*error = "source game does not exist";
		}
		Exec("ROLLBACK", nullptr);
		return false;
	}

	int max_ordinal = -1;
	if (!Prepare(db_,
		"SELECT COALESCE(MAX(ordinal), -1) FROM media WHERE game_id = ?",
		&stmt, error)) {
		Exec("ROLLBACK", nullptr);
		return false;
	}
	sqlite3_bind_int64(stmt, 1, target_game_id);
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		max_ordinal = sqlite3_column_int(stmt, 0);
	}
	else {
		if (error != nullptr) {
			*error = sqlite3_errmsg(db_);
		}
		sqlite3_finalize(stmt);
		Exec("ROLLBACK", nullptr);
		return false;
	}
	sqlite3_finalize(stmt);

	std::vector<std::string> media_md5s;
	if (!Prepare(db_,
		"SELECT md5 FROM media WHERE game_id = ? ORDER BY ordinal, md5",
		&stmt, error)) {
		Exec("ROLLBACK", nullptr);
		return false;
	}
	sqlite3_bind_int64(stmt, 1, source_game_id);
	while (true) {
		const int rc = sqlite3_step(stmt);
		if (rc == SQLITE_DONE) {
			break;
		}
		if (rc != SQLITE_ROW) {
			if (error != nullptr) {
				*error = sqlite3_errmsg(db_);
			}
			sqlite3_finalize(stmt);
			Exec("ROLLBACK", nullptr);
			return false;
		}
		media_md5s.push_back(
			reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
	}
	sqlite3_finalize(stmt);
	if (media_md5s.empty()) {
		if (error != nullptr) {
			*error = "source game has no media";
		}
		Exec("ROLLBACK", nullptr);
		return false;
	}

	if (!Prepare(db_, "DELETE FROM launch_profiles WHERE game_id = ?",
		&stmt, error)) {
		Exec("ROLLBACK", nullptr);
		return false;
	}
	sqlite3_bind_int64(stmt, 1, source_game_id);
	if (!StepDone(db_, stmt, error)) {
		Exec("ROLLBACK", nullptr);
		return false;
	}

	for (size_t i = 0; i < media_md5s.size(); i++) {
		if (!Prepare(db_,
			"UPDATE media SET game_id = ?, ordinal = ?, detached_at = NULL"
			" WHERE md5 = ? AND game_id = ?",
			&stmt, error)) {
			Exec("ROLLBACK", nullptr);
			return false;
		}
		sqlite3_bind_int64(stmt, 1, target_game_id);
		sqlite3_bind_int(stmt, 2,
			max_ordinal + 1 + static_cast<int>(i));
		sqlite3_bind_text(stmt, 3, media_md5s[i].c_str(), -1,
			SQLITE_TRANSIENT);
		sqlite3_bind_int64(stmt, 4, source_game_id);
		if (!StepDone(db_, stmt, error)) {
			Exec("ROLLBACK", nullptr);
			return false;
		}
	}

	if (!Prepare(db_, "DELETE FROM games WHERE id = ?",
		&stmt, error)) {
		Exec("ROLLBACK", nullptr);
		return false;
	}
	sqlite3_bind_int64(stmt, 1, source_game_id);
	if (!StepDone(db_, stmt, error)) {
		Exec("ROLLBACK", nullptr);
		return false;
	}

	return Exec("COMMIT", error);
}

bool RaLibrary::ListGames(std::vector<RaLibraryGameListItem> *games,
	std::string *error)
{
	return ListGamesForUser("*", games, error);
}

bool RaLibrary::ListGamesForUser(const std::string& username,
	std::vector<RaLibraryGameListItem> *games, std::string *error)
{
	if (games == nullptr) {
		if (error != nullptr) {
			*error = "invalid argument";
		}
		return false;
	}
	games->clear();

	sqlite3_stmt *stmt = nullptr;
	if (!Prepare(db_,
		"SELECT g.id, g.ra_game_id, g.title, g.badge_url,"
		" g.identification_state, COUNT(DISTINCT m.md5),"
		" COALESCE(MAX(m.health_state), 0), g.last_played_at,"
		" MAX(p.core_total), MAX(p.core_unlocked),"
		" MAX(p.hardcore_unlocked), MAX(p.points_total),"
		" MAX(p.points_unlocked)"
		" FROM games g"
		" LEFT JOIN media m ON m.game_id = g.id"
		" LEFT JOIN progress p ON p.ra_game_id = g.ra_game_id"
		"  AND (? = '*' OR p.username = ?)"
		" WHERE g.identification_state IN (1, 4)"
		" GROUP BY g.id"
		" ORDER BY g.last_played_at IS NULL, g.last_played_at DESC,"
		" g.sort_title ASC, g.id ASC",
		&stmt, error)) {
		return false;
	}
	sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 2, username.c_str(), -1, SQLITE_TRANSIENT);

	while (true) {
		const int rc = sqlite3_step(stmt);
		if (rc == SQLITE_DONE) {
			break;
		}
		if (rc != SQLITE_ROW) {
			if (error != nullptr) {
				*error = sqlite3_errmsg(db_);
			}
			sqlite3_finalize(stmt);
			return false;
		}

		RaLibraryGameListItem item;
		item.game_id = sqlite3_column_int64(stmt, 0);
		item.ra_game_id = sqlite3_column_type(stmt, 1) == SQLITE_NULL ?
			0 : sqlite3_column_int64(stmt, 1);
		item.title = ColumnText(stmt, 2);
		item.badge_url = ColumnText(stmt, 3);
		item.identification_state = sqlite3_column_int(stmt, 4);
		item.media_count = sqlite3_column_int(stmt, 5);
		item.health_state = sqlite3_column_int(stmt, 6);
		item.last_played_at = sqlite3_column_type(stmt, 7) == SQLITE_NULL ?
			0 : sqlite3_column_int64(stmt, 7);
		item.has_progress = sqlite3_column_type(stmt, 8) != SQLITE_NULL;
		if (item.has_progress) {
			item.core_total = sqlite3_column_int(stmt, 8);
			item.core_unlocked = sqlite3_column_int(stmt, 9);
			item.hardcore_unlocked = sqlite3_column_int(stmt, 10);
			item.points_total = sqlite3_column_type(stmt, 11) == SQLITE_NULL ?
				0 : sqlite3_column_int(stmt, 11);
			item.points_unlocked = sqlite3_column_type(stmt, 12) == SQLITE_NULL ?
				0 : sqlite3_column_int(stmt, 12);
		}
		games->push_back(item);
	}
	sqlite3_finalize(stmt);
	return true;
}

bool RaLibrary::MarkGameIdentified(int64_t game_id, int64_t ra_game_id,
	const std::string& title, const std::string& badge_url,
	std::string *error)
{
	if (game_id <= 0 || ra_game_id <= 0) {
		if (error != nullptr) {
			*error = "invalid RA game";
		}
		return false;
	}

	const std::string saved_title =
		title.empty() ? "RA Game " + std::to_string(ra_game_id) : title;
	const std::string sort_title = SortTitle(saved_title);

	if (!Exec("BEGIN IMMEDIATE", error)) {
		return false;
	}

	sqlite3_stmt *stmt = nullptr;
	if (!Prepare(db_,
		"UPDATE games SET ra_game_id = ?,"
		" title = CASE WHEN title_source = 2 THEN title ELSE ? END,"
		" sort_title = CASE WHEN title_source = 2 THEN sort_title ELSE ? END,"
		" title_source = CASE WHEN title_source = 2 THEN title_source ELSE 1 END,"
		" badge_url = ?, identification_state = 1, updated_at = ?"
		" WHERE id = ?",
		&stmt, error)) {
		Exec("ROLLBACK", nullptr);
		return false;
	}
	const int64_t now = NowUnixTime();
	sqlite3_bind_int64(stmt, 1, ra_game_id);
	sqlite3_bind_text(stmt, 2, saved_title.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 3, sort_title.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 4, badge_url.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int64(stmt, 5, now);
	sqlite3_bind_int64(stmt, 6, game_id);
	if (!StepDone(db_, stmt, error)) {
		Exec("ROLLBACK", nullptr);
		return false;
	}
	if (sqlite3_changes(db_) != 1) {
		Exec("ROLLBACK", nullptr);
		if (error != nullptr) {
			*error = "game is not registered";
		}
		return false;
	}

	if (!Prepare(db_,
		"UPDATE media SET ra_game_id = ?, identification_state = 1,"
		" verified_at = ? WHERE game_id = ?",
		&stmt, error)) {
		Exec("ROLLBACK", nullptr);
		return false;
	}
	sqlite3_bind_int64(stmt, 1, ra_game_id);
	sqlite3_bind_int64(stmt, 2, now);
	sqlite3_bind_int64(stmt, 3, game_id);
	if (!StepDone(db_, stmt, error)) {
		Exec("ROLLBACK", nullptr);
		return false;
	}

	return Exec("COMMIT", error);
}

bool RaLibrary::MarkGamePlayed(int64_t game_id, std::string *error)
{
	if (game_id <= 0) {
		if (error != nullptr) {
			*error = "invalid game";
		}
		return false;
	}

	sqlite3_stmt *stmt = nullptr;
	if (!Prepare(db_,
		"UPDATE games SET last_played_at = ?, updated_at = ? WHERE id = ?",
		&stmt, error)) {
		return false;
	}
	const int64_t now = NowUnixTime();
	sqlite3_bind_int64(stmt, 1, now);
	sqlite3_bind_int64(stmt, 2, now);
	sqlite3_bind_int64(stmt, 3, game_id);
	if (!StepDone(db_, stmt, error)) {
		return false;
	}
	if (sqlite3_changes(db_) != 1) {
		if (error != nullptr) {
			*error = "game is not registered";
		}
		return false;
	}
	return true;
}

bool RaLibrary::RegisterDesktopMedia(const D88MediaInfo& media,
	const std::string& source_path, const std::string& display_name,
	int64_t source_mtime, MediaRecord *record, std::string *error)
{
	return RegisterDesktopMediaInternal(media, source_path, display_name,
		source_mtime, 0, 0, true, record, error);
}

bool RaLibrary::RegisterDesktopMediaInGame(const D88MediaInfo& media,
	const std::string& source_path, const std::string& display_name,
	int64_t source_mtime, int64_t game_id, int ordinal,
	MediaRecord *record, std::string *error)
{
	if (game_id <= 0 || ordinal <= 0) {
		if (error != nullptr) {
			*error = "invalid grouped media target";
		}
		return false;
	}
	return RegisterDesktopMediaInternal(media, source_path, display_name,
		source_mtime, game_id, ordinal, false, record, error);
}

bool RaLibrary::RegisterDesktopMediaInternal(const D88MediaInfo& media,
	const std::string& source_path, const std::string& display_name,
	int64_t source_mtime, int64_t game_id, int ordinal,
	bool create_anchor_profile, MediaRecord *record, std::string *error)
{
	if (media.md5.size() != 32 || media.banks <= 0) {
		if (error != nullptr) {
			*error = "invalid media metadata";
		}
		return false;
	}

	MediaRecord existing;
	if (FindMedia(media.md5, &existing, nullptr)) {
		if (game_id > 0 && existing.game_id != game_id) {
			if (error != nullptr) {
				*error = "media is already registered to another game";
			}
			return false;
		}
		if (!UpdateMediaBankHashes(media, error)) {
			return false;
		}
		if (record != nullptr) {
			*record = existing;
		}
		return true;
	}

	if (!Exec("BEGIN IMMEDIATE", error)) {
		return false;
	}

	const int64_t now = NowUnixTime();
	if (game_id == 0) {
		std::string title =
			!media.bank_names.empty() && !media.bank_names[0].empty() ?
			media.bank_names[0] : display_name;
		if (title.empty()) {
			title = media.md5;
		}

		sqlite3_stmt *stmt = nullptr;
		if (!Prepare(db_,
			"INSERT INTO games(title, sort_title, title_source, identification_state,"
			" created_at, updated_at) VALUES(?, ?, 0, 0, ?, ?)",
			&stmt, error)) {
			Exec("ROLLBACK", nullptr);
			return false;
		}
		sqlite3_bind_text(stmt, 1, title.c_str(), -1, SQLITE_TRANSIENT);
		const std::string sort_title = SortTitle(title);
		sqlite3_bind_text(stmt, 2, sort_title.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_int64(stmt, 3, now);
		sqlite3_bind_int64(stmt, 4, now);
		if (!StepDone(db_, stmt, error)) {
			Exec("ROLLBACK", nullptr);
			return false;
		}
		game_id = sqlite3_last_insert_rowid(db_);
	}

	const std::string working_relpath =
		std::string("media/") + media.md5 + "/working.d88";
	sqlite3_stmt *stmt = nullptr;
	if (!Prepare(db_,
		"INSERT INTO media(md5, game_id, identification_state, source_kind,"
		" source_locator, source_display_name, source_size, source_mtime,"
		" bank_count, working_relpath, ordinal, health_state, created_at, verified_at)"
		" VALUES(?, ?, 0, 0, ?, ?, ?, ?, ?, ?, ?, 0, ?, ?)",
		&stmt, error)) {
		Exec("ROLLBACK", nullptr);
		return false;
	}
	sqlite3_bind_text(stmt, 1, media.md5.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int64(stmt, 2, game_id);
	sqlite3_bind_text(stmt, 3, source_path.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 4, display_name.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(media.size));
	if (source_mtime >= 0) {
		sqlite3_bind_int64(stmt, 6, source_mtime);
	}
	else {
		sqlite3_bind_null(stmt, 6);
	}
	sqlite3_bind_int(stmt, 7, media.banks);
	sqlite3_bind_text(stmt, 8, working_relpath.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 9, ordinal);
	sqlite3_bind_int64(stmt, 10, now);
	sqlite3_bind_int64(stmt, 11, now);
	if (!StepDone(db_, stmt, error)) {
		Exec("ROLLBACK", nullptr);
		return false;
	}

	for (int i = 0; i < media.banks; i++) {
		if (!Prepare(db_,
			"INSERT INTO media_banks(media_md5, bank_index, label, ra_hash)"
			" VALUES(?, ?, ?, ?)",
			&stmt, error)) {
			Exec("ROLLBACK", nullptr);
			return false;
		}
		const std::string label =
			i < static_cast<int>(media.bank_names.size()) ? media.bank_names[i] : "";
		sqlite3_bind_text(stmt, 1, media.md5.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_int(stmt, 2, i);
		sqlite3_bind_text(stmt, 3, label.c_str(), -1, SQLITE_TRANSIENT);
		const std::string hash =
			i < static_cast<int>(media.bank_md5s.size()) ?
			media.bank_md5s[i] : "";
		if (hash.size() == 32) {
			sqlite3_bind_text(stmt, 4, hash.c_str(), -1, SQLITE_TRANSIENT);
		}
		else {
			sqlite3_bind_null(stmt, 4);
		}
		if (!StepDone(db_, stmt, error)) {
			Exec("ROLLBACK", nullptr);
			return false;
		}
	}

	if (create_anchor_profile) {
		if (!Prepare(db_,
			"INSERT INTO launch_profiles(game_id, drive, media_md5, bank_index, is_ra_anchor)"
			" VALUES(?, 0, ?, 0, 1)",
			&stmt, error)) {
			Exec("ROLLBACK", nullptr);
			return false;
		}
		sqlite3_bind_int64(stmt, 1, game_id);
		sqlite3_bind_text(stmt, 2, media.md5.c_str(), -1, SQLITE_TRANSIENT);
		if (!StepDone(db_, stmt, error)) {
			Exec("ROLLBACK", nullptr);
			return false;
		}
	}

	if (!Exec("COMMIT", error)) {
		return false;
	}

	if (record != nullptr) {
		record->md5 = media.md5;
		record->game_id = game_id;
		record->working_relpath = working_relpath;
		record->inserted = true;
	}
	return true;
}

bool RaLibrary::UpdateMediaBankHashes(const D88MediaInfo& media,
	std::string *error)
{
	if (!IsMd5Hex(media.md5) || media.banks <= 0 ||
		media.bank_md5s.size() != static_cast<size_t>(media.banks)) {
		if (error != nullptr) {
			*error = "invalid media bank hashes";
		}
		return false;
	}
	for (const std::string& hash : media.bank_md5s) {
		if (!IsMd5Hex(hash)) {
			if (error != nullptr) {
				*error = "invalid RA bank hash";
			}
			return false;
		}
	}
	if (!Exec("BEGIN IMMEDIATE", error)) {
		return false;
	}

	sqlite3_stmt *stmt = nullptr;
	for (int bank = 0; bank < media.banks; bank++) {
		if (!Prepare(db_,
			"UPDATE media_banks SET ra_hash = ?"
			" WHERE media_md5 = ? AND bank_index = ?",
			&stmt, error)) {
			Exec("ROLLBACK", nullptr);
			return false;
		}
		sqlite3_bind_text(stmt, 1, media.bank_md5s[bank].c_str(), -1,
			SQLITE_TRANSIENT);
		sqlite3_bind_text(stmt, 2, media.md5.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_int(stmt, 3, bank);
		if (!StepDone(db_, stmt, error) || sqlite3_changes(db_) != 1) {
			if (error != nullptr && error->empty()) {
				*error = "media bank is not registered";
			}
			Exec("ROLLBACK", nullptr);
			return false;
		}
	}
	return Exec("COMMIT", error);
}

bool RaLibrary::ListMediaBankHashes(std::vector<RaMediaBankHash> *hashes,
	std::string *error)
{
	if (hashes == nullptr) {
		if (error != nullptr) {
			*error = "invalid argument";
		}
		return false;
	}
	hashes->clear();
	sqlite3_stmt *stmt = nullptr;
	if (!Prepare(db_,
		"SELECT media_md5, bank_index, ra_hash FROM media_banks"
		" WHERE ra_hash IS NOT NULL ORDER BY media_md5, bank_index",
		&stmt, error)) {
		return false;
	}
	while (true) {
		const int rc = sqlite3_step(stmt);
		if (rc == SQLITE_DONE) {
			break;
		}
		if (rc != SQLITE_ROW) {
			if (error != nullptr) {
				*error = sqlite3_errmsg(db_);
			}
			sqlite3_finalize(stmt);
			return false;
		}
		RaMediaBankHash item;
		item.media_md5 = ColumnText(stmt, 0);
		item.bank_index = sqlite3_column_int(stmt, 1);
		item.ra_hash = ColumnText(stmt, 2);
		if (!IsMd5Hex(item.ra_hash)) {
			if (error != nullptr) {
				*error = "invalid stored RA bank hash";
			}
			sqlite3_finalize(stmt);
			return false;
		}
		hashes->push_back(item);
	}
	sqlite3_finalize(stmt);
	return true;
}

bool RaLibrary::ApplyLibrarySync(const RaLibrarySyncPayload& payload,
	std::string *error)
{
	if (payload.username.empty()) {
		if (error != nullptr) {
			*error = "library sync username is required";
		}
		return false;
	}

	std::map<std::string, uint32_t> hash_map;
	std::set<uint32_t> matched_game_ids;
	for (const RaLibraryHashMatch& item : payload.hashes) {
		if (!IsMd5Hex(item.hash) || item.ra_game_id == 0 ||
			!hash_map.emplace(item.hash, item.ra_game_id).second) {
			if (error != nullptr) {
				*error = "invalid or duplicate library hash result";
			}
			return false;
		}
		matched_game_ids.insert(item.ra_game_id);
	}

	std::map<uint32_t, RaLibraryGameTitle> title_map;
	for (const RaLibraryGameTitle& item : payload.titles) {
		if (item.ra_game_id == 0 || item.title.empty() ||
			!title_map.emplace(item.ra_game_id, item).second) {
			if (error != nullptr) {
				*error = "invalid or duplicate game title result";
			}
			return false;
		}
	}
	for (uint32_t game_id : matched_game_ids) {
		if (title_map.find(game_id) == title_map.end()) {
			if (error != nullptr) {
				*error = "game title result is incomplete";
			}
			return false;
		}
	}

	std::set<uint32_t> progress_ids;
	for (const RaLibraryProgress& item : payload.progress) {
		if (item.ra_game_id == 0 || item.core_unlocked > item.core_total ||
			item.hardcore_unlocked > item.core_total ||
			!progress_ids.insert(item.ra_game_id).second) {
			if (error != nullptr) {
				*error = "invalid or duplicate progress result";
			}
			return false;
		}
	}

	if (!Exec("BEGIN IMMEDIATE", error)) {
		return false;
	}
	auto rollback = [&]() {
		Exec("ROLLBACK", nullptr);
		return false;
	};
	const int64_t now = NowUnixTime();
	sqlite3_stmt *stmt = nullptr;
	if (!Prepare(db_, "SELECT media_md5, bank_index, ra_hash FROM media_banks",
		&stmt, error)) {
		return rollback();
	}
	struct BankUpdate {
		std::string media_md5;
		int bank = 0;
		uint32_t game_id = 0;
		bool has_hash = false;
	};
	std::vector<BankUpdate> bank_updates;
	while (true) {
		const int rc = sqlite3_step(stmt);
		if (rc == SQLITE_DONE) {
			break;
		}
		if (rc != SQLITE_ROW) {
			if (error != nullptr) {
				*error = sqlite3_errmsg(db_);
			}
			sqlite3_finalize(stmt);
			return rollback();
		}
		BankUpdate update;
		update.media_md5 = ColumnText(stmt, 0);
		update.bank = sqlite3_column_int(stmt, 1);
		const std::string hash = ColumnText(stmt, 2);
		update.has_hash = IsMd5Hex(hash);
		const auto match = hash_map.find(hash);
		if (match != hash_map.end()) {
			update.game_id = match->second;
		}
		bank_updates.push_back(update);
	}
	sqlite3_finalize(stmt);

	for (const BankUpdate& update : bank_updates) {
		if (!Prepare(db_,
			"UPDATE media_banks SET ra_game_id = ?, identification_state = ?"
			" WHERE media_md5 = ? AND bank_index = ?",
			&stmt, error)) {
			return rollback();
		}
		if (update.game_id != 0) {
			sqlite3_bind_int64(stmt, 1, update.game_id);
		}
		else {
			sqlite3_bind_null(stmt, 1);
		}
		sqlite3_bind_int(stmt, 2,
			!update.has_hash ? 0 : (update.game_id != 0 ? 1 : 2));
		sqlite3_bind_text(stmt, 3, update.media_md5.c_str(), -1,
			SQLITE_TRANSIENT);
		sqlite3_bind_int(stmt, 4, update.bank);
		if (!StepDone(db_, stmt, error)) {
			return rollback();
		}
	}

	const char *aggregate_media =
		"UPDATE media SET"
		" ra_game_id = (SELECT CASE WHEN COUNT(DISTINCT ra_game_id) = 1"
		"  THEN MIN(ra_game_id) ELSE NULL END FROM media_banks"
		"  WHERE media_md5 = media.md5 AND ra_game_id IS NOT NULL),"
		" identification_state = (SELECT CASE"
		"  WHEN SUM(CASE WHEN ra_hash IS NULL THEN 1 ELSE 0 END) > 0 THEN 0"
		"  WHEN COUNT(DISTINCT ra_game_id) = 0 THEN 2"
		"  WHEN COUNT(DISTINCT ra_game_id) = 1 THEN 1 ELSE 4 END"
		"  FROM media_banks WHERE media_md5 = media.md5)";
	if (!Exec(aggregate_media, error)) {
		return rollback();
	}

	struct GameUpdate {
		int64_t local_game_id = 0;
		uint32_t ra_game_id = 0;
		int distinct_ids = 0;
		int anchor_state = 0;
	};
	std::vector<GameUpdate> game_updates;
	if (!Prepare(db_,
		"SELECT g.id, mb.ra_game_id,"
		" (SELECT COUNT(DISTINCT mb2.ra_game_id) FROM media m2"
		"  JOIN media_banks mb2 ON mb2.media_md5 = m2.md5"
		"  WHERE m2.game_id = g.id AND mb2.ra_game_id IS NOT NULL),"
		" mb.identification_state"
		" FROM games g"
		" LEFT JOIN launch_profiles lp ON lp.game_id = g.id"
		"  AND lp.is_ra_anchor = 1"
		" LEFT JOIN media_banks mb ON mb.media_md5 = lp.media_md5"
		"  AND mb.bank_index = lp.bank_index",
		&stmt, error)) {
		return rollback();
	}
	std::map<uint32_t, int> game_id_counts;
	while (true) {
		const int rc = sqlite3_step(stmt);
		if (rc == SQLITE_DONE) {
			break;
		}
		if (rc != SQLITE_ROW) {
			if (error != nullptr) {
				*error = sqlite3_errmsg(db_);
			}
			sqlite3_finalize(stmt);
			return rollback();
		}
		GameUpdate update;
		update.local_game_id = sqlite3_column_int64(stmt, 0);
		update.ra_game_id = static_cast<uint32_t>(sqlite3_column_int64(stmt, 1));
		update.distinct_ids = sqlite3_column_int(stmt, 2);
		update.anchor_state = sqlite3_column_int(stmt, 3);
		if (update.ra_game_id != 0 && update.distinct_ids == 1) {
			game_id_counts[update.ra_game_id]++;
		}
		game_updates.push_back(update);
	}
	sqlite3_finalize(stmt);
	if (!Exec("UPDATE games SET ra_game_id = NULL", error)) {
		return rollback();
	}

	for (const GameUpdate& update : game_updates) {
		const bool conflict = update.distinct_ids > 1 ||
			(update.ra_game_id != 0 && game_id_counts[update.ra_game_id] > 1);
		const int state = conflict ? 4 :
			(update.anchor_state == 0 ? 0 : (update.ra_game_id == 0 ? 2 : 1));
		if (state == 1) {
			const RaLibraryGameTitle& title = title_map.at(update.ra_game_id);
			if (!Prepare(db_,
				"UPDATE games SET ra_game_id = ?,"
				" title = CASE WHEN title_source = 2 THEN title ELSE ? END,"
				" sort_title = CASE WHEN title_source = 2 THEN sort_title ELSE ? END,"
				" title_source = CASE WHEN title_source = 2 THEN 2 ELSE 1 END,"
				" badge_url = ?, identification_state = 1, updated_at = ?"
				" WHERE id = ?",
				&stmt, error)) {
				return rollback();
			}
			sqlite3_bind_int64(stmt, 1, update.ra_game_id);
			sqlite3_bind_text(stmt, 2, title.title.c_str(), -1, SQLITE_TRANSIENT);
			const std::string sort_title = SortTitle(title.title);
			sqlite3_bind_text(stmt, 3, sort_title.c_str(), -1, SQLITE_TRANSIENT);
			sqlite3_bind_text(stmt, 4, title.badge_url.c_str(), -1,
				SQLITE_TRANSIENT);
			sqlite3_bind_int64(stmt, 5, now);
			sqlite3_bind_int64(stmt, 6, update.local_game_id);
		}
		else {
			if (!Prepare(db_,
				"UPDATE games SET identification_state = ?, updated_at = ?"
				" WHERE id = ?",
				&stmt, error)) {
				return rollback();
			}
			sqlite3_bind_int(stmt, 1, state);
			sqlite3_bind_int64(stmt, 2, now);
			sqlite3_bind_int64(stmt, 3, update.local_game_id);
		}
		if (!StepDone(db_, stmt, error)) {
			return rollback();
		}
	}

	if (!Prepare(db_, "DELETE FROM progress WHERE username = ?", &stmt,
		error)) {
		return rollback();
	}
	sqlite3_bind_text(stmt, 1, payload.username.c_str(), -1, SQLITE_TRANSIENT);
	if (!StepDone(db_, stmt, error)) {
		return rollback();
	}
	for (const RaLibraryProgress& item : payload.progress) {
		if (!Prepare(db_,
			"INSERT INTO progress(username, ra_game_id, core_total,"
			" core_unlocked, hardcore_unlocked, points_total,"
			" points_unlocked, synced_at) VALUES(?, ?, ?, ?, ?, NULL, NULL, ?)",
			&stmt, error)) {
			return rollback();
		}
		sqlite3_bind_text(stmt, 1, payload.username.c_str(), -1,
			SQLITE_TRANSIENT);
		sqlite3_bind_int64(stmt, 2, item.ra_game_id);
		sqlite3_bind_int64(stmt, 3, item.core_total);
		sqlite3_bind_int64(stmt, 4, item.core_unlocked);
		sqlite3_bind_int64(stmt, 5, item.hardcore_unlocked);
		sqlite3_bind_int64(stmt, 6, now);
		if (!StepDone(db_, stmt, error)) {
			return rollback();
		}
	}

	if (!Prepare(db_,
		"INSERT INTO sync_state(sync_key, completed_at, result_code)"
		" VALUES(?, ?, 0) ON CONFLICT(sync_key) DO UPDATE SET"
		" completed_at = excluded.completed_at, result_code = 0",
		&stmt, error)) {
		return rollback();
	}
	const std::string sync_key = "library:" + payload.username + ":pc8800";
	sqlite3_bind_text(stmt, 1, sync_key.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int64(stmt, 2, now);
	if (!StepDone(db_, stmt, error)) {
		return rollback();
	}
	return Exec("COMMIT", error);
}

RaImageCacheLoadResult RaLibrary::LoadCachedImage(const std::string& url,
	int64_t now, std::vector<uint8_t> *data, std::string *content_type,
	std::string *error)
{
	if (db_ == nullptr || url.empty() || data == nullptr ||
		content_type == nullptr) {
		if (error != nullptr) {
			*error = "invalid image cache lookup";
		}
		return RaImageCacheLoadResult::Error;
	}
	data->clear();
	content_type->clear();
	sqlite3_stmt *stmt = nullptr;
	if (!Prepare(db_, "SELECT id, relative_path, content_type, byte_size"
		" FROM image_cache WHERE url = ?", &stmt, error)) {
		return RaImageCacheLoadResult::Error;
	}
	sqlite3_bind_text(stmt, 1, url.c_str(), -1, SQLITE_TRANSIENT);
	const int rc = sqlite3_step(stmt);
	if (rc == SQLITE_DONE) {
		sqlite3_finalize(stmt);
		return RaImageCacheLoadResult::Miss;
	}
	if (rc != SQLITE_ROW) {
		if (error != nullptr) {
			*error = sqlite3_errmsg(db_);
		}
		sqlite3_finalize(stmt);
		return RaImageCacheLoadResult::Error;
	}
	const int64_t id = sqlite3_column_int64(stmt, 0);
	const std::string relative_path = ColumnText(stmt, 1);
	const std::string stored_type = ColumnText(stmt, 2);
	const int64_t byte_size = sqlite3_column_int64(stmt, 3);
	sqlite3_finalize(stmt);

	std::vector<uint8_t> loaded;
	int width = 0;
	int height = 0;
	std::vector<uint8_t> rgba;
	const std::string normalized_type = NormalizeImageContentType(stored_type);
	const bool valid = IsOwnedImageRelativePath(id, relative_path) &&
		(normalized_type == "image/png" ||
			normalized_type == "image/jpeg") &&
		((normalized_type == "image/png" &&
			relative_path.size() >= 4 &&
			relative_path.compare(relative_path.size() - 4, 4, ".png") == 0) ||
		(normalized_type == "image/jpeg" &&
			relative_path.size() >= 4 &&
			relative_path.compare(relative_path.size() - 4, 4, ".jpg") == 0)) &&
		byte_size > 0 && byte_size <= 1024 * 1024 &&
			ReadRaFile(JoinPath(root_, relative_path.c_str()), &loaded,
				1024U * 1024U, nullptr) &&
		static_cast<int64_t>(loaded.size()) == byte_size &&
		Xm8RaBuildInfo::DecodeImageRgba(loaded.data(), loaded.size(),
			&width, &height, &rgba) && width <= 2048 && height <= 2048 &&
		static_cast<int64_t>(width) * height <= 4194304;
	if (!valid) {
		RemoveCachedImageById(id, relative_path, nullptr);
		return RaImageCacheLoadResult::Miss;
	}

	if (!Prepare(db_, "UPDATE image_cache SET last_used_at = ? WHERE id = ?",
		&stmt, error)) {
		return RaImageCacheLoadResult::Error;
	}
	sqlite3_bind_int64(stmt, 1, now);
	sqlite3_bind_int64(stmt, 2, id);
	if (!StepDone(db_, stmt, error)) {
		return RaImageCacheLoadResult::Error;
	}
	*data = std::move(loaded);
	*content_type = normalized_type;
	return RaImageCacheLoadResult::Hit;
}

bool RaLibrary::StoreCachedImage(const std::string& url,
	RaImageKind image_kind, const std::string& content_type,
	const std::vector<uint8_t>& data, int64_t now, int64_t cache_limit_bytes,
	const std::vector<std::string>& protected_urls, std::string *error)
{
	const std::string normalized_type = NormalizeImageContentType(content_type);
	const int image_kind_value = static_cast<int>(image_kind);
	int width = 0;
	int height = 0;
	std::vector<uint8_t> rgba;
	if (db_ == nullptr || url.empty() ||
		(normalized_type != "image/png" &&
			normalized_type != "image/jpeg") ||
		data.empty() || data.size() > 1024U * 1024U ||
		!Xm8RaBuildInfo::DecodeImageRgba(data.data(), data.size(), &width,
			&height, &rgba) || width > 2048 || height > 2048 ||
		static_cast<int64_t>(width) * height > 4194304 ||
		image_kind_value < 0 || image_kind_value > 3 ||
		cache_limit_bytes < 0) {
		if (error != nullptr) {
			*error = "invalid image cache entry";
		}
		return false;
	}

	int64_t id = 0;
	std::string old_relative_path;
	sqlite3_stmt *stmt = nullptr;
	if (!Prepare(db_, "SELECT id, relative_path FROM image_cache WHERE url = ?",
		&stmt, error)) {
		return false;
	}
	sqlite3_bind_text(stmt, 1, url.c_str(), -1, SQLITE_TRANSIENT);
	const int existing_rc = sqlite3_step(stmt);
	if (existing_rc == SQLITE_ROW) {
		id = sqlite3_column_int64(stmt, 0);
		old_relative_path = ColumnText(stmt, 1);
	}
	sqlite3_finalize(stmt);
	if (existing_rc != SQLITE_ROW && existing_rc != SQLITE_DONE) {
		if (error != nullptr) {
			*error = sqlite3_errmsg(db_);
		}
		return false;
	}
	if (id == 0) {
		if (!Prepare(db_, "SELECT COALESCE(MAX(id), 0) + 1 FROM image_cache",
			&stmt, error)) {
			return false;
		}
		const int id_rc = sqlite3_step(stmt);
		if (id_rc == SQLITE_ROW) {
			id = sqlite3_column_int64(stmt, 0);
		}
		sqlite3_finalize(stmt);
		if (id_rc != SQLITE_ROW) {
			if (error != nullptr) {
				*error = sqlite3_errmsg(db_);
			}
			return false;
		}
	}
	if (id <= 0) {
		if (error != nullptr) {
			*error = "cannot allocate image cache id";
		}
		return false;
	}

	const std::string relative_path = "images/" + std::to_string(id) +
		(normalized_type == "image/png" ? ".png" : ".jpg");
	const std::string final_path = JoinPath(root_, relative_path.c_str());
	const std::string temporary_path = final_path + ".tmp";
	if (!WriteRaFile(temporary_path, data.data(), data.size(), error) ||
		!MoveRaFile(temporary_path, final_path, true, error)) {
		RemoveRaFile(temporary_path, nullptr);
		if (error != nullptr && error->empty()) {
			*error = "cannot install image cache file";
		}
		return false;
	}

	if (!Prepare(db_, "INSERT INTO image_cache(id, url, image_kind,"
		" relative_path, content_type, byte_size, last_used_at)"
		" VALUES(?, ?, ?, ?, ?, ?, ?) ON CONFLICT(url) DO UPDATE SET"
		" image_kind = excluded.image_kind,"
		" relative_path = excluded.relative_path,"
		" content_type = excluded.content_type,"
		" byte_size = excluded.byte_size,"
		" last_used_at = excluded.last_used_at", &stmt, error)) {
		RemoveRaFile(final_path, nullptr);
		return false;
	}
	sqlite3_bind_int64(stmt, 1, id);
	sqlite3_bind_text(stmt, 2, url.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(stmt, 3, image_kind_value);
	sqlite3_bind_text(stmt, 4, relative_path.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(stmt, 5, normalized_type.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int64(stmt, 6, static_cast<int64_t>(data.size()));
	sqlite3_bind_int64(stmt, 7, now);
	if (!StepDone(db_, stmt, error)) {
		RemoveRaFile(final_path, nullptr);
		return false;
	}
	if (!old_relative_path.empty() && old_relative_path != relative_path &&
		IsOwnedImageRelativePath(id, old_relative_path)) {
		RemoveRaFile(JoinPath(root_, old_relative_path.c_str()), nullptr);
	}
	return PruneImageCache(cache_limit_bytes, protected_urls, error);
}

bool RaLibrary::RemoveCachedImageById(int64_t id,
	const std::string& relative_path, std::string *error)
{
	if (IsOwnedImageRelativePath(id, relative_path)) {
		const std::string path = JoinPath(root_, relative_path.c_str());
		if (!RemoveRaFile(path, error)) {
			return false;
		}
	}
	sqlite3_stmt *stmt = nullptr;
	if (!Prepare(db_, "DELETE FROM image_cache WHERE id = ?", &stmt, error)) {
		return false;
	}
	sqlite3_bind_int64(stmt, 1, id);
	return StepDone(db_, stmt, error);
}

bool RaLibrary::RemoveCachedImage(const std::string& url, std::string *error)
{
	sqlite3_stmt *stmt = nullptr;
	if (!Prepare(db_, "SELECT id, relative_path FROM image_cache WHERE url = ?",
		&stmt, error)) {
		return false;
	}
	sqlite3_bind_text(stmt, 1, url.c_str(), -1, SQLITE_TRANSIENT);
	const int rc = sqlite3_step(stmt);
	if (rc != SQLITE_ROW) {
		sqlite3_finalize(stmt);
		if (rc == SQLITE_DONE) {
			return true;
		}
		if (error != nullptr) {
			*error = sqlite3_errmsg(db_);
		}
		return false;
	}
	const int64_t id = sqlite3_column_int64(stmt, 0);
	const std::string relative_path = ColumnText(stmt, 1);
	sqlite3_finalize(stmt);
	return RemoveCachedImageById(id, relative_path, error);
}

bool RaLibrary::PruneImageCache(int64_t cache_limit_bytes,
	const std::vector<std::string>& protected_urls, std::string *error)
{
	if (cache_limit_bytes < 0) {
		return false;
	}
	std::set<std::string> protected_set(protected_urls.begin(),
		protected_urls.end());
	struct Candidate {
		int64_t id;
		std::string url;
		std::string path;
		int64_t size;
		bool valid;
	};
	std::vector<Candidate> candidates;
	int64_t total = 0;
	sqlite3_stmt *stmt = nullptr;
	if (!Prepare(db_, "SELECT id, url, relative_path, byte_size"
		" FROM image_cache ORDER BY last_used_at, id", &stmt, error)) {
		return false;
	}
	int rc = SQLITE_ROW;
	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		Candidate item;
		item.id = sqlite3_column_int64(stmt, 0);
		item.url = ColumnText(stmt, 1);
		item.path = ColumnText(stmt, 2);
		item.size = sqlite3_column_int64(stmt, 3);
		item.valid = item.size > 0 &&
			IsOwnedImageRelativePath(item.id, item.path);
		if (item.valid) {
			total += item.size;
		}
		candidates.push_back(item);
	}
	sqlite3_finalize(stmt);
	if (rc != SQLITE_DONE) {
		if (error != nullptr) {
			*error = sqlite3_errmsg(db_);
		}
		return false;
	}
	for (const Candidate& item : candidates) {
		if (!item.valid) {
			if (!RemoveCachedImageById(item.id, item.path, error)) {
				return false;
			}
			continue;
		}
		if (total <= cache_limit_bytes) {
			continue;
		}
		if (protected_set.find(item.url) != protected_set.end()) {
			continue;
		}
		if (!RemoveCachedImageById(item.id, item.path, error)) {
			return false;
		}
		total -= item.size;
	}
	return true;
}

bool RaLibrary::EnqueuePendingUnlock(const RaPendingUnlockRecord& record,
	int64_t *record_id, std::string *error)
{
	if (record_id != nullptr) *record_id = 0;
	if (db_ == nullptr || record.account.empty() || record.achievement_id == 0 ||
		!IsMd5Hex(record.game_hash) || record.unlocked_at <= 0) {
		if (error != nullptr) *error = "invalid pending unlock";
		return false;
	}
	sqlite3_stmt *stmt = nullptr;
	if (!Prepare(db_,
		"INSERT INTO pending_unlocks(account, achievement_id, hardcore,"
		" game_hash, unlocked_at) VALUES(?, ?, ?, ?, ?)"
		" ON CONFLICT(account, achievement_id, hardcore) DO UPDATE SET"
		" game_hash=excluded.game_hash,"
		" unlocked_at=MIN(pending_unlocks.unlocked_at, excluded.unlocked_at)"
		" RETURNING id", &stmt, error)) {
		return false;
	}
	sqlite3_bind_text(stmt, 1, record.account.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int64(stmt, 2, record.achievement_id);
	sqlite3_bind_int(stmt, 3, record.hardcore ? 1 : 0);
	sqlite3_bind_text(stmt, 4, record.game_hash.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int64(stmt, 5, record.unlocked_at);
	const int rc = sqlite3_step(stmt);
	if (rc == SQLITE_ROW && record_id != nullptr) {
		*record_id = sqlite3_column_int64(stmt, 0);
	}
	const bool ok = rc == SQLITE_ROW && sqlite3_step(stmt) == SQLITE_DONE;
	if (!ok && error != nullptr) *error = sqlite3_errmsg(db_);
	sqlite3_finalize(stmt);
	return ok;
}

bool RaLibrary::ListPendingUnlocks(const std::string& account,
	std::vector<RaPendingUnlockRecord> *records, std::string *error)
{
	if (records != nullptr) records->clear();
	if (db_ == nullptr || account.empty() || records == nullptr) {
		if (error != nullptr) *error = "invalid pending unlock query";
		return false;
	}
	sqlite3_stmt *stmt = nullptr;
	if (!Prepare(db_, "SELECT id, account, achievement_id, hardcore,"
		" game_hash, unlocked_at, status, attempt_count, last_error"
		" FROM pending_unlocks WHERE account=? ORDER BY unlocked_at, id",
		&stmt, error)) return false;
	sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
	int rc = SQLITE_ROW;
	while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		RaPendingUnlockRecord record;
		const int64_t achievement_id = sqlite3_column_int64(stmt, 2);
		const int64_t attempt_count = sqlite3_column_int64(stmt, 7);
		record.id = sqlite3_column_int64(stmt, 0);
		record.account = ColumnText(stmt, 1);
		record.achievement_id = achievement_id > 0 && achievement_id <= UINT32_MAX ?
			static_cast<uint32_t>(achievement_id) : 0;
		record.hardcore = sqlite3_column_int(stmt, 3) != 0;
		record.game_hash = ColumnText(stmt, 4);
		record.unlocked_at = sqlite3_column_int64(stmt, 5);
		record.status = static_cast<RaPendingUnlockStatus>(
			sqlite3_column_int(stmt, 6));
		record.attempt_count = attempt_count >= 0 && attempt_count <= UINT32_MAX ?
			static_cast<uint32_t>(attempt_count) : UINT32_MAX;
		record.last_error = ColumnText(stmt, 8);
		if (record.id <= 0 || record.account != account ||
			record.achievement_id == 0 || !IsMd5Hex(record.game_hash) ||
			record.unlocked_at <= 0 || attempt_count < 0 ||
			attempt_count > UINT32_MAX ||
			(record.status != RaPendingUnlockStatus::Pending &&
			record.status != RaPendingUnlockStatus::Held)) {
			if (error != nullptr) *error = "invalid pending unlock record";
			sqlite3_finalize(stmt);
			records->clear();
			return false;
		}
		records->push_back(record);
	}
	if (rc != SQLITE_DONE && error != nullptr) *error = sqlite3_errmsg(db_);
	sqlite3_finalize(stmt);
	if (rc != SQLITE_DONE) records->clear();
	return rc == SQLITE_DONE;
}

bool RaLibrary::MarkPendingUnlockAttempt(int64_t record_id,
	RaPendingUnlockStatus status, const std::string& last_error,
	std::string *error)
{
	sqlite3_stmt *stmt = nullptr;
	if (record_id <= 0 || (status != RaPendingUnlockStatus::Pending &&
		status != RaPendingUnlockStatus::Held) ||
		!Prepare(db_, "UPDATE pending_unlocks SET status=?,"
		" attempt_count=attempt_count+1, last_error=? WHERE id=?",
		&stmt, error)) return false;
	sqlite3_bind_int(stmt, 1, static_cast<int>(status));
	sqlite3_bind_text(stmt, 2, last_error.c_str(), -1, SQLITE_TRANSIENT);
	sqlite3_bind_int64(stmt, 3, record_id);
	return StepDone(db_, stmt, error);
}

bool RaLibrary::RemovePendingUnlock(int64_t record_id, std::string *error)
{
	sqlite3_stmt *stmt = nullptr;
	if (record_id <= 0 || !Prepare(db_,
		"DELETE FROM pending_unlocks WHERE id=?", &stmt, error)) return false;
	sqlite3_bind_int64(stmt, 1, record_id);
	return StepDone(db_, stmt, error);
}

bool RaLibrary::RemovePendingUnlocksForAccount(const std::string& account,
	std::string *error)
{
	sqlite3_stmt *stmt = nullptr;
	if (account.empty() || !Prepare(db_,
		"DELETE FROM pending_unlocks WHERE account=?", &stmt, error)) return false;
	sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
	return StepDone(db_, stmt, error);
}

bool RaLibrary::CountPendingUnlocks(const std::string& account,
	size_t *count, std::string *error)
{
	if (count != nullptr) *count = 0;
	sqlite3_stmt *stmt = nullptr;
	if (account.empty() || count == nullptr || !Prepare(db_,
		"SELECT COUNT(*) FROM pending_unlocks WHERE account=?", &stmt, error)) {
		return false;
	}
	sqlite3_bind_text(stmt, 1, account.c_str(), -1, SQLITE_TRANSIENT);
	const int rc = sqlite3_step(stmt);
	if (rc == SQLITE_ROW) *count = static_cast<size_t>(sqlite3_column_int64(stmt, 0));
	if (rc != SQLITE_ROW && error != nullptr) *error = sqlite3_errmsg(db_);
	sqlite3_finalize(stmt);
	return rc == SQLITE_ROW;
}

bool RaLibrary::RecoveryRequired(std::string *reason) const
{
	const bool required = RaPathExists(PendingUnlockRecoveryPath());
	if (reason != nullptr) {
		*reason = required ?
			"a quarantined RA database may contain pending unlocks" : "";
	}
	return required;
}

bool RaLibrary::ConfirmDiscardRecovery(std::string *error)
{
	return RemoveRaFile(PendingUnlockRecoveryPath(), error);
}

} // namespace Xm8Ra

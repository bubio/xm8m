#ifndef XM8_RA_LIBRARY_H
#define XM8_RA_LIBRARY_H

#include "ra_media_probe.h"
#include "ra_pending_unlock.h"

#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;

namespace Xm8Ra {

constexpr int kRaModeSoftcore = 1;
constexpr int kRaModeHardcore = 2;

constexpr int kRaIdentificationUnidentified = 0;
constexpr int kRaIdentificationIdentified = 1;
constexpr int kRaIdentificationUnregistered = 2;
constexpr int kRaIdentificationError = 3;
constexpr int kRaIdentificationConflict = 4;

constexpr int kRaMediaHealthOk = 0;
constexpr int kRaMediaHealthSourceMissing = 1;
constexpr int kRaMediaHealthSourceChanged = 2;
constexpr int kRaMediaHealthWorkingMissing = 3;
constexpr int kRaMediaHealthWorkingCorrupt = 4;

struct RaSettings {
	bool enabled = false;
	int last_mode = kRaModeHardcore;
	bool unofficial_enabled = false;
	bool encore_enabled = false;
	bool spectator_enabled = false;
	int notification_seconds = 5;
	int image_cache_limit_mib = 128;
};

struct MediaRecord {
	std::string md5;
	int64_t game_id = 0;
	std::string working_relpath;
	bool inserted = false;
};

struct MediaHealthRecord {
	std::string md5;
	int64_t game_id = 0;
	std::string source_locator;
	int64_t source_size = 0;
	int64_t source_mtime = -1;
	std::string working_relpath;
	int health_state = kRaMediaHealthOk;
};

struct MediaHealthStatus {
	std::string md5;
	int health_state = kRaMediaHealthOk;
	bool source_exists = false;
	bool source_metadata_changed = false;
	bool source_hash_changed = false;
	bool working_exists = false;
	bool working_probe_ok = false;
	int64_t source_size = 0;
	int64_t source_mtime = -1;
};

struct RaLibraryGameListItem {
	int64_t game_id = 0;
	int64_t ra_game_id = 0;
	std::string title;
	std::string badge_url;
	int identification_state = 0;
	int media_count = 0;
	int health_state = kRaMediaHealthOk;
	int64_t last_played_at = 0;
	bool has_progress = false;
	int core_total = 0;
	int core_unlocked = 0;
	int hardcore_unlocked = 0;
	int points_total = 0;
	int points_unlocked = 0;
};

struct RaMediaBankHash {
	std::string media_md5;
	int bank_index = 0;
	std::string ra_hash;
};

struct RaLibraryHashMatch {
	std::string hash;
	uint32_t ra_game_id = 0;
};

struct RaLibraryGameTitle {
	uint32_t ra_game_id = 0;
	std::string title;
	std::string badge_url;
};

struct RaLibraryProgress {
	uint32_t ra_game_id = 0;
	uint32_t core_total = 0;
	uint32_t core_unlocked = 0;
	uint32_t hardcore_unlocked = 0;
};

struct RaLibrarySyncPayload {
	std::string username;
	std::vector<RaLibraryHashMatch> hashes;
	std::vector<RaLibraryGameTitle> titles;
	std::vector<RaLibraryProgress> progress;
};

enum class RaImageCacheLoadResult {
	Hit,
	Miss,
	Error,
};

enum class RaImageKind {
	GameBadge = 0,
	AchievementBadge = 1,
	AchievementBadgeLocked = 2,
	Other = 3,
};

enum class RaGameConflictKind {
	None = 0,
	Merge = 1,
	Split = 2,
	Manual = 3,
};

struct RaGameConflictInfo {
	RaGameConflictKind kind = RaGameConflictKind::None;
	int64_t game_id = 0;
	int64_t primary_ra_game_id = 0;
	int related_game_count = 0;
	int resulting_game_count = 0;
};

struct LaunchDrive {
	bool assigned = false;
	std::string media_md5;
	int bank_index = 0;
	bool is_ra_anchor = false;
};

struct LaunchProfile {
	int64_t game_id = 0;
	LaunchDrive drives[2];
};

class RaLibrary : public RaPendingUnlockStore {
public:
	RaLibrary();
	~RaLibrary();

	RaLibrary(const RaLibrary&) = delete;
	RaLibrary& operator=(const RaLibrary&) = delete;

	bool Open(const std::string& ra_root, std::string *error);
	void Close();

	const std::string& Root() const { return root_; }
	std::string MediaRoot() const;
	std::string TempRoot() const;
	std::string DatabasePath() const;
	std::string PendingUnlockRecoveryPath() const;

	bool LoadSettings(RaSettings *settings, std::string *error);
	bool SaveSettings(const RaSettings& settings, std::string *error);
	bool RegisterDesktopMedia(const D88MediaInfo& media,
		const std::string& source_path, const std::string& display_name,
		int64_t source_mtime, MediaRecord *record, std::string *error);
	bool RegisterDesktopMediaInGame(const D88MediaInfo& media,
		const std::string& source_path, const std::string& display_name,
		int64_t source_mtime, int64_t game_id, int ordinal,
		MediaRecord *record, std::string *error);
	bool FindMedia(const std::string& md5, MediaRecord *record,
		std::string *error);
	bool FindMediaByWorkingRelpath(const std::string& working_relpath,
		MediaRecord *record, std::string *error);
	bool LoadMediaBankHash(const std::string& media_md5, int bank_index,
		std::string *ra_hash, std::string *error);
	bool LoadMediaHealthRecord(const std::string& md5,
		MediaHealthRecord *record, std::string *error);
	bool UpdateMediaHealth(const MediaHealthStatus& status,
		std::string *error);
	bool LoadLaunchProfile(int64_t game_id, LaunchProfile *profile,
		std::string *error);
	bool SaveLaunchProfile(const LaunchProfile& profile,
		std::string *error);
	bool LoadGameIdentification(int64_t game_id, int64_t *ra_game_id,
		int *identification_state, std::string *error);
	bool InspectGameConflict(int64_t game_id, RaGameConflictInfo *info,
		std::string *error);
	bool ResolveGameConflict(int64_t game_id, RaGameConflictInfo *result,
		std::string *error);
	bool MergeGameMedia(int64_t target_game_id, int64_t source_game_id,
		std::string *error);
	bool MarkGameIdentified(int64_t game_id, int64_t ra_game_id,
		const std::string& title, const std::string& badge_url,
		std::string *error);
	bool ListGames(std::vector<RaLibraryGameListItem> *games,
		std::string *error);
	bool ListGamesForUser(const std::string& username,
		std::vector<RaLibraryGameListItem> *games, std::string *error);
	bool MarkGamePlayed(int64_t game_id, std::string *error);
	bool ListMediaBankHashes(std::vector<RaMediaBankHash> *hashes,
		std::string *error);
	bool ApplyLibrarySync(const RaLibrarySyncPayload& payload,
		std::string *error);
	RaImageCacheLoadResult LoadCachedImage(const std::string& url,
		int64_t now, std::vector<uint8_t> *data, std::string *content_type,
		std::string *error);
	bool StoreCachedImage(const std::string& url, RaImageKind image_kind,
		const std::string& content_type, const std::vector<uint8_t>& data,
		int64_t now, int64_t cache_limit_bytes,
		const std::vector<std::string>& protected_urls, std::string *error);
	bool RemoveCachedImage(const std::string& url, std::string *error);
	bool PruneImageCache(int64_t cache_limit_bytes,
		const std::vector<std::string>& protected_urls, std::string *error);
	bool EnqueuePendingUnlock(const RaPendingUnlockRecord& record,
		int64_t *record_id, std::string *error) override;
	bool ListPendingUnlocks(const std::string& account,
		std::vector<RaPendingUnlockRecord> *records,
		std::string *error) override;
	bool MarkPendingUnlockAttempt(int64_t record_id,
		RaPendingUnlockStatus status, const std::string& last_error,
		std::string *error) override;
	bool RemovePendingUnlock(int64_t record_id,
		std::string *error) override;
	bool RemovePendingUnlocksForAccount(const std::string& account,
		std::string *error) override;
	bool CountPendingUnlocks(const std::string& account,
		size_t *count, std::string *error) override;
	bool RecoveryRequired(std::string *reason) const override;
	bool ConfirmDiscardRecovery(std::string *error) override;

private:
	bool Exec(const char *sql, std::string *error);
	bool SetupDatabase(std::string *error);
	bool CheckIntegrity(std::string *error);
	bool InitializeSchema(std::string *error);
	bool EnsureSettingsRow(std::string *error);
	bool IsDatabaseDamage() const;
	bool QuarantineDatabase(std::string *error);
	bool ValidateSettings(const RaSettings& settings, std::string *error) const;
	bool RemoveCachedImageById(int64_t id, const std::string& relative_path,
		std::string *error);
	bool RegisterDesktopMediaInternal(const D88MediaInfo& media,
		const std::string& source_path, const std::string& display_name,
		int64_t source_mtime, int64_t game_id, int ordinal,
		bool create_anchor_profile, MediaRecord *record,
		std::string *error);
	bool UpdateMediaBankHashes(const D88MediaInfo& media,
		std::string *error);
	int64_t NowUnixTime() const;
	void CloseDatabaseOnly();

	sqlite3 *db_;
	std::string root_;
};

} // namespace Xm8Ra

#endif

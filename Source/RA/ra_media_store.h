#ifndef XM8_RA_MEDIA_STORE_H
#define XM8_RA_MEDIA_STORE_H

#include "ra_library.h"
#include "ra_media_probe.h"

#include <string>
#include <vector>

namespace Xm8Ra {

struct ImportedMedia {
	MediaRecord record;
	D88MediaInfo media_info;
	std::string working_path;
	bool copied = false;
};

struct ImportedPlaylist {
	std::vector<ImportedMedia> media;
	int64_t game_id = 0;
	std::string anchor_md5;
};

struct ImportedFolder {
	std::vector<ImportedMedia> standalone_media;
	std::vector<ImportedPlaylist> playlists;
	int scanned_candidates = 0;
};

struct ResolvedLaunchDisk {
	bool assigned = false;
	int drive = 0;
	int bank_index = 0;
	std::string media_md5;
	std::string ra_hash;
	std::string working_path;
	int health_state = kRaMediaHealthOk;
	bool is_ra_anchor = false;
};

struct ResolvedWorkingMedia {
	MediaRecord record;
	int bank_index = 0;
	std::string ra_hash;
	std::string working_path;
};

struct ResolvedLaunchProfile {
	int64_t game_id = 0;
	ResolvedLaunchDisk drives[2];
	std::string anchor_md5;
	std::string anchor_working_path;
};

class RaMediaStore {
public:
	explicit RaMediaStore(RaLibrary *library);

	bool ImportDesktopD88(const std::string& source_path,
		ImportedMedia *imported, std::string *error);
	bool ImportM3U(const std::string& playlist_path,
		ImportedPlaylist *imported, std::string *error);
	bool ImportFolderRecursive(const std::string& folder_path,
		ImportedFolder *imported, std::string *error);
	bool ResetWorkingCopy(const std::string& source_path,
		const std::string& expected_md5, std::string *working_path,
		std::string *error);
	bool CheckMediaHealth(const std::string& md5,
		MediaHealthStatus *status, std::string *error);
	bool ResolveLaunchProfile(int64_t game_id,
		ResolvedLaunchProfile *profile, std::string *error);
	bool ResolveWorkingMedia(const std::string& working_path, int bank_index,
		ResolvedWorkingMedia *media, std::string *error);

private:
	bool ImportD88IntoGame(const std::string& source_path, int64_t game_id,
		int ordinal, ImportedMedia *imported, std::string *error);
	bool EnsureWorkingCopy(const std::string& source_path,
		const D88MediaInfo& media, std::string *working_path,
		bool *copied, std::string *error);
	bool CopyAndVerify(const std::string& source_path,
		const std::string& temporary_path, const D88MediaInfo& expected,
		std::string *error);
	int64_t FileMtime(const std::string& path) const;

	RaLibrary *library_;
};

} // namespace Xm8Ra

#endif

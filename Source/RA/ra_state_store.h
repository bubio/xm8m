#ifndef XM8_RA_STATE_STORE_H
#define XM8_RA_STATE_STORE_H

#include <cstdint>
#include <string>
#include <vector>

namespace Xm8Ra {

constexpr uint32_t kRaStateChunkVersion = 2;
constexpr size_t kRaStateMaxProgressSize = 16U * 1024U * 1024U;
constexpr size_t kRaStateMaxFileSize = 256U * 1024U * 1024U;
constexpr int kRaStateMaxSlot = 9;

enum class RaStateMode : uint8_t {
	Casual = 1,
	HardcoreDebug = 2,
	Offline = 3,
};

struct RaStateRecord {
	RaStateMode mode = RaStateMode::Casual;
	uint32_t game_id = 0;
	std::string anchor_md5;
	std::string active_media_hash;
	uint32_t rcheevos_version = 0;
	std::vector<uint8_t> body;
	std::vector<uint8_t> progress;
};

struct RaStateExpectation {
	RaStateMode mode = RaStateMode::Casual;
	uint32_t game_id = 0;
	std::string anchor_md5;
	std::string active_media_hash;
	uint32_t rcheevos_version = 0;
};

bool BuildRaState(const RaStateRecord& record, std::vector<uint8_t> *bytes,
	std::string *error);
bool ParseRaState(const std::vector<uint8_t>& bytes, RaStateRecord *record,
	std::string *error);
bool ValidateRaState(const RaStateRecord& record,
	const RaStateExpectation& expected, std::string *error);

std::string RaStatePath(const std::string& ra_root, RaStateMode mode,
	uint32_t game_id, const std::string& anchor_md5, int slot);
bool ReadRaStateFile(const std::string& path, std::vector<uint8_t> *bytes,
	std::string *error);
bool WriteRaStateFileAtomically(const std::string& path,
	const std::vector<uint8_t>& bytes, std::string *error);

} // namespace Xm8Ra

#endif

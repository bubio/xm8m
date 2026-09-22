#include "ra_state_store.h"

#include "ra_file_util.h"

#include <cstring>
#include <limits>
#include <sstream>

namespace Xm8Ra {
namespace {

constexpr size_t kFixedChunkSize = 108;
constexpr size_t kFooterSize = 8;
constexpr size_t kVersionOffset = 4;
constexpr size_t kChunkSizeOffset = 8;
constexpr size_t kBodySizeOffset = 12;
constexpr size_t kBodyCrcOffset = 20;
constexpr size_t kGameIdOffset = 24;
constexpr size_t kModeOffset = 28;
constexpr size_t kReservedOffset = 29;
constexpr size_t kMediaMd5Offset = 32;
constexpr size_t kRcheevosVersionOffset = 64;
constexpr size_t kActiveMediaHashOffset = 68;
constexpr size_t kProgressSizeOffset = 100;
constexpr size_t kProgressOffset = 104;
const uint8_t kChunkMagic[4] = {'X', 'M', 'R', 'A'};
const uint8_t kFooterMagic[4] = {'X', 'M', 'R', 'F'};

void SetError(std::string *error, const std::string& message)
{
	if (error != nullptr) {
		*error = message;
	}
}

void Append32(std::vector<uint8_t> *bytes, uint32_t value)
{
	for (int shift = 0; shift < 32; shift += 8) {
		bytes->push_back(static_cast<uint8_t>(value >> shift));
	}
}

void Append64(std::vector<uint8_t> *bytes, uint64_t value)
{
	for (int shift = 0; shift < 64; shift += 8) {
		bytes->push_back(static_cast<uint8_t>(value >> shift));
	}
}

bool Read32(const std::vector<uint8_t>& bytes, size_t offset, uint32_t *value)
{
	if (offset > bytes.size() || bytes.size() - offset < 4) {
		return false;
	}
	*value = static_cast<uint32_t>(bytes[offset]) |
		(static_cast<uint32_t>(bytes[offset + 1]) << 8) |
		(static_cast<uint32_t>(bytes[offset + 2]) << 16) |
		(static_cast<uint32_t>(bytes[offset + 3]) << 24);
	return true;
}

bool Read64(const std::vector<uint8_t>& bytes, size_t offset, uint64_t *value)
{
	if (offset > bytes.size() || bytes.size() - offset < 8) {
		return false;
	}
	*value = 0;
	for (int index = 7; index >= 0; --index) {
		*value = (*value << 8) | bytes[offset + static_cast<size_t>(index)];
	}
	return true;
}

uint32_t Crc32(const uint8_t *data, size_t size)
{
	uint32_t crc = 0xffffffffU;
	for (size_t index = 0; index < size; ++index) {
		crc ^= data[index];
		for (int bit = 0; bit < 8; ++bit) {
			crc = (crc >> 1) ^ (0xedb88320U &
				static_cast<uint32_t>(-static_cast<int32_t>(crc & 1U)));
		}
	}
	return ~crc;
}

bool IsMd5(const std::string& value)
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

std::string JoinPath(const std::string& left, const std::string& right)
{
	if (left.empty()) {
		return right;
	}
	if (left.back() == '/' || left.back() == '\\') {
		return left + right;
	}
	return left + "/" + right;
}

std::string ParentPath(const std::string& path)
{
	const size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

} // namespace

bool BuildRaState(const RaStateRecord& record, std::vector<uint8_t> *bytes,
	std::string *error)
{
	if (bytes != nullptr) bytes->clear();
	if (bytes == nullptr || !IsMd5(record.anchor_md5)) {
		SetError(error, "invalid RA state media MD5");
		return false;
	}
	if (record.progress.size() > kRaStateMaxProgressSize ||
		record.progress.size() > std::numeric_limits<uint32_t>::max()) {
		SetError(error, "RA progress is too large");
		return false;
	}
	if (record.body.size() > kRaStateMaxFileSize - kFixedChunkSize -
		kFooterSize - record.progress.size()) {
		SetError(error, "RA state body is too large");
		return false;
	}
	if (record.mode == RaStateMode::Casual ||
		record.mode == RaStateMode::HardcoreDebug) {
		if (record.game_id == 0 || record.progress.empty() ||
			!IsMd5(record.active_media_hash)) {
			SetError(error,
				"online RA state requires a game, media hash, and RA progress");
			return false;
		}
	}
	else if (record.mode == RaStateMode::Offline) {
		if (record.game_id != 0 || !record.progress.empty() ||
			!record.active_media_hash.empty()) {
			SetError(error,
				"Offline state cannot contain RA progress or an active media hash");
			return false;
		}
	}
	else {
		SetError(error, "unknown RA state mode");
		return false;
	}

	const uint64_t body_size = record.body.size();
	const uint64_t chunk_size64 = kFixedChunkSize + record.progress.size();
	if (chunk_size64 > std::numeric_limits<uint32_t>::max()) {
		SetError(error, "RA state chunk is too large");
		return false;
	}
	const uint32_t chunk_size = static_cast<uint32_t>(chunk_size64);
	bytes->reserve(record.body.size() + chunk_size + kFooterSize);
	bytes->insert(bytes->end(), record.body.begin(), record.body.end());
	const size_t chunk_offset = bytes->size();
	bytes->insert(bytes->end(), kChunkMagic, kChunkMagic + 4);
	Append32(bytes, kRaStateChunkVersion);
	Append32(bytes, chunk_size);
	Append64(bytes, body_size);
	Append32(bytes, Crc32(record.body.data(), record.body.size()));
	Append32(bytes, record.game_id);
	bytes->push_back(static_cast<uint8_t>(record.mode));
	bytes->insert(bytes->end(), 3, 0);
	bytes->insert(bytes->end(), record.anchor_md5.begin(), record.anchor_md5.end());
	Append32(bytes, record.rcheevos_version);
	if (record.active_media_hash.empty()) {
		bytes->insert(bytes->end(), 32, 0);
	}
	else {
		bytes->insert(bytes->end(), record.active_media_hash.begin(),
			record.active_media_hash.end());
	}
	Append32(bytes, static_cast<uint32_t>(record.progress.size()));
	bytes->insert(bytes->end(), record.progress.begin(), record.progress.end());
	Append32(bytes, Crc32(bytes->data() + chunk_offset,
		bytes->size() - chunk_offset));
	bytes->insert(bytes->end(), kFooterMagic, kFooterMagic + 4);
	Append32(bytes, chunk_size);
	SetError(error, std::string());
	return true;
}

bool ParseRaState(const std::vector<uint8_t>& bytes, RaStateRecord *record,
	std::string *error)
{
	if (record == nullptr) {
		SetError(error, "RA state output is required");
		return false;
	}
	if (bytes.size() > kRaStateMaxFileSize) {
		SetError(error, "RA state file is too large");
		return false;
	}
	if (bytes.size() < kFixedChunkSize + kFooterSize) {
		SetError(error, "RA state footer is missing");
		return false;
	}
	const size_t footer_offset = bytes.size() - kFooterSize;
	if (std::memcmp(bytes.data() + footer_offset, kFooterMagic, 4) != 0) {
		SetError(error, "RA state footer is missing");
		return false;
	}
	uint32_t footer_chunk_size = 0;
	if (!Read32(bytes, footer_offset + 4, &footer_chunk_size) ||
		footer_chunk_size < kFixedChunkSize ||
		footer_chunk_size > footer_offset) {
		SetError(error, "invalid RA state chunk size");
		return false;
	}
	const size_t chunk_offset = footer_offset - footer_chunk_size;
	if (std::memcmp(bytes.data() + chunk_offset, kChunkMagic, 4) != 0) {
		SetError(error, "RA state chunk is missing");
		return false;
	}
	uint32_t version = 0;
	uint32_t header_chunk_size = 0;
	uint64_t body_size = 0;
	uint32_t body_crc = 0;
	uint32_t game_id = 0;
	uint32_t rcheevos_version = 0;
	uint32_t progress_size = 0;
	if (!Read32(bytes, chunk_offset + kVersionOffset, &version) ||
		!Read32(bytes, chunk_offset + kChunkSizeOffset, &header_chunk_size) ||
		!Read64(bytes, chunk_offset + kBodySizeOffset, &body_size) ||
		!Read32(bytes, chunk_offset + kBodyCrcOffset, &body_crc) ||
		!Read32(bytes, chunk_offset + kGameIdOffset, &game_id) ||
		!Read32(bytes, chunk_offset + kRcheevosVersionOffset,
			&rcheevos_version) ||
		!Read32(bytes, chunk_offset + kProgressSizeOffset, &progress_size)) {
		SetError(error, "truncated RA state chunk");
		return false;
	}
	if (version != kRaStateChunkVersion) {
		SetError(error, "unsupported RA state version");
		return false;
	}
	if (header_chunk_size != footer_chunk_size || body_size != chunk_offset ||
		progress_size > kRaStateMaxProgressSize ||
		static_cast<uint64_t>(kFixedChunkSize) + progress_size !=
			header_chunk_size) {
		SetError(error, "inconsistent RA state sizes");
		return false;
	}
	if (bytes[chunk_offset + kReservedOffset] != 0 ||
		bytes[chunk_offset + kReservedOffset + 1] != 0 ||
		bytes[chunk_offset + kReservedOffset + 2] != 0) {
		SetError(error, "invalid RA state reserved bytes");
		return false;
	}
	const uint32_t stored_chunk_crc = [&]() {
		uint32_t value = 0;
		Read32(bytes, footer_offset - 4, &value);
		return value;
	}();
	if (Crc32(bytes.data() + chunk_offset, footer_chunk_size - 4) !=
		stored_chunk_crc) {
		SetError(error, "RA state chunk CRC mismatch");
		return false;
	}
	if (Crc32(bytes.data(), chunk_offset) != body_crc) {
		SetError(error, "RA state body CRC mismatch");
		return false;
	}

	RaStateRecord parsed;
	parsed.mode = static_cast<RaStateMode>(bytes[chunk_offset + kModeOffset]);
	parsed.game_id = game_id;
	parsed.anchor_md5.assign(reinterpret_cast<const char *>(
		bytes.data() + chunk_offset + kMediaMd5Offset), 32);
	parsed.rcheevos_version = rcheevos_version;
	parsed.active_media_hash.assign(reinterpret_cast<const char *>(
		bytes.data() + chunk_offset + kActiveMediaHashOffset), 32);
	if (parsed.active_media_hash == std::string(32, '\0')) {
		parsed.active_media_hash.clear();
	}
	parsed.body.assign(bytes.begin(), bytes.begin() + chunk_offset);
	parsed.progress.assign(bytes.begin() + chunk_offset + kProgressOffset,
		bytes.begin() + chunk_offset + kProgressOffset + progress_size);
	if (!IsMd5(parsed.anchor_md5)) {
		SetError(error, "invalid RA state media MD5");
		return false;
	}
	if (((parsed.mode == RaStateMode::Casual ||
		parsed.mode == RaStateMode::HardcoreDebug) &&
		(parsed.game_id == 0 || parsed.progress.empty() ||
		 !IsMd5(parsed.active_media_hash))) ||
		(parsed.mode == RaStateMode::Offline &&
		(parsed.game_id != 0 || !parsed.progress.empty() ||
		 !parsed.active_media_hash.empty())) ||
		(parsed.mode != RaStateMode::Casual &&
		parsed.mode != RaStateMode::HardcoreDebug &&
		parsed.mode != RaStateMode::Offline)) {
		SetError(error, "invalid RA state mode payload");
		return false;
	}
	*record = std::move(parsed);
	SetError(error, std::string());
	return true;
}

bool ValidateRaState(const RaStateRecord& record,
	const RaStateExpectation& expected, std::string *error)
{
	if (record.mode != expected.mode) {
		SetError(error, "RA state mode does not match the current session");
		return false;
	}
	if (record.game_id != expected.game_id) {
		SetError(error, "RA state belongs to a different game");
		return false;
	}
	if (record.anchor_md5 != expected.anchor_md5) {
		SetError(error, "RA state belongs to different media");
		return false;
	}
	if (record.active_media_hash != expected.active_media_hash) {
		SetError(error, "RA state uses a different active media hash");
		return false;
	}
	if (record.rcheevos_version != expected.rcheevos_version) {
		SetError(error, "RA state uses a different rcheevos version");
		return false;
	}
	SetError(error, std::string());
	return true;
}

std::string RaStatePath(const std::string& ra_root, RaStateMode mode,
	uint32_t game_id, const std::string& anchor_md5, int slot)
{
	if (ra_root.empty() || !IsMd5(anchor_md5) || slot < 0 ||
		slot > kRaStateMaxSlot) {
		return std::string();
	}
	std::ostringstream directory;
	if (mode == RaStateMode::Casual && game_id != 0) {
		directory << game_id;
	}
	else if (mode == RaStateMode::HardcoreDebug && game_id != 0) {
		directory << "hardcore-debug/" << game_id;
	}
	else if (mode == RaStateMode::Offline && game_id == 0) {
		directory << "offline";
	}
	else {
		return std::string();
	}
	std::ostringstream filename;
	filename << "state" << slot << ".bin";
	return JoinPath(JoinPath(JoinPath(JoinPath(ra_root, "states"),
		directory.str()), anchor_md5), filename.str());
}

bool ReadRaStateFile(const std::string& path, std::vector<uint8_t> *bytes,
	std::string *error)
{
	if (bytes != nullptr) bytes->clear();
	if (bytes == nullptr) {
		SetError(error, "RA state output is required");
		return false;
	}
	if (!ReadRaFile(path, bytes, kRaStateMaxFileSize, error)) {
		return false;
	}
	SetError(error, std::string());
	return true;
}

bool WriteRaStateFileAtomically(const std::string& path,
	const std::vector<uint8_t>& bytes, std::string *error)
{
	if (path.empty()) {
		SetError(error, "RA state path is empty");
		return false;
	}
	if (bytes.size() > kRaStateMaxFileSize) {
		SetError(error, "RA state file is too large");
		return false;
	}
	if (!EnsureRaDirectoryTree(ParentPath(path), error)) {
		return false;
	}
	const std::string temporary = path + ".tmp";
	if (!WriteRaFile(temporary, bytes.data(), bytes.size(), error)) {
		RemoveRaFile(temporary, nullptr);
		return false;
	}
	if (!MoveRaFile(temporary, path, true, error)) {
		RemoveRaFile(temporary, nullptr);
		return false;
	}
	SetError(error, std::string());
	return true;
}

} // namespace Xm8Ra

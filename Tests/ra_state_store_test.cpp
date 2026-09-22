#include "ra_state_store.h"
#include "ra_file_util.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void Check(bool condition, const char *message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
}

Xm8Ra::RaStateRecord CasualRecord()
{
	Xm8Ra::RaStateRecord record;
	record.mode = Xm8Ra::RaStateMode::Casual;
	record.game_id = 1234;
	record.anchor_md5 = "0123456789abcdef0123456789abcdef";
	record.active_media_hash = "11111111111111111111111111111111";
	record.rcheevos_version = 12003000;
	record.body = {1, 2, 3, 4, 5, 6};
	record.progress = {9, 8, 7, 6, 5};
	return record;
}

bool Rejects(const std::vector<uint8_t>& bytes)
{
	Xm8Ra::RaStateRecord parsed;
	std::string error;
	return !Xm8Ra::ParseRaState(bytes, &parsed, &error) && !error.empty();
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

void Write32(std::vector<uint8_t> *bytes, size_t offset, uint32_t value)
{
	for (int shift = 0; shift < 32; shift += 8) {
		(*bytes)[offset++] = static_cast<uint8_t>(value >> shift);
	}
}

void RefreshChunkCrc(std::vector<uint8_t> *bytes, size_t body_size)
{
	const size_t footer_offset = bytes->size() - 8;
	Write32(bytes, footer_offset - 4,
		Crc32(bytes->data() + body_size, footer_offset - body_size - 4));
}

void TestRoundTrip()
{
	Xm8Ra::RaStateRecord source = CasualRecord();
	std::vector<uint8_t> bytes;
	std::string error;
	Check(Xm8Ra::BuildRaState(source, &bytes, &error), "build Casual state");
	Xm8Ra::RaStateRecord parsed;
	Check(Xm8Ra::ParseRaState(bytes, &parsed, &error), "parse Casual state");
	Check(parsed.mode == source.mode, "mode round trip");
	Check(parsed.game_id == source.game_id, "game ID round trip");
	Check(parsed.anchor_md5 == source.anchor_md5, "media MD5 round trip");
	Check(parsed.active_media_hash == source.active_media_hash,
		"active media hash round trip");
	Check(parsed.rcheevos_version == source.rcheevos_version,
		"rcheevos version round trip");
	Check(parsed.body == source.body, "body round trip");
	Check(parsed.progress == source.progress, "progress round trip");

	Xm8Ra::RaStateExpectation expected;
	expected.mode = source.mode;
	expected.game_id = source.game_id;
	expected.anchor_md5 = source.anchor_md5;
	expected.active_media_hash = source.active_media_hash;
	expected.rcheevos_version = source.rcheevos_version;
	Check(Xm8Ra::ValidateRaState(parsed, expected, &error),
		"matching expectation accepted");
	expected.game_id++;
	Check(!Xm8Ra::ValidateRaState(parsed, expected, &error),
		"different game rejected");
	expected.game_id = source.game_id;
	expected.anchor_md5 = "fedcba9876543210fedcba9876543210";
	Check(!Xm8Ra::ValidateRaState(parsed, expected, &error),
		"different media rejected");
	expected.anchor_md5 = source.anchor_md5;
	expected.active_media_hash[0] = '2';
	Check(!Xm8Ra::ValidateRaState(parsed, expected, &error),
		"different active media hash rejected");
	expected.active_media_hash = source.active_media_hash;
	expected.rcheevos_version++;
	Check(!Xm8Ra::ValidateRaState(parsed, expected, &error),
		"different rcheevos version rejected");

	source.mode = Xm8Ra::RaStateMode::HardcoreDebug;
	Check(Xm8Ra::BuildRaState(source, &bytes, &error),
		"build Hardcore Debug state");
	Check(Xm8Ra::ParseRaState(bytes, &parsed, &error),
		"parse Hardcore Debug state");
	Check(parsed.mode == Xm8Ra::RaStateMode::HardcoreDebug,
		"Hardcore Debug mode round trip");
	Check(parsed.progress == source.progress,
		"Hardcore Debug hit counts round trip");
	expected.mode = Xm8Ra::RaStateMode::HardcoreDebug;
	expected.rcheevos_version = source.rcheevos_version;
	Check(Xm8Ra::ValidateRaState(parsed, expected, &error),
		"matching Hardcore Debug expectation accepted");
	expected.mode = Xm8Ra::RaStateMode::Casual;
	Check(!Xm8Ra::ValidateRaState(parsed, expected, &error),
		"Hardcore Debug state rejected as a normal Casual state");
}

void TestOfflineAndInvalidPayloads()
{
	Xm8Ra::RaStateRecord offline = CasualRecord();
	offline.mode = Xm8Ra::RaStateMode::Offline;
	offline.game_id = 0;
	offline.progress.clear();
	offline.active_media_hash.clear();
	std::vector<uint8_t> bytes;
	std::string error;
	Check(Xm8Ra::BuildRaState(offline, &bytes, &error), "build Offline state");
	Xm8Ra::RaStateRecord parsed;
	Check(Xm8Ra::ParseRaState(bytes, &parsed, &error), "parse Offline state");
	offline.active_media_hash = "11111111111111111111111111111111";
	Check(!Xm8Ra::BuildRaState(offline, &bytes, &error),
		"Offline state with active media hash rejected");
	offline.active_media_hash.clear();
	offline.progress.push_back(1);
	Check(!Xm8Ra::BuildRaState(offline, &bytes, &error),
		"Offline progress rejected");
	Xm8Ra::RaStateRecord casual = CasualRecord();
	casual.progress.clear();
	Check(!Xm8Ra::BuildRaState(casual, &bytes, &error),
		"Casual state without progress rejected");
	casual = CasualRecord();
	casual.active_media_hash.clear();
	Check(!Xm8Ra::BuildRaState(casual, &bytes, &error),
		"Casual state without active media hash rejected");
	casual = CasualRecord();
	casual.anchor_md5[0] = 'A';
	bytes = {1, 2, 3};
	Check(!Xm8Ra::BuildRaState(casual, &bytes, &error),
		"noncanonical MD5 rejected");
	Check(bytes.empty(), "failed build clears output");
}

void TestCorruptionRejection()
{
	std::vector<uint8_t> valid;
	std::string error;
	Check(Xm8Ra::BuildRaState(CasualRecord(), &valid, &error),
		"build corruption fixture");
	Check(Rejects(std::vector<uint8_t>{1, 2, 3}), "legacy state rejected");

	std::vector<uint8_t> damaged = valid;
	damaged.resize(damaged.size() - 1);
	Check(Rejects(damaged), "truncated footer rejected");
	damaged = valid;
	damaged[damaged.size() - 1] ^= 0x40;
	Check(Rejects(damaged), "footer size mismatch rejected");
	damaged = valid;
	damaged[CasualRecord().body.size() + 4] = 3;
	Check(Rejects(damaged), "unknown chunk version rejected");
	damaged = valid;
	damaged[0] ^= 0x80;
	Check(Rejects(damaged), "body CRC mismatch rejected");
	damaged = valid;
	damaged[CasualRecord().body.size() + 104] ^= 0x80;
	Check(Rejects(damaged), "progress/chunk CRC mismatch rejected");
	damaged = valid;
	damaged[CasualRecord().body.size() + 29] = 1;
	Check(Rejects(damaged), "reserved byte rejected");
	damaged = valid;
	damaged[CasualRecord().body.size() + 28] = 4;
	RefreshChunkCrc(&damaged, CasualRecord().body.size());
	Check(Rejects(damaged), "unknown saved mode rejected");
}

void TestPathsAndAtomicFile()
{
	const std::string md5 = "0123456789abcdef0123456789abcdef";
	const char *temporary = std::getenv(
#ifdef _WIN32
		"TEMP"
#else
		"TMPDIR"
#endif
	);
	const auto unique = std::chrono::steady_clock::now()
		.time_since_epoch().count();
	const std::string root = std::string(temporary != nullptr ? temporary :
#ifdef _WIN32
		"."
#else
		"/tmp"
#endif
	) + "/xm8-ra-state-store-" + std::to_string(unique);
	const std::string casual = Xm8Ra::RaStatePath(root,
		Xm8Ra::RaStateMode::Casual, 42, md5, 3);
	const std::string offline = Xm8Ra::RaStatePath(root,
		Xm8Ra::RaStateMode::Offline, 0, md5, 3);
	const std::string hardcore_debug = Xm8Ra::RaStatePath(root,
		Xm8Ra::RaStateMode::HardcoreDebug, 42, md5, 3);
	Check(casual.find("/states/42/" + md5 + "/state3.bin") !=
		std::string::npos, "Casual path separated by game");
	Check(offline.find("/states/offline/" + md5 + "/state3.bin") !=
		std::string::npos, "Offline path separated");
	Check(hardcore_debug.find("/states/hardcore-debug/42/" + md5 +
		"/state3.bin") != std::string::npos,
		"Hardcore Debug path separated by game");
	Check(casual != offline, "Casual and Offline paths differ");
	Check(casual != hardcore_debug && offline != hardcore_debug,
		"Hardcore Debug path is isolated");
	Check(Xm8Ra::RaStatePath(root, Xm8Ra::RaStateMode::Casual, 0, md5, 1)
		.empty(), "Casual path requires game ID");
	Check(Xm8Ra::RaStatePath(root, Xm8Ra::RaStateMode::Casual, 42, md5, 10)
		.empty(), "slot above UI range rejected");

	const std::vector<uint8_t> source = {4, 3, 2, 1};
	std::string error;
	Check(Xm8Ra::WriteRaStateFileAtomically(casual, source, &error),
		"atomic state write");
	std::vector<uint8_t> loaded;
	Check(Xm8Ra::ReadRaStateFile(casual, &loaded, &error), "state file read");
	Check(loaded == source, "atomic file round trip");
	const std::vector<uint8_t> replacement = {8, 9};
	Check(Xm8Ra::WriteRaStateFileAtomically(casual, replacement, &error),
		"atomic state replacement");
	Check(Xm8Ra::ReadRaStateFile(casual, &loaded, &error) &&
		loaded == replacement, "replacement is complete");
	Check(Xm8Ra::RemoveRaTree(root, &error), "remove state test directory");
	Check(!Xm8Ra::ReadRaStateFile(casual, &loaded, &error),
		"missing state file rejected");
	Check(loaded.empty(), "failed read clears output");
}

} // namespace

int main()
{
	TestRoundTrip();
	TestOfflineAndInvalidPayloads();
	TestCorruptionRejection();
	TestPathsAndAtomicFile();
	if (failures != 0) {
		std::cerr << failures << " RA state store test(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "RA state store tests passed\n";
	return EXIT_SUCCESS;
}

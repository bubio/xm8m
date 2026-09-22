#include "Fixtures/ra_library_fixture_seed.h"
#include "ra_file_util.h"
#include "ra_library.h"
#include "ra_media_store.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
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

std::string JoinPath(const std::string& dir, const char *name)
{
	return !dir.empty() && dir.back() == '/' ? dir + name : dir + "/" + name;
}

bool MutateWorkingDisk(const std::string& path)
{
	std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
	char value = 0;
	if (!file.is_open()) return false;
	// D88 header is 0x2b0 bytes and the first sector header is 16 bytes.
	// Change sector payload, matching an emulator-originated disk write.
	file.seekg(0x2c0);
	if (!file.read(&value, 1)) return false;
	value ^= 0x01;
	file.seekp(0x2c0);
	file.write(&value, 1);
	return file.good();
}

} // namespace

int main()
{
	const auto unique =
		std::chrono::steady_clock::now().time_since_epoch().count();
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
	) + "/xm8-ra-working-identity-" + std::to_string(unique);
	const std::string ra_root = JoinPath(base, "ra");

	RaLibraryFixtureSeed::SeedResult seed;
	std::string error;
	Check(RaLibraryFixtureSeed::Seed(ra_root, &seed, &error),
		"seed RA library");
	Xm8Ra::RaLibrary library;
	Check(library.Open(ra_root, &error), "open RA library");
	Xm8Ra::RaMediaStore store(&library);
	std::vector<Xm8Ra::RaLibraryGameListItem> games;
	Check(library.ListGames(&games, &error), "list games");

	Xm8Ra::ResolvedLaunchProfile before;
	bool found = false;
	for (const Xm8Ra::RaLibraryGameListItem& game : games) {
		if (game.title == "RA Test Multi Disk") {
			found = store.ResolveLaunchProfile(game.game_id, &before, &error);
			break;
		}
	}
	Check(found, "resolve multi-disk launch profile");
	Check(!before.drives[0].ra_hash.empty(), "anchor has stored RA hash");
	Check(!before.drives[1].ra_hash.empty(), "auxiliary has stored RA hash");
	const std::string anchor_hash = before.drives[0].ra_hash;
	const std::string auxiliary_hash = before.drives[1].ra_hash;

	Check(MutateWorkingDisk(before.drives[0].working_path),
		"simulate an in-game write to the working disk");
	Xm8Ra::ResolvedWorkingMedia mounted;
	Check(store.ResolveWorkingMedia(before.drives[0].working_path,
		before.drives[0].bank_index, &mounted, &error),
		"resolve mutated working disk by registered identity");
	Check(mounted.ra_hash == anchor_hash,
		"mutated working disk retains original RA hash");

	Xm8Ra::ResolvedLaunchProfile after;
	Check(store.ResolveLaunchProfile(before.game_id, &after, &error),
		"resolve launch profile after working-disk write");
	Check(after.drives[0].ra_hash == anchor_hash,
		"library relaunch retains anchor RA hash");
	Check(after.drives[1].ra_hash == auxiliary_hash,
		"library relaunch retains auxiliary RA hash");

	library.Close();
	Xm8Ra::RemoveRaTree(base);
	if (failures != 0) return EXIT_FAILURE;
	std::cout << "RA working media identity tests passed\n";
	return EXIT_SUCCESS;
}

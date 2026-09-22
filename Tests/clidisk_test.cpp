#include "clidisk.h"
#include "m3u.h"

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

CliOptions Parse(std::initializer_list<const char*> arguments)
{
	std::vector<std::string> storage;
	std::vector<char*> argv;

	for (const char *argument : arguments) {
		storage.emplace_back(argument);
	}
	for (std::string& argument : storage) {
		argv.push_back(argument.data());
	}
	return ParseCommandLine(static_cast<int>(argv.size()), argv.data());
}

void Check(bool condition, const char *message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		failures++;
	}
}

void CheckError(std::initializer_list<const char*> arguments, const char *message)
{
	Check(Parse(arguments).action == CliAction::Error, message);
}

bool WriteTextFile(const std::string& path, const std::string& content)
{
	std::ofstream file(path);
	if (!file.is_open()) {
		return false;
	}
	file << content;
	return file.good();
}

bool WriteBinaryFile(const std::string& path, const std::string& content)
{
	std::ofstream file(path, std::ios::binary);
	if (!file.is_open()) return false;
	file.write(content.data(), static_cast<std::streamsize>(content.size()));
	return file.good();
}

} // namespace

int main()
{
	{
		const DiskSpec drive2{"game.d88", 1, 0};
		Check(drive2.path == "game.d88" && drive2.drive == 1 &&
			drive2.bank == 0,
			"DiskSpec positional construction uses path, drive, bank order");
	}
	{
		CliOptions options = Parse({"xm8"});
		Check(options.action == CliAction::Run, "no arguments action");
		Check(options.disks.empty(), "no arguments disks");
	}
	{
		CliOptions options = Parse({"xm8", "one.d88", "two.d88#1"});
		Check(options.disks.size() == 2, "two disks parsed");
		Check(options.disks[0].drive == 0 && options.disks[0].bank == 0,
			"drive 0 defaults");
		Check(options.disks[1].drive == 1 && options.disks[1].bank == 1,
			"drive 1 bank");
	}
	CheckError({"xm8", "one", "two", "three"}, "reject three disks");
	{
		CliOptions hash = Parse({"xm8", "game.d88#12"});
		CliOptions colon = Parse({"xm8", "game.d88:1"});
		CliOptions zero_hash = Parse({"xm8", "game.d88#0"});
		CliOptions zero_colon = Parse({"xm8", "game.d88:0"});
		Check(hash.disks[0].path == "game.d88" && hash.disks[0].bank == 12,
			"hash bank");
		Check(colon.disks[0].path == "game.d88" && colon.disks[0].bank == 1,
			"colon bank");
		Check(zero_hash.disks[0].bank == 0, "zero hash bank");
		Check(zero_colon.disks[0].bank == 0, "zero colon bank");
	}
	{
		CliOptions drive = Parse({"xm8", "C:\\disk.d88"});
		CliOptions bank = Parse({"xm8", "C:\\disk.d88:1"});
		Check(drive.disks[0].path == "C:\\disk.d88" &&
			drive.disks[0].bank == 0, "windows drive");
		Check(bank.disks[0].path == "C:\\disk.d88" &&
			bank.disks[0].bank == 1, "windows drive bank");
	}
	CheckError({"xm8", ""}, "reject empty path");
	CheckError({"xm8", "game.d88#"}, "reject empty bank");
	CheckError({"xm8", "game.d88#-1"}, "reject negative bank");
	CheckError({"xm8", "game.d88#x"}, "reject non-numeric bank");
	CheckError({"xm8", "game.d88#2147483648"}, "reject bank overflow");
	Check(Parse({"xm8", "game.d88#2147483647"}).disks[0].bank == INT_MAX,
		"accept INT_MAX bank");
	{
		CliOptions options = Parse({"xm8", "/tmp/a#b:c/日本 語.d88"});
		Check(options.disks[0].path == "/tmp/a#b:c/日本 語.d88",
			"separators in parent path");
	}
	CheckError({"xm8", "game#name.d88"}, "reject hash in basename");
	CheckError({"xm8", "game:name.d88"}, "reject colon in basename");
	{
		CliOptions options = Parse({"xm8", "--", "-game.d88"});
		Check(options.disks.size() == 1 &&
			options.disks[0].path == "-game.d88", "double dash");
	}
	{
		const std::string playlist = "./clidisk_test_playlist.m3u";
		Check(WriteTextFile(playlist,
			"#EXTM3U\n"
			"  disk1.d88#1  \n"
			"\n"
			"disk2.d88\n"
			"disk3.d88\n"),
			"write m3u playlist");

		CliOptions options = Parse({"xm8", playlist.c_str()});
		Check(options.action == CliAction::Run, "m3u action");
		Check(options.disks.size() == 2, "m3u loads first two disks");
		Check(options.disks[0].path == "./disk1.d88" &&
			options.disks[0].drive == 0 &&
			options.disks[0].bank == 1, "m3u first disk");
		Check(options.disks[1].path == "./disk2.d88" &&
			options.disks[1].drive == 1 &&
			options.disks[1].bank == 0, "m3u second disk");

		std::remove(playlist.c_str());
	}
	CheckError({"xm8", "./clidisk_test_missing.m3u"}, "missing m3u");
	{
		const std::string m3u8 = "./clidisk_test_playlist.m3u8";
		Check(WriteBinaryFile(m3u8, "\xef\xbb\xbfutf8.d88\n"), "write UTF-8 BOM m3u8");
		CliOptions options = Parse({"xm8", m3u8.c_str()});
		Check(options.action == CliAction::Run && options.disks.size() == 1 &&
			options.disks[0].path == "./utf8.d88", "m3u8 UTF-8 BOM");
		std::remove(m3u8.c_str());
	}
	{
		const std::string utf16 = "./clidisk_test_utf16.m3u";
		const char utf16_bytes[] = "\xff\xfe" "u\0t\0f\0" "1\0" ".\0d\0" "8\0" "8\0\n\0";
		Check(WriteBinaryFile(utf16, std::string(utf16_bytes, sizeof(utf16_bytes) - 1)),
			"write UTF-16 m3u");
		M3UResult playlist = LoadM3U(utf16);
		Check(playlist.success && playlist.entries.size() == 1 &&
			playlist.entries[0] == "./utf1.d88", "m3u UTF-16 LE");
		std::remove(utf16.c_str());
	}
	{
		const std::string sjis = "./clidisk_test_sjis.m3u";
		Check(WriteBinaryFile(sjis, "\x82\xa0.d88\n"), "write Shift-JIS m3u");
		M3UResult playlist = LoadM3U(sjis);
		Check(playlist.success && playlist.entries.size() == 1 &&
			playlist.entries[0] == "./\xe3\x81\x82.d88", "m3u Shift-JIS fallback");
		std::remove(sjis.c_str());
	}
	{
		const std::string invalid = "./clidisk_test_invalid.m3u8";
		Check(WriteBinaryFile(invalid, "\x82\xa0.d88\n"), "write invalid m3u8");
		Check(!LoadM3U(invalid).success, "m3u8 rejects Shift-JIS fallback");
		std::remove(invalid.c_str());
	}
	for (const char *value : {"V1S", "v1h", "V2", "n"}) {
		Check(Parse({"xm8", "--system", value}).action == CliAction::Run,
			"valid system");
	}
	for (const char *value : {"4", "4mhz", "8", "8MHz", "8h", "8mhzh"}) {
		Check(Parse({"xm8", "--clock", value}).action == CliAction::Run,
			"valid clock");
	}
	CheckError({"xm8", "--foo"}, "unknown option");
	CheckError({"xm8", "--system", "V3"}, "invalid system");
	CheckError({"xm8", "--clock", "16MHz"}, "invalid clock");
	CheckError({"xm8", "--system"}, "missing system");
	CheckError({"xm8", "--clock"}, "missing clock");
	CheckError({"xm8", "--system", "V2", "--system", "N"},
		"duplicate system");
	CheckError({"xm8", "--clock", "4", "--clock", "8"}, "duplicate clock");
	Check(Parse({"xm8", "--help"}).action == CliAction::ShowHelp, "help");
	Check(Parse({"xm8", "-h"}).action == CliAction::ShowHelp, "short help");
	Check(Parse({"xm8", "--version"}).action == CliAction::ShowVersion,
		"version");
	CheckError({"xm8", "--help", "game.d88"}, "help must be alone");
	CheckError({"xm8", "--version", "--help"}, "actions conflict");
	Check(Parse({"xm8", "--system", "v1s"}).system == CliSystemMode::V1S,
		"system enum");
	Check(Parse({"xm8", "--clock", "8h"}).clock == CliClockMode::Clock8MHzH,
		"clock enum");

	if (failures != 0) {
		std::cerr << failures << " test(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "clidisk tests passed\n";
	return EXIT_SUCCESS;
}

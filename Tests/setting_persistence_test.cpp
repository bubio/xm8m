// Never calls Setting::Init/Deinit or resolves the user's settings directory.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>
#define SDL_MAIN_HANDLED
#include "os.h"
#include "common.h"
#include "fileio.h"
#include "config.h"
#include "setting.h"

// Setting uses the shared config, without needing the emulator core at runtime.
config_t config;

namespace {

void Require(bool ok, const std::string& message)
{
	if (!ok) {
		std::cerr << "FAIL: " << message << '\n';
		std::exit(1);
	}
}

struct TemporaryFile {
	std::string path;
	TemporaryFile()
	{
		const char *directory = std::getenv(
#ifdef _WIN32
			"TEMP"
#else
			"TMPDIR"
#endif
		);
		path = std::string(directory ? directory : ".") + "/xm8-setting-test-" +
			std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
	}
	~TemporaryFile() { std::remove(path.c_str()); }
};

using Bytes = std::vector<unsigned char>;

Bytes Save(Setting& setting)
{
	TemporaryFile temporary;
	FILEIO file;
	Require(file.Fopen(const_cast<char *>(temporary.path.c_str()), FILEIO_WRITE_BINARY), "open output");
	setting.SaveSetting(&file);
	file.Fclose();
	Require(!file.HasError(), "save and close settings");
	std::ifstream input(temporary.path, std::ios::binary);
	return Bytes(std::istreambuf_iterator<char>(input), {});
}

bool Load(Setting& setting, const Bytes& bytes, uint32 *position = nullptr)
{
	TemporaryFile temporary;
	{
		std::ofstream output(temporary.path, std::ios::binary);
		if (!bytes.empty()) output.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
		Require(output.good(), "write input fixture");
	}
	FILEIO file;
	Require(file.Fopen(const_cast<char *>(temporary.path.c_str()), FILEIO_READ_BINARY), "open input");
	const bool loaded = setting.LoadSetting(&file);
	if (position) *position = file.Ftell();
	return loaded;
}

void Put32(Bytes& bytes, size_t offset, uint32 value)
{
	Require(offset + sizeof(value) <= bytes.size(), "fixture field fits");
	std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void Configure(Setting& setting, bool alternate = false)
{
	setting.SetSystemMode(alternate ? SETTING_V1H_MODE : SETTING_N_MODE);
	setting.SetCPUClock(alternate ? 4 : 8);
	setting.SetDip(alternate ? DIP_LINE_25 : DIP_WIDTH_40 | DIP_CLOCK_8MHZH);
	setting.SetWindowWidth(alternate ? 640 : 1280);
	setting.SetSkipFrame(alternate ? 0 : 3);
	setting.SetLowReso(!alternate);
	setting.SetScanline(alternate);
	setting.SetBrightness(alternate ? 255 : 128);
	setting.SetAudioFreq(alternate ? 44100 : 48000);
	setting.SetAudioBuffer(alternate ? 120 : 200);
	setting.SetOPNA(!alternate);
	for (int i = 0; i < 4; ++i) setting.SetSoftKeySet(i, alternate ? i : 10 - i);
	if (!alternate) Require(setting.NextSoftKey(), "select a nonzero softkey slot");
	setting.SetSoftKeyAlpha(alternate ? 96 : 180);
	setting.SetSoftKeyTime(alternate ? 4000 : 5000);
	setting.SetJoyEnable(alternate);
	setting.SetJoySwap(!alternate);
	setting.SetJoyKey(alternate);
	setting.SetMouseTime(alternate ? 3000 : 6000);
	setting.SetStateNum(alternate ? 0 : 9);
	setting.SetStatusLine(alternate);
	setting.SetStatusAlpha(alternate ? 96 : 70);
	setting.SetScaleQuality(1); // Avoid the intentional non-Windows 2 -> 1 conversion.
	setting.SetKeyEnable(alternate);
	setting.SetFastDisk(!alternate);
	setting.SetForceRGB565(alternate);
	setting.SetWatchBattery(alternate);
	for (int i = 0; i < 15; ++i) setting.SetJoystickToKey(i, alternate ? 0x1000 : 0x0100 + i);
	setting.SetAudioDevice(alternate ? 0 : 2);
	setting.SetShowSoftKeyMouseMove(!alternate);
	setting.SetCursorToNumPad(!alternate);
	setting.SetNumToNumPad(!alternate);
	setting.SetRotationMode(alternate ? Setting::RotationAuto : Setting::RotationPortrait);
}

void RejectUnchanged(const Bytes& bytes, const std::string& label)
{
	Setting target;
	Configure(target, true);
	config.cpu_power = 17; // An unrelated shared-config field must survive too.
	const auto before = Save(target);
	Require(!Load(target, bytes), label + " must be rejected");
	Require(Save(target) == before, label + " must not partially replace settings");
	Require(config.cpu_power == 17, label + " must retain unrelated config");
}

} // namespace

int main()
{
	Setting source;
	Configure(source);
	const Bytes valid = Save(source);
	Require(valid.size() == 159, "current settings format remains 159 bytes");
	uint32 version;
	std::memcpy(&version, valid.data(), sizeof(version));
	Require(version == 20260801, "current wire-format version");
	{
		Setting target;
		Configure(target, true);
		config.cpu_power = 17;
		Require(Load(target, valid), "load complete settings");
		Require(target.GetSystemMode() == SETTING_N_MODE && target.GetCPUClock() == 8,
			"restore machine settings");
		Require(target.GetWindowWidth() == 1280 && target.GetSkipFrame() == 3 && target.IsLowReso(),
			"restore video settings");
		Require(target.GetAudioFreq() == 48000 && target.GetAudioBuffer() == 200 && target.HasOPNA(),
			"restore audio settings");
		Require(target.GetSoftKeyType() == 9 && target.GetSoftKeySet(3) == 7,
			"restore softkey index and layouts");
		Require(!target.IsJoyEnable() && target.IsJoySwap() && !target.IsKeyEnable(), "restore input flags");
		Require(target.GetJoystickToKey(14) == 0x010e && target.GetStateNum() == 9,
			"restore key mapping and last slot");
		Require(target.IsCursorToNumPad() && target.IsNumToNumPad() &&
			target.GetRotationMode() == Setting::RotationPortrait, "restore recent format extensions");
		Require(config.cpu_power == 17, "successful load retains unrelated config");
		Require(Save(target) == valid, "all serialized fields survive a round trip");
	}

	for (size_t length = 0; length < valid.size(); ++length) {
		RejectUnchanged(Bytes(valid.begin(), valid.begin() + length), "truncation at " + std::to_string(length));
	}
	for (uint32 bad_version : {0U, 20150620U, 20260802U, 0xffffffffU}) {
		auto corrupt = valid;
		Put32(corrupt, 0, bad_version);
		RejectUnchanged(corrupt, "unsupported version " + std::to_string(bad_version));
	}
	// Fixed offsets also cover byte-valued flags; arbitrary bool representations
	// must not enter a C++ bool object during deserialization.
	for (size_t offset : {28U, 67U, 68U, 69U, 78U, 84U, 85U, 86U, 87U, 152U, 153U, 154U}) {
		for (unsigned char invalid : {2, 255}) {
			auto corrupt = valid;
			corrupt[offset] = invalid;
			RejectUnchanged(corrupt, "invalid bool at " + std::to_string(offset));
		}
	}
	for (size_t offset : {4U, 8U, 24U, 38U, 42U, 46U, 50U, 54U, 58U, 80U}) {
		for (uint32 invalid : {0xffffffffU, 0x7fffffffU}) {
			auto corrupt = valid;
			Put32(corrupt, offset, invalid);
			RejectUnchanged(corrupt, "invalid enum/index at " + std::to_string(offset));
		}
	}
	for (size_t offset : {16U, 30U, 34U}) {
		for (uint32 invalid : {0U, 0xffffffffU}) {
			auto corrupt = valid;
			Put32(corrupt, offset, invalid);
			RejectUnchanged(corrupt, "nonpositive dimension/audio value at " + std::to_string(offset));
		}
	}
	for (const auto field : {std::pair<size_t, uint32>{20, 4}, {74, 10}, {148, 0xffffffffU}}) {
		auto corrupt = valid;
		Put32(corrupt, field.first, field.second);
		RejectUnchanged(corrupt, "out-of-range selection at " + std::to_string(field.first));
	}

	// Each historical version ends at its last supported extension. New fields
	// retain the constructor defaults; legacy files need no new trailing bytes.
	struct Legacy { uint32 version; size_t size; };
	for (const auto legacy : {Legacy{20150621, 78}, {20150624, 84}, {20150626, 85},
		{20150710, 87}, {20150822, 88}, {20151020, 148}, {20180120, 148},
		{20230401, 153}, {20230501, 155}}) {
		auto bytes = valid;
		bytes.resize(legacy.size);
		Put32(bytes, 0, legacy.version);
		Setting target;
		Require(Load(target, bytes), "load legacy version " + std::to_string(legacy.version));
		Require(target.GetSystemMode() == SETTING_N_MODE && target.GetAudioFreq() == 48000,
			"legacy common fields");
		Require(target.GetRotationMode() == Setting::RotationAuto, "legacy rotation default");
		if (legacy.version < 20150624) {
			Require(target.HasStatusLine() && target.GetStatusAlpha() == 96 &&
				std::strcmp(target.GetScaleQuality(), "2") == 0, "legacy status/scaling defaults");
		}
		if (legacy.version < 20150626) {
#ifdef __ANDROID__
			Require(!target.IsKeyEnable(), "legacy Android keyboard default");
#else
			Require(target.IsKeyEnable(), "legacy desktop keyboard default");
#endif
		}
		if (legacy.version < 20150710) {
			Require(!target.IsFastDisk() && target.IsForceRGB565(), "legacy disk/video defaults");
		}
		if (legacy.version < 20150822) Require(target.IsWatchBattery(), "legacy battery default");
		if (legacy.version < 20151020) {
			Require(target.GetJoystickToKey(0) == 0x0100 && target.GetJoystickToKey(14) == 0x1000,
				"legacy joystick map defaults");
		} else {
			Require(target.GetJoystickToKey(14) == 0x010e, "legacy joystick map restored");
		}
		if (legacy.version < 20230401) {
			Require(target.GetAudioDevice() == 0 && !target.IsShowSoftKeyMouseMove(),
				"legacy audio device/mouse defaults");
		} else {
			Require(target.GetAudioDevice() == 2 && target.IsShowSoftKeyMouseMove(),
				"legacy audio device/mouse restored");
		}
		if (legacy.version < 20230501) {
			Require(!target.IsCursorToNumPad() && !target.IsNumToNumPad(), "legacy keypad defaults");
		} else {
			Require(target.IsCursorToNumPad() && target.IsNumToNumPad(), "legacy keypad restored");
		}
		RejectUnchanged(Bytes(bytes.begin(), bytes.end() - 1), "truncated legacy record");
	}
	{
		Setting target;
		auto stream = valid;
		stream.insert(stream.end(), {0x51, 0x62, 0x73, 0x84});
		uint32 consumed = 0;
		Require(Load(target, stream, &consumed) && consumed == valid.size(),
			"embedded settings leave the following state body unread");
	}
	{
		Setting target;
		Configure(target, true);
		const auto before = Save(target);
		FILEIO unopened;
		Require(!target.LoadSetting(nullptr) && !target.LoadSetting(&unopened),
			"missing input stream is rejected without scalar I/O assertions");
		Require(Save(target) == before, "missing stream preserves settings");
	}
	for (uint32 invalid_rotation : {0xffffffffU, 3U}) {
		auto bytes = valid;
		Put32(bytes, 155, invalid_rotation);
		Setting target;
		Require(Load(target, bytes) && target.GetRotationMode() == Setting::RotationAuto,
			"retain existing invalid-rotation normalization");
	}
	{
		auto bytes = valid;
		Put32(bytes, 80, 2);
		Setting target;
		Require(Load(target, bytes), "load legacy scaling preference");
#ifdef _WIN32
		Require(std::strcmp(target.GetScaleQuality(), "2") == 0, "Windows keeps quality 2");
#else
		Require(std::strcmp(target.GetScaleQuality(), "1") == 0, "non-Windows normalizes quality 2");
#endif
	}
	std::cout << "Settings round trip, all truncation boundaries, corruption, legacy versions and embedded state passed\n";
}

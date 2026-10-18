// Exercise production startup, event loop shutdown and persisted session state.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#define SDL_MAIN_HANDLED
#include "os.h"
#include "common.h"
#include "app.h"
#include "setting.h"
#include "input.h"
#include "menuid.h"
#include "emu.h"
#include "ra_file_util.h"
#include "lifecycle_host.h"
namespace {
using Bytes = std::vector<unsigned char>;
void Require(bool ok, const std::string& message)
{
	if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
Bytes Read(const std::string& path)
{
	std::ifstream input(path, std::ios::binary);
	Require(input.good(), "read test file " + path);
	return Bytes(std::istreambuf_iterator<char>(input), {});
}
void SyntheticHostData(const std::string& root)
{
	Require(Xm8Ra::EnsureRaDirectoryTree(root), "create isolated host directory");
	// Generated zero bytes are sufficient for the file-presence startup checks.
	// These are synthetic test inputs, not ROM dumps, and no VM program is run.
	for (const char *name : {"PC88.ROM", "KANJI1.ROM"}) {
		std::ofstream output(root + "/" + name, std::ios::binary);
		const Bytes zeros(0x20000, 0);
		output.write(reinterpret_cast<const char*>(zeros.data()), zeros.size());
		Require(output.good(), "write synthetic host data");
	}
}
void QuitThroughEventLoop(App& app)
{
	SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
	SDL_Event quit{}; quit.type = SDL_QUIT;
	Require(SDL_PushEvent(&quit) == 1, "enqueue quit event");
	app.Run(); // Default startup, or explicitly paused CLI startup, never runs a synthetic program.
}
void Clean(App& app)
{
	app.Deinit(); app.Deinit();
	Require(app.GetSetting() == nullptr && app.GetInput() == nullptr &&
		app.GetAudio() == nullptr && app.GetVideo() == nullptr, "partial/full teardown is repeatable");
	Require(LifecycleHost::LiveAudioDevices() == 0, "audio resources released");
}
}
int main(int, char**)
{
	SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
	SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
	SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
	Require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) == 0, "headless SDL backend");
	const char *temporary = std::getenv(
#ifdef _WIN32
		"TEMP"
#else
		"TMPDIR"
#endif
	);
	const std::string root = std::string(temporary ? temporary : ".") + "/xm8-lifecycle-" +
		std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
	SyntheticHostData(root);
	LifecycleHost::Configure(root);
	{
		App app;
		Require(app.Init({}), "first launch with no saved setting or state");
		Require(!app.IsRaModeEnabled(), "runtime RA OFF");
		Require(app.GetSetting()->GetSystemMode() == SETTING_V2_MODE &&
			app.GetSetting()->GetCPUClock() == 4, "first-launch system defaults");
		app.GetSetting()->SetWindowWidth(800);
		app.GetSetting()->SetCursorToNumPad(true);
		app.SetWindowWidth();
		QuitThroughEventLoop(app);
		Clean(app);
	}
	Bytes automatic = Read(root + "/state0.bin");
	Require(automatic.size() > 65536, "Run shutdown writes complete automatic state");
	const Bytes saved_settings = Read(root + "/setting.bin");
	Require(saved_settings.size() == 159, "Deinit writes current settings format");
	{
		App app;
		Require(app.Init({}), "second launch loads saved settings");
		Require(app.GetSetting()->GetWindowWidth() == 800 &&
			app.GetSetting()->IsCursorToNumPad(), "settings survive shutdown and startup");
		app.GetSetting()->SetWindowWidth(640);
		app.GetSetting()->SetCursorToNumPad(false);
		QuitThroughEventLoop(app);
		Require(app.GetSetting()->GetWindowWidth() == 800 &&
			app.GetSetting()->IsCursorToNumPad(), "Run startup restores automatic state");
		app.GetInput()->OnKeyDown(false, SDL_SCANCODE_LEFT);
		Require(app.GetEmu()->key_buffer()[0x64] != 0, "automatic load reapplies persisted input mapping");
		app.GetInput()->OnKeyUp(false, SDL_SCANCODE_LEFT);
		Clean(app);
	}
	automatic = Read(root + "/state0.bin"); // Snapshot after legitimate RTC resync/re-save.
	const int modes[] = {SETTING_V1S_MODE, SETTING_V1H_MODE, SETTING_V2_MODE, SETTING_N_MODE};
	const CliSystemMode cli_modes[] = {CliSystemMode::V1S, CliSystemMode::V1H, CliSystemMode::V2, CliSystemMode::N};
	const CliClockMode clocks[] = {CliClockMode::Clock4MHz, CliClockMode::Clock8MHz, CliClockMode::Clock8MHzH};
	for (int mode = 0; mode < 4; ++mode) for (int clock = 0; clock < 3; ++clock) {
		CliOptions options; options.system = cli_modes[mode]; options.clock = clocks[clock];
		App app;
		Require(app.Init(options), "CLI session initialization");
		Require(app.GetSetting()->GetSystemMode() == modes[mode] &&
			app.GetSetting()->GetCPUClock() == (clock == 0 ? 4 : 8) &&
			app.GetSetting()->Is8HMode() == (clock == 2), "CLI overrides applied to runtime");
		app.GetSetting()->SetBrightness(123); // Unrelated changes must still persist.
		app.EnterMenu(MENU_MAIN); // Keep the synthetic VM paused while exercising Run shutdown.
		QuitThroughEventLoop(app);
		Clean(app);
		Setting reload;
		Require(reload.Init(), "read persisted settings after CLI session");
		Require(reload.GetSystemMode() == SETTING_V2_MODE && reload.GetCPUClock() == 4 &&
			!reload.Is8HMode(), "CLI system/clock overrides are not persisted");
		Require(reload.GetBrightness() == 123, "unrelated setting persists through CLI cleanup");
		Require(Read(root + "/state0.bin") == automatic, "CLI session does not overwrite normal auto-state");
	}
	{
		const Bytes before = Read(root + "/setting.bin");
		CliOptions bad; bad.system = CliSystemMode::N; bad.clock = CliClockMode::Clock8MHzH;
		bad.disks.emplace_back(root + "/missing.d88", 0, 0);
		App app;
		Require(!app.Init(bad), "invalid startup disk reports initialization failure");
		Clean(app);
		Require(Read(root + "/setting.bin") == before, "failed startup restores CLI overrides before saving");
		Require(Read(root + "/state0.bin") == automatic, "failed startup preserves automatic state");
	}
	{
		const auto missing = root + "/no-roms";
		Require(Xm8Ra::EnsureRaDirectoryTree(missing), "isolated missing-ROM case");
		LifecycleHost::Configure(missing);
		App app;
		Require(!app.Init({}), "missing required host data fails startup");
		Clean(app);
	}
	{
		for (bool too_long : {false, true}) {
			LifecycleHost::Configure(too_long ? std::string(4096, 'x') : root, !too_long);
			App app;
			Require(!app.Init({}), "unavailable or oversized preferences path rejects startup");
			Clean(app);
		}
	}
	LifecycleHost::Configure(root);
	SDL_Quit();
	Require(Xm8Ra::RemoveRaTree(root), "remove generated synthetic fixtures");
	std::cout << "App startup/shutdown, auto-state, persisted settings and CLI isolation passed\n";
}

// Real media managers, App save/load, FILEIO and VM; RA mode OFF.
// The host resolves every ROM/state path into a unique disposable directory.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#define SDL_MAIN_HANDLED
#include "os.h"
#include "common.h"
#include "app.h"
#include "emu_sdl.h"
#include "emu.h"
#include "vm.h"
#include "event.h"
#include "pc88.h"
#include "upd1990a.h"
#include "diskmgr.h"
#include "tapemgr.h"
#include "setting.h"
#include "audio.h"
#include "video.h"
#include "font.h"
#include "input.h"
#include "fileio.h"
#include "Fixtures/d88_fixture.h"
#include "media_manager_fileio.h"
#include "ra_file_util.h"
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace {
std::string host_root;
bool expected_io_failure = false;
using Bytes = std::vector<unsigned char>;
void Require(bool ok, const std::string& message)
{
	if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
SDL_assert_state SDLCALL Assertion(const SDL_assert_data *data, void*)
{
	// Debug builds must not open SDL's assertion dialog for injected I/O errors.
	Require(expected_io_failure, std::string("unexpected SDL assertion: ") + data->condition);
	return SDL_ASSERTION_IGNORE;
}
struct ExpectedIOFailure {
	ExpectedIOFailure() { expected_io_failure = true; }
	~ExpectedIOFailure() { expected_io_failure = false; }
};
Bytes Read(const std::string& path)
{
	std::ifstream file(path, std::ios::binary);
	Require(file.good(), "read generated file " + path);
	return Bytes(std::istreambuf_iterator<char>(file), {});
}
void Write(const std::string& path, const Bytes& bytes)
{
	std::ofstream file(path, std::ios::binary);
	file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	Require(file.good(), "write generated fixture " + path);
}
}

EMU_SDL::EMU_SDL(Video *v) : video(v)
{
	std::snprintf(base_path, sizeof(base_path), "%s/", host_root.c_str());
}
EMU_SDL::~EMU_SDL() = default;
_TCHAR* EMU_SDL::get_app_path() { return base_path; }
_TCHAR* EMU_SDL::get_bios_path(_TCHAR *name)
{
	std::snprintf(bios_path, sizeof(bios_path), "%s%s", base_path, name);
	return bios_path;
}
void EMU_SDL::get_host_time(cur_time_t *time) { std::memset(time, 0, sizeof(*time)); }
void EMU_SDL::printer_out(uint8) {}
void EMU_SDL::printer_strobe(bool) {}
void EMU_SDL::mute_sound() {}
scrntype* EMU_SDL::get_screen_buf(int) { return nullptr; }
void EMU_SDL::current_thread_sleep(uint32) {}

// Wire dependencies without App::Init, SDL_GetPrefPath, user ROMs or a window.
// Persistence and VM state operations themselves are never mocked.
class AppStateTestAccess {
public:
	App app;
	explicit AppStateTestAccess(const std::string& root)
	{
		host_root = root;
		app.setting = new Setting;
		// Mirror the system/audio defaults normally set by Setting::Init, while
		// deliberately avoiding its lookup of the user's preferences directory.
		app.setting->SetSystemMode(SETTING_V2_MODE);
		app.setting->SetCPUClock(4);
		app.setting->SetDip(DIP_LINE_25);
		app.setting->SetAudioFreq(48000);
		app.setting->SetAudioBuffer(120);
		app.setting->SetOPNA(false);
		Require(root.size() + 32 < sizeof(app.setting->setting_dir), "fixture path fits");
		std::snprintf(app.setting->setting_dir, sizeof(app.setting->setting_dir), "%s/", root.c_str());
		std::snprintf(app.setting->setting_path, sizeof(app.setting->setting_path), "%s/setting.bin", root.c_str());
		app.audio = new Audio;
		Require(app.audio->Init(), "dummy audio initialization");
		app.audio_param = static_cast<Uint8*>(SDL_calloc(1, sizeof(Audio::OpenParam)));
		Require(app.audio_param != nullptr, "audio parameter storage");
		app.video = new Video(&app);
		app.video->softkey_buf = static_cast<Uint32*>(SDL_calloc(SCREEN_WIDTH * SCREEN_HEIGHT, sizeof(Uint32)));
		Require(app.video->softkey_buf != nullptr, "headless softkey framebuffer");
		app.font = new Font(&app); // Constructor provides in-memory glyph tables.
		app.input = new Input(&app);
		app.wrapper = new EMU_SDL(app.video);
		app.emu = new EMU(app.wrapper);
		app.vm = new VM(app.emu);
		app.vm->initialize_sound(app.setting->GetAudioFreq(), 1024);
		app.vm->reset();
		app.evmgr = static_cast<EVENT*>(app.vm->get_device(1));
		app.pc88 = static_cast<PC88*>(app.vm->get_device(2));
		app.upd1990a = static_cast<UPD1990A*>(app.vm->get_device(6));
		for (int drive = 0; drive < 2; ++drive) {
			app.diskmgr[drive] = new DiskManager;
			Require(app.diskmgr[drive]->Init(app.vm, drive), "disk manager initialization");
		}
		app.tapemgr = new TapeManager;
		Require(app.tapemgr->Init(app.vm), "tape manager initialization");
		Require(app.input->Init(), "headless input initialization");
		app.vm_sem = SDL_CreateSemaphore(1);
		Require(app.vm_sem != nullptr, "VM semaphore initialization");
		Require(!app.ra_mode_enabled && app.ra_service == nullptr, "runtime RA mode OFF");
	}
	TapeManager& Tape() { return *app.tapemgr; }
	DiskManager& Disk() { return *app.diskmgr[0]; }
	bool TapeInserted() { return app.vm->tape_inserted(); }
	bool DiskInserted() { return app.vm->disk_inserted(0); }
	void Record(const Bytes& bytes) {
		app.pc88->write_io8(0x30, 0x08);
		for (auto value : bytes) app.pc88->write_signal(SIG_PC88_USART_OUT, value, 0xff);
	}
};

int main(int, char**)
{
	SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
	Require(SDL_Init(SDL_INIT_TIMER | SDL_INIT_AUDIO) == 0, "SDL initialization");
	SDL_SetAssertionHandler(Assertion, nullptr);
	const char *temporary = std::getenv(
#ifdef _WIN32
		"TEMP"
#else
		"TMPDIR"
#endif
	);
	const std::string root = std::string(temporary ? temporary : ".") + "/xm8-media-" +
		std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
	std::string error;
	Require(D88Fixture::GenerateStandardSet(root, &error), error);
	const auto tape = root + "/sample.cmt", recording = root + "/record.cmt";
	const auto disk = root + "/multi.d88", missing = root + "/missing.cmt";
	Write(tape, Bytes(256, 0x55));
	const auto original_tape = Read(tape), original_disk = Read(disk);
	{
		AppStateTestAccess h(root);
		auto& t = h.Tape();
		Require(!t.IsPlay() && !t.IsRec() && !h.TapeInserted(), "initial tape empty");
		Require(t.Play(tape.c_str()) && t.IsPlay() && !t.IsRec() && h.TapeInserted(), "real tape playback");
		Require(std::string(t.GetFileName()) == "sample.cmt" && std::string(t.GetDir()) == root + "/", "tape path reporting");
		Require(!t.Play(missing.c_str()) && t.IsPlay() && h.TapeInserted(), "missing playback preserves current tape");
		Require(!t.Rec((root + "/absent/record.cmt").c_str()) && t.IsPlay(), "record open failure preserves playback");
		Require(h.app.Save(1), "save playback state");
		t.Eject(); t.Eject();
		Require(!t.IsPlay() && !t.IsRec() && !h.TapeInserted() && !*t.GetFileName(), "idempotent eject");
		Require(h.app.Load(1) && t.IsPlay() && h.TapeInserted(), "restore playback through App");
		Require(t.Rec(recording.c_str()) && t.IsRec() && !t.IsPlay() && h.TapeInserted(), "playback to recording");
		const Bytes payload{0x12, 0x34, 0xab, 0xcd};
		h.Record(payload);
		Require(h.app.Save(2), "save buffered recording");
		t.Eject(); Require(Read(recording) == payload, "eject flushes actual recorded bytes");
		Require(h.app.Load(2) && t.IsRec() && h.TapeInserted(), "restore recording and buffered bytes");
		h.Record(Bytes{0xef}); t.Eject();
		Require(Read(recording) == Bytes({0x12, 0x34, 0xab, 0xcd, 0xef}), "restored recording continues in order");

		MediaTestIO::Inject(recording, MediaTestIO::Fault::OpenAfterProbe);
		Require(!t.Rec(recording.c_str()), "second recording open failure reported");
		Require(MediaTestIO::Triggered() && !t.IsRec() && !h.TapeInserted(), "failed VM open never reports recording");
		MediaTestIO::Clear();
		MediaTestIO::Inject(recording, MediaTestIO::Fault::Close);
		Require(!t.Rec(recording.c_str()) && !t.IsRec(), "preflight close failure reported");
		Require(MediaTestIO::Triggered(), "preflight close failure reached"); MediaTestIO::Clear();

		MediaTestIO::Inject(recording, MediaTestIO::Fault::ShortWrite);
		Require(t.Rec(recording.c_str()), "record before injected short write");
		h.Record(payload);
		{ ExpectedIOFailure expected; t.Eject(); }
		Require(MediaTestIO::Triggered() && Read(recording) == Bytes({0x12, 0x34}), "actual recording flush short write reached");
		Require(!t.IsRec() && !h.TapeInserted(), "short write still releases tape");
		MediaTestIO::Clear();
		Require(t.Rec(recording.c_str()), "recording can retry after I/O failure");
		h.Record(payload); t.Eject(); Require(Read(recording) == payload, "retry records complete bytes");

		Require(h.app.Save(3), "save ejected tape state");
		Require(t.Play(tape.c_str()) && h.app.Load(3) && !t.IsPlay() && !t.IsRec() && !h.TapeInserted(), "restore ejected tape state");
		MediaTestIO::Inject(disk, MediaTestIO::Fault::OpenAfterAnalyze);
		Require(!h.Disk().Open(disk.c_str(), 0), "FDC open failure reported after successful analysis");
		Require(MediaTestIO::Triggered() && !h.Disk().IsOpen() && !h.DiskInserted(), "failed FDC open leaves drive empty");
		MediaTestIO::Clear();
		MediaTestIO::Inject(disk, MediaTestIO::Fault::ReadOnly);
		Require(h.Disk().Open(disk.c_str(), 1) && h.Disk().IsOpen() && h.DiskInserted(), "read-only disk opens through fallback");
		Require(h.Disk().IsProtect(), "UI exposes read-only disk protection");
		h.Disk().Close(); MediaTestIO::Clear();
		Require(!h.Disk().Open((root + "/absent.d88").c_str(), 0) && !h.Disk().IsOpen() && !h.DiskInserted(), "missing disk rejected");
		Require(Read(tape) == original_tape && Read(disk) == original_disk, "source tape and disk preserved");
	}
	MediaTestIO::Clear(); SDL_SetAssertionHandler(nullptr, nullptr); SDL_Quit();
	Require(Xm8Ra::RemoveRaTree(root), "remove only generated fixtures");
	std::cout << "Tape playback/recording/state/eject, I/O recovery and read-only disk passed\n";
}

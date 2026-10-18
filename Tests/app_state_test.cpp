// Real App::Save/Load, Setting, media managers, FILEIO and VM; RA mode OFF.
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
#include "app_state_fileio.h"
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
	Setting& Settings() { return *app.setting; }
	std::string Path(int slot) const { return host_root + "/state" + std::to_string(slot) + ".bin"; }
	void Unlocked() { Require(SDL_SemValue(app.vm_sem) == 1, "VM lock balanced on success/failure"); }
	void SetMemory(unsigned char value)
	{
		int wait = 0;
		app.pc88->write_data8w(0xc123, value, &wait);
		Require(Memory() == value, "RAM fixture reached real PC88 memory");
	}
	unsigned Memory() { int wait = 0; return app.pc88->read_data8w(0xc123, &wait); }
	void Mount(const std::string& first, const std::string& second, const std::string& tape)
	{
		Require(app.diskmgr[0]->Open(first.c_str(), 1), "mount Drive 1 bank 1");
		Require(app.diskmgr[1]->Open(second.c_str(), 0), "mount Drive 2 bank 0");
		Require(app.tapemgr->Play(tape.c_str()), "mount generated tape for playback");
	}
	void Eject()
	{
		app.diskmgr[0]->Close(); app.diskmgr[1]->Close(); app.tapemgr->Eject();
	}
	void ExpectMounted(const std::string& first, const std::string& second, const std::string& tape)
	{
		for (int drive = 0; drive < 2; ++drive) {
			auto *disk = app.diskmgr[drive];
			Require(disk->IsOpen() && app.vm->disk_inserted(drive), "restored disk in manager and real FDC");
			Require(disk->GetPath() == (drive == 0 ? first : second), "restored disk path");
			Require(disk->GetBank() == (drive == 0 ? 1 : 0), "restored disk bank");
		}
		Require(app.tapemgr->IsPlay() && !app.tapemgr->IsRec(), "restored tape playback mode");
		Require(std::string(app.tapemgr->GetFileName()) == tape.substr(tape.find_last_of("/\\") + 1), "restored tape filename");
	}
	void ExpectEmpty()
	{
		for (int drive = 0; drive < 2; ++drive)
			Require(!app.diskmgr[drive]->IsOpen() && !app.vm->disk_inserted(drive), "restored empty drive");
		Require(!app.tapemgr->IsPlay() && !app.tapemgr->IsRec(), "restored ejected tape");
	}
	void ExpectMapping()
	{
		app.input->OnKeyDown(false, SDL_SCANCODE_LEFT);
		app.input->OnKeyDown(false, SDL_SCANCODE_1);
		Require(app.emu->key_buffer()[0x64] && app.emu->key_buffer()[0x61], "restored settings applied to actual input mapping");
		app.input->OnKeyUp(false, SDL_SCANCODE_LEFT);
		app.input->OnKeyUp(false, SDL_SCANCODE_1);
	}
	size_t MetadataSize(const std::string& path)
	{
		FILEIO file;
		Require(file.Fopen(const_cast<char*>(path.c_str()), FILEIO_WRITE_BINARY), "metadata probe output");
		app.setting->SaveSetting(&file);
		for (auto *disk : app.diskmgr) disk->Save(&file);
		app.tapemgr->Save(&file);
		const size_t result = file.Ftell();
		file.Fclose();
		Require(!file.HasError(), "metadata probe completion");
		return result;
	}
};

int main(int, char**)
{
	SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
	Require(SDL_Init(SDL_INIT_TIMER | SDL_INIT_AUDIO) == 0, "SDL timer and dummy audio");
	SDL_SetAssertionHandler(Assertion, nullptr);
	const char *temporary = std::getenv(
#ifdef _WIN32
		"TEMP"
#else
		"TMPDIR"
#endif
	);
	const std::string root = std::string(temporary ? temporary : ".") +
		"/xm8-app-state-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
	std::string error;
	Require(D88Fixture::GenerateStandardSet(root, &error), error);
	const auto first = root + "/multi.d88", second = root + "/second.d88", tape = root + "/sample.cmt";
	Write(tape, Bytes(256, 0x55));
	{
		AppStateTestAccess h(root);
		h.Settings().SetWindowWidth(1280);
		h.Settings().SetCursorToNumPad(true); h.Settings().SetNumToNumPad(true);
		h.Mount(first, second, tape); h.SetMemory(0x5a);
		const auto media_before = Read(first), tape_before = Read(tape);
		Require(h.app.Save(1), "public Save of complete normal state"); h.Unlocked();
		const Bytes valid = Read(h.Path(1));
		const size_t vm_offset = h.MetadataSize(root + "/metadata.bin");
		Require(valid.size() > vm_offset + 65536, "state includes VM payload beyond UI metadata");
		h.Settings().SetWindowWidth(640);
		h.Settings().SetCursorToNumPad(false); h.Settings().SetNumToNumPad(false);
		h.Eject(); h.SetMemory(0xa5);
		Require(h.app.Load(1), "public Load of complete normal state"); h.Unlocked();
		Require(h.Settings().GetWindowWidth() == 1280, "restore UI settings");
		Require(h.Memory() == 0x5a, "restore real VM RAM");
		h.ExpectMounted(first, second, tape); h.ExpectMapping();
		Require(Read(first) == media_before && Read(tape) == tape_before, "save/load does not rewrite source media");

		// Slots are independent, including automatic slot 0 and an empty mount set.
		h.Eject(); h.SetMemory(0x33);
		Require(h.app.Save(0), "save automatic slot through normal public entry");
		Require(h.app.Load(1), "reload populated slot"); h.ExpectMounted(first, second, tape);
		Require(h.app.Load(0), "load automatic slot"); h.ExpectEmpty();
		Require(h.Memory() == 0x33 && Read(h.Path(1)) == valid, "slots retain independent state"); h.Unlocked();

		// Open failures must not damage the already saved slot or running machine.
		Require(!h.app.Load(9), "missing state returns failure"); h.Unlocked();
		Require(h.Memory() == 0x33, "missing state preserves VM");
		Require(Xm8Ra::EnsureRaDirectoryTree(h.Path(2)), "block target with directory");
		Require(!h.app.Save(2), "unwritable state target returns failure"); h.Unlocked();
		Require(h.Memory() == 0x33 && Read(h.Path(1)) == valid, "save open failure preserves VM and other slot");
		Require(Xm8Ra::RemoveRaTree(h.Path(2)), "remove blocked target");

		for (auto fault : {StateTestIO::Fault::ShortWrite, StateTestIO::Fault::Close,
			StateTestIO::Fault::ShortWrite}) {
			StateTestIO::Inject(h.Path(2), fault);
			{
				ExpectedIOFailure expected;
				Require(!h.app.Save(2), "public Save reports write/close failure");
			}
			Require(StateTestIO::Triggered() && StateTestIO::Closes() == 1, "fault reached and stream closed exactly once");
			StateTestIO::Clear(); h.Unlocked();
			Require(h.Memory() == 0x33 && Read(h.Path(1)) == valid, "failed save preserves VM and known-good slot");
			Require(h.app.Save(2), "save retries successfully after I/O failure"); h.Unlocked();
			h.SetMemory(0x99);
			Require(h.app.Load(2), "retry state can be loaded");
			Require(h.Memory() == 0x33, "retry saved the full VM state"); h.Unlocked();
		}

		// Reject early corruption before replacing the VM or settings.
		for (size_t size : {size_t(0), size_t(3), size_t(158)}) {
			Write(h.Path(3), Bytes(valid.begin(), valid.begin() + size));
			ExpectedIOFailure expected;
			Require(!h.app.Load(3), "truncated settings prefix rejected by public Load"); h.Unlocked();
			Require(h.Memory() == 0x33, "early rejection preserves running VM");
		}
		Bytes corrupt = valid;
		std::memset(corrupt.data(), 0xff, sizeof(Uint32));
		Write(h.Path(3), corrupt);
		Require(!h.app.Load(3), "unknown settings version rejected"); h.Unlocked();
		Require(h.Memory() == 0x33, "unknown settings version preserves VM");

		// Late failures can partially restore state in the legacy loader. Require
		// failure reporting and recovery, without asserting transactional rollback.
		for (bool version_error : {true, false}) {
			corrupt = valid;
			if (version_error) std::memset(corrupt.data() + vm_offset, 0xff, sizeof(Uint32));
			else corrupt.pop_back();
			Write(h.Path(3), corrupt);
			{
				ExpectedIOFailure expected;
				Require(!h.app.Load(3), "unknown VM version or truncated VM tail rejected");
			}
			h.Unlocked();
			Require(h.app.Load(1), "valid state reloads after late failure"); h.Unlocked();
			Require(h.Memory() == 0x5a, "recovery restores real VM RAM");
			h.ExpectMounted(first, second, tape); h.ExpectMapping();
		}
	}
	SDL_SetAssertionHandler(nullptr, nullptr);
	SDL_Quit();
	Require(Xm8Ra::RemoveRaTree(root), "remove generated fixture root"); // Only this test's unique root.
	std::cout << "Normal App save/load, slot isolation, media/RAM restoration and I/O failure recovery passed\n";
}

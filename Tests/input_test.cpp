// Real Input/SoftKey/Setting logic; only host rendering and VM boundaries are fakes.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
#define SDL_MAIN_HANDLED
#include "os.h"
#include "common.h"
#include "emu.h"
#include "app.h"
#include "setting.h"
#include "platform.h"
#include "video.h"
#include "font.h"
#include "input.h"

config_t config;
namespace {
Setting *host_setting;
Input *host_input;
EMU *host_emu;
Video *host_video;
Font *host_font;
std::vector<Uint32> pixels(640 * 400);
std::vector<SDL_Scancode> notifications;
int menu_count;
void Require(bool ok, const char *message)
{
	if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
}

App::App() {}
App::~App() {}
Screenshot::Writer::Writer(const std::string&) : impl(nullptr) {}
Screenshot::Writer::~Writer() {}
Setting* App::GetSetting() { return host_setting; }
Input* App::GetInput() { return host_input; }
EMU* App::GetEmu() { return host_emu; }
Video* App::GetVideo() { return host_video; }
Font* App::GetFont() { return host_font; }
Platform* App::GetPlatform() { return nullptr; }
void App::OnKeyVM(SDL_Scancode code) { notifications.push_back(code); }
void App::GetKeyVM(Uint8 *buf) { std::memcpy(buf, host_emu->key_buffer(), 256); }
Uint32 App::GetKeyCode(Uint32 port, Uint32 bit) { return port == 1 && bit == 0 ? 0x41 : 0x42; }
void App::EnterMenu(int) { ++menu_count; }
EMU::EMU(EMU_SDL *wrapper) : emu_sdl(wrapper)
{
	std::memset(key_status, 0, sizeof(key_status));
	std::memset(joy_status, 0, sizeof(joy_status));
}
void EMU::set_key_buffer(uint8 *status) { std::memcpy(key_status, status, sizeof(key_status)); }
void EMU::set_joy_buffer(uint32 *status) { std::memcpy(joy_status, status, sizeof(joy_status)); }
Video::Video(App*) {}
Video::~Video() {}
Uint32* Video::GetSoftKeyFrame() { return pixels.data(); }
void Video::SetSoftKey(bool, bool) {}
void Video::UpdateSoftKey() {}
bool Video::ConvertSoftKeyPoint(int*, int*) { return true; }
bool Video::ConvertSoftKeyFinger(float, float, int*, int*) { return false; }
Font::Font(App*) {}
Font::~Font() {}
void Font::DrawSoftKeyRect(Uint32*, SDL_Rect*, Uint32, Uint32) {}
void Font::DrawSoftKey1x(Uint32*, SDL_Rect*, const char*, Uint32) {}
void Font::DrawSoftKey4x(Uint32*, SDL_Rect*, const char*, Uint32) {}
bool Platform::CheckMouseButton() { return false; }

// Seed only elapsed time/pending state, avoiding real sleeps and clock races.
class InputTestAccess {
public:
	static void Expire(Input& input, SDL_Scancode code)
	{
		input.key_soft_make_tick[code] = SDL_GetTicks() - 33U;
	}
	static void QueueBreak(Input& input, SDL_Scancode code)
	{
		Expire(input, code);
		input.key_soft_break_flag[code] = true;
	}
	static bool Pending(Input& input, SDL_Scancode code) { return input.key_soft_break_flag[code]; }
	static SoftKey* FirstSoftKey(Input& input) { return input.key_list; }
	static void JoyDown(Input& input, int button) { input.OnJoyKeyDown(button); }
	static void JoyUp(Input& input, int button) { input.OnJoyKeyUp(button); }
};

namespace {
struct Harness {
	Setting setting;
	App app;
	EMU emu{nullptr};
	Video video{&app};
	Font font{&app};
	Input input{&app};
	explicit Harness(bool cursor = false, bool number = false)
	{
		host_setting = &setting; host_input = &input; host_emu = &emu;
		host_video = &video; host_font = &font;
		notifications.clear(); menu_count = 0;
		setting.SetCursorToNumPad(cursor);
		setting.SetNumToNumPad(number);
		Require(input.Init(), "headless input initialization");
	}
	bool Down(unsigned code) { return emu.key_buffer()[code] != 0; }
	void Empty()
	{
		for (unsigned code = 0; code < 256; ++code) Require(!Down(code), "all VM keys released");
	}
};
}

int main()
{
	Require(SDL_Init(SDL_INIT_TIMER) == 0, "timer initialization (no window or joystick)");
	{
		Harness h;
		h.Empty();
		h.input.OnKeyDown(false, SDL_SCANCODE_A);
		h.input.OnKeyDown(false, SDL_SCANCODE_B);
		Require(h.Down(0x41) && h.Down(0x42), "simultaneous key presses reach VM");
		h.input.OnKeyDown(false, SDL_SCANCODE_A);
		h.input.OnKeyUp(false, SDL_SCANCODE_A);
		Require(!h.Down(0x41) && h.Down(0x42), "repeat needs one release; unrelated key stays down");
		h.input.OnKeyUp(false, SDL_SCANCODE_B);
		h.input.OnKeyUp(false, SDL_SCANCODE_B);
		h.Empty();
		h.input.OnKeyDown(false, SDL_SCANCODE_F11);
		h.input.OnKeyUp(false, SDL_SCANCODE_F11);
		h.Empty();
	}
	{
		Harness h;
		h.input.OnKeyDown(false, SDL_SCANCODE_LSHIFT);
		h.input.OnKeyDown(false, SDL_SCANCODE_RSHIFT);
		h.input.OnKeyUp(false, SDL_SCANCODE_LSHIFT);
		Require(!h.Down(0xa0) && h.Down(0xa1), "releasing left Shift must preserve held right Shift");
		h.input.OnKeyUp(false, SDL_SCANCODE_RSHIFT);
		h.Empty();
	}
	for (bool cursor : {false, true}) for (bool number : {false, true}) {
		Harness h(cursor, number);
		const SDL_Scancode directions[] = {SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT};
		const unsigned normal[] = {0x26, 0x28, 0x25, 0x27};
		const unsigned numeric[] = {0x68, 0x62, 0x64, 0x66};
		for (unsigned i = 0; i < 4; ++i) {
			h.input.OnKeyDown(false, directions[i]);
			Require(h.Down(cursor ? numeric[i] : normal[i]), "persisted cursor mapping applied by Init");
			h.input.OnKeyUp(false, directions[i]); h.Empty();
		}
		for (unsigned i = 0; i < 10; ++i) {
			const auto code = static_cast<SDL_Scancode>(i == 0 ? SDL_SCANCODE_0 : SDL_SCANCODE_1 + i - 1);
			h.input.OnKeyDown(false, code);
			Require(h.Down((number ? 0x60 : 0x30) + i), "all ten number mappings applied by Init");
			h.input.OnKeyUp(false, code); h.Empty();
		}
	}
	{
		Harness h(true, true);
		h.input.OnKeyDown(false, SDL_SCANCODE_LEFT);
		h.input.OnKeyDown(false, SDL_SCANCODE_KP_4);
		h.input.OnKeyUp(false, SDL_SCANCODE_LEFT);
		Require(h.Down(0x64), "remapped cursor and keypad have independent ownership");
		h.input.OnKeyUp(false, SDL_SCANCODE_KP_4); h.Empty();
		h.input.OnKeyDown(false, SDL_SCANCODE_1);
		h.input.OnKeyDown(false, SDL_SCANCODE_B);
		h.input.ChangeNumToNumPad(false);
		Require(!h.Down(0x61) && h.Down(0x31) && h.Down(0x42), "held number retargets without clearing unrelated keys");
		h.input.OnKeyUp(false, SDL_SCANCODE_1);
		Require(h.Down(0x42), "unrelated key survives remapped number release");
		h.input.OnKeyUp(false, SDL_SCANCODE_B); h.Empty();
		h.input.OnKeyDown(false, SDL_SCANCODE_UP);
		h.input.ChangeCursorToNumPad(false);
		Require(!h.Down(0x68) && h.Down(0x26), "held cursor retargets without an orphaned key");
		h.input.OnKeyUp(false, SDL_SCANCODE_UP); h.Empty();
	}
	{
		Harness h;
		h.input.OnKeyDown(false, SDL_SCANCODE_A);
		h.input.OnKeyDown(false, SDL_SCANCODE_LCTRL);
		h.input.ProcessList();
		Require(h.input.GetKeyStatus(SDL_SCANCODE_A), "VM feedback exposes held key");
		h.emu.joy_buffer()[0] = 0x3f;
		h.input.LostFocus(); h.Empty();
		Require(!h.input.GetKeyStatus(SDL_SCANCODE_A), "focus loss clears cached VM feedback too");
		Require(h.emu.joy_buffer()[0] == 0 && h.emu.joy_buffer()[1] == 0, "focus loss clears joystick output");
		h.input.LostFocus();
		h.input.OnKeyUp(false, SDL_SCANCODE_A); h.Empty();
		h.input.OnKeyDown(false, SDL_SCANCODE_A);
		Require(h.Down(0x41), "input works again after focus loss");
		h.input.OnKeyUp(false, SDL_SCANCODE_A); h.Empty();
	}
	{
		Harness h;
		h.input.OnKeyDown(true, SDL_SCANCODE_A);
		h.input.OnKeyDown(false, SDL_SCANCODE_A);
		InputTestAccess::QueueBreak(h.input, SDL_SCANCODE_A);
		h.input.DelayedBreak();
		Require(h.Down(0x41), "delayed soft release preserves a physical press");
		h.input.OnKeyUp(false, SDL_SCANCODE_A); h.Empty();
		h.input.OnKeyDown(true, SDL_SCANCODE_RSHIFT);
		InputTestAccess::Expire(h.input, SDL_SCANCODE_RSHIFT);
		h.input.OnKeyUp(true, SDL_SCANCODE_RSHIFT);
		Require(h.Down(0xa1), "soft right Shift stays latched after release");
		h.input.OnKeyDown(false, SDL_SCANCODE_RSHIFT);
		h.input.OnKeyUp(false, SDL_SCANCODE_RSHIFT);
		Require(h.Down(0xa1), "physical right Shift release preserves soft latch");
		h.input.OnKeyDown(true, SDL_SCANCODE_RSHIFT); h.Empty();
		h.input.OnKeyDown(true, SDL_SCANCODE_A);
		InputTestAccess::QueueBreak(h.input, SDL_SCANCODE_A);
		h.input.OnKeyDown(true, SDL_SCANCODE_A);
		Require(!InputTestAccess::Pending(h.input, SDL_SCANCODE_A), "new soft press cancels an older pending release");
		InputTestAccess::Expire(h.input, SDL_SCANCODE_A);
		h.input.OnKeyUp(true, SDL_SCANCODE_A); h.Empty();
		h.input.OnKeyDown(true, SDL_SCANCODE_A);
		InputTestAccess::QueueBreak(h.input, SDL_SCANCODE_A);
		h.input.LostFocus(); h.Empty();
		Require(!InputTestAccess::Pending(h.input, SDL_SCANCODE_A), "focus loss cancels delayed release");
		h.input.OnKeyDown(false, SDL_SCANCODE_A);
		h.input.DelayedBreak();
		Require(h.Down(0x41), "old delayed release cannot cancel a new press");
		h.input.OnKeyUp(false, SDL_SCANCODE_A); h.Empty();
	}
	{
		Harness h;
		h.setting.SetJoystickToKey(0, 0x0100);
		h.setting.SetJoystickToKey(1, 0x0100);
		InputTestAccess::JoyDown(h.input, 0);
		InputTestAccess::JoyDown(h.input, 1);
		InputTestAccess::JoyUp(h.input, 0);
		Require(h.Down(0x41), "joystick aliases retain the other held button");
		h.input.OnKeyDown(false, SDL_SCANCODE_A);
		InputTestAccess::JoyUp(h.input, 1);
		Require(h.Down(0x41), "joystick release preserves physical key");
		h.input.OnKeyUp(false, SDL_SCANCODE_A); h.Empty();
		InputTestAccess::JoyDown(h.input, 0);
		h.setting.SetJoystickToKey(0, 0x0200);
		InputTestAccess::JoyUp(h.input, 0); h.Empty();
		InputTestAccess::JoyDown(h.input, 0);
		Require(h.Down(0x42), "new joystick mapping applies on next press");
		h.input.LostFocus(); h.Empty();
		h.input.OnKeyDown(false, SDL_SCANCODE_CAPSLOCK);
		Require(notifications.back() == SDL_SCANCODE_CAPSLOCK, "caps notification reaches VM boundary");
		h.input.OnKeyUp(false, SDL_SCANCODE_CAPSLOCK); h.Empty();
	}
	{
		Harness h;
		h.input.DelList();
		const softkey_param layout[] = {{0, 0, 2, 2, 1, "A", SDL_SCANCODE_A}, {0, 0, 0, 0, 1, "", -1}};
		h.input.AddList(layout);
		// Drive a real SoftKey directly; no window coordinates or physical mouse.
		SoftKey *key = InputTestAccess::FirstSoftKey(h.input);
		Require(key != nullptr, "softkey initialization");
		key->OnInput(1, 1, true, 7);
		Require(h.Down(0x41) && key->IsPress(), "softkey press reaches input");
		h.input.LostFocus();
		Require(!key->IsPress(), "softkey leaves pressed state on focus loss");
		h.Empty();
	}
	{
		Harness h;
		for (int invalid : {0x120, SDL_NUM_SCANCODES - 1}) {
			const auto code = static_cast<SDL_Scancode>(invalid);
			h.input.OnKeyDown(false, code); h.input.OnKeyUp(false, code);
			Require(!h.input.GetKeyStatus(code), "unsupported scancode ignored");
		}
		h.Empty();
	}
	SDL_Quit();
	std::cout << "Keyboard transitions, focus loss, aliases, remapping and invalid scancodes passed\n";
}

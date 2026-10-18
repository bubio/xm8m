// Real Video, Menu, MenuList and MenuItem; headless App/VM host with no user data.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <iostream>
#include <limits>
#include <string>
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
#include "converter.h"
#include "menu.h"
#include "menulist.h"
#include "menuitem.h"
#include "menuid.h"
#include "ra_file_util.h"
namespace {
std::string host_root;
Uint32 menu_time = 1000;
void Require(bool ok, const std::string& message)
{
	if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
Uint32 SDLCALL MenuTicks() { return menu_time; }
Uint64 SDLCALL MenuCounter() { return menu_time; }
Uint64 SDLCALL MenuFrequency() { return 1000; }
}
// Only the menu's clock is controlled. All event handling remains production code.
#define SDL_GetTicks MenuTicks
#define SDL_GetPerformanceCounter MenuCounter
#define SDL_GetPerformanceFrequency MenuFrequency
#include "../Source/UI/menulist.cpp"
#undef SDL_GetTicks
#undef SDL_GetPerformanceCounter
#undef SDL_GetPerformanceFrequency

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


class UiTestAccess {
public:
	App app;
	explicit UiTestAccess(const std::string& root)
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
		app.video->setting = app.setting;
		app.video->menu_buf = static_cast<Uint32*>(SDL_calloc(SCREEN_WIDTH * SCREEN_HEIGHT, sizeof(Uint32)));
		Require(app.video->menu_buf != nullptr, "menu framebuffer");
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
		app.converter = new Converter;
		Require(app.converter->Init(), "headless converter initialization");
		app.menu = new Menu(&app);
		Require(app.menu->Init(), "headless menu initialization");
		app.vm_sem = SDL_CreateSemaphore(1);
		Require(app.vm_sem != nullptr, "VM semaphore initialization");
		Require(!app.ra_mode_enabled && app.ra_service == nullptr, "runtime RA mode OFF");
	}
	Setting& Settings() { return *app.setting; }
	MenuList& List() { return *app.menu->list; }
	Video& Display() { return *app.video; }
	void Layout(int width, int height, bool status)
	{
		app.setting->SetStatusLine(status);
		app.video->SetMenuMode(false);
		app.video->SetWindowSize(width, height);
	}
	void Point(int wx, int wy, int lx, int ly)
	{
		int x = wx, y = wy;
		Require(app.video->ConvertPoint(&x, &y) && x == lx && y == ly,
			"window point maps to drawn logical point: " + std::to_string(wx) + "," + std::to_string(wy));
	}
	void Outside(int x, int y)
	{
		Require(!app.video->ConvertPoint(&x, &y) && x == 0 && y == 0, "outside/empty drawing area rejected");
	}
	void Key(SDL_Scancode code, int repeat = 0)
	{
		SDL_Event event{}; event.type = SDL_KEYDOWN;
		event.key.keysym.scancode = code; event.key.repeat = repeat;
		List().OnKeyDown(&event);
	}
	void Enter(int id)
	{
		app.EnterMenu(MENU_MAIN);
		if (id == MENU_INPUT) app.menu->EnterInput(MENU_INPUT_CURSOR_NUM);
		if (id == MENU_VIDEO) app.menu->EnterVideo(MENU_VIDEO_BRIGHTNESS);
		menu_time += 300;
	}
	void Mouse(bool down, int x, int y, Uint8 button = SDL_BUTTON_LEFT)
	{
		SDL_Event event{}; event.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
		event.button.button = button; event.button.state = down ? SDL_PRESSED : SDL_RELEASED;
		event.button.x = x; event.button.y = y;
		if (down) List().OnMouseButtonDown(&event);
		else List().OnMouseButtonUp(&event);
	}
	void FocusPoint(int *x, int *y)
	{
		*x = 320;
		*y = 100 + (List().menu_focus - List().menu_top) * MENUITEM_HEIGHT;
	}
	void Finger(bool down, float x, float y)
	{
		SDL_Event event{}; event.type = down ? SDL_FINGERDOWN : SDL_FINGERUP;
		event.tfinger.x = x; event.tfinger.y = y; event.tfinger.fingerId = 1;
		if (down) List().OnFingerDown(&event);
		else List().OnFingerUp(&event);
	}
	void PortraitPanels()
	{
		Layout(640, 800, false);
		app.video->portrait_split = true;
		app.video->draw_rect = {0, 0, 640, 400};
		app.video->softkey_rect = {0, 400, 640, 400};
		app.video->menu_rect = app.video->softkey_rect;
	}
	bool InMenu() { return app.app_menu; }
};

int main(int, char**)
{
	SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
	Require(SDL_Init(SDL_INIT_TIMER | SDL_INIT_AUDIO) == 0, "headless SDL initialization");
	const char *temporary = std::getenv(
#ifdef _WIN32
		"TEMP"
#else
		"TMPDIR"
#endif
	);
	const std::string root = std::string(temporary ? temporary : ".") + "/xm8-ui-test-" +
		std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
	Require(Xm8Ra::EnsureRaDirectoryTree(root), "create isolated host root");
	{
		UiTestAccess h(root);
		h.Layout(1280, 800, false);
		h.Point(0, 0, 0, 0); h.Point(640, 400, 320, 200); h.Point(1279, 799, 639, 399);
		h.Outside(-1, 100); h.Outside(1280, 100); h.Outside(100, -1); h.Outside(100, 800);
		h.Layout(1400, 800, false);
		h.Point(60, 0, 0, 0); h.Point(700, 400, 320, 200);
		h.Outside(59, 200); h.Outside(1340, 200);
		h.Layout(1280, 1000, false);
		h.Point(640, 500, 320, 200); h.Outside(640, 99); h.Outside(640, 900);
		h.Layout(640, 418, true);
		h.Point(320, 200, 320, 200); h.Point(639, 399, 639, 399);
		h.Point(320, 400, 320, 400); h.Point(639, 417, 639, 417);
		Require(h.Display().IsDedicatedStatusPoint(320, 400), "dedicated RA status uses separate logical rows");
		h.Display().SetMenuMode(true);
		h.Point(320, 200, 320, 200); h.Outside(320, 400);
		h.Layout(0, 0, false); h.Outside(0, 0);
		h.PortraitPanels();
		h.Point(320, 200, 320, 200);
		int panel_x = 320, panel_y = 600;
		Require(h.Display().ConvertSoftKeyPoint(&panel_x, &panel_y) && panel_x == 320 && panel_y == 200, "dedicated softkey panel conversion");
		h.Display().SetMenuMode(true);
		h.Point(320, 600, 320, 200); h.Outside(320, 399);
		Require(h.Display().ConvertFinger(0.5f, 0.75f, &panel_x, &panel_y) && panel_x == 320 && panel_y == 200, "portrait menu uses lower panel for finger");
		h.Layout(640, 400, false);
		int x = 0, y = 0;
		Require(h.Display().ConvertFinger(0.5f, 0.5f, &x, &y) && x == 320 && y == 200, "normalized finger uses actual drawing rectangle");
		Require(!h.Display().ConvertFinger(1.0f, 0.5f, &x, &y), "finger right boundary excluded");
		for (float outside : {-0.00001f, 1.0f, std::numeric_limits<float>::quiet_NaN()}) {
			Require(!h.Display().ConvertFinger(outside, 0.5f, &x, &y), "invalid normalized finger X rejected before rounding");
			Require(!h.Display().ConvertFinger(0.5f, outside, &x, &y), "invalid normalized finger Y rejected before rounding");
			Require(!h.Display().ConvertSoftKeyFinger(outside, 0.5f, &x, &y), "invalid softkey finger X rejected");
			Require(!h.Display().ConvertSoftKeyFinger(0.5f, outside, &x, &y), "invalid softkey finger Y rejected");
		}

		h.Enter(MENU_MAIN);
		h.List().SetFocus(MENU_MAIN_INPUT); h.Key(SDL_SCANCODE_RETURN, 1);
		Require(h.List().GetID() == MENU_MAIN, "key repeat does not activate menu");
		h.Key(SDL_SCANCODE_RETURN);
		Require(h.List().GetID() == MENU_INPUT, "keyboard enters submenu");
		h.List().SetFocus(MENU_INPUT_CURSOR_NUM);
		bool enabled = h.Settings().IsCursorToNumPad();
		h.Key(SDL_SCANCODE_RETURN);
		Require(h.Settings().IsCursorToNumPad() != enabled, "keyboard activates actual setting handler");
		h.Key(SDL_SCANCODE_ESCAPE, 1); Require(h.List().GetID() == MENU_INPUT, "repeated Escape ignored");
		h.Key(SDL_SCANCODE_ESCAPE); Require(h.List().GetID() == MENU_MAIN, "Escape returns to parent");
		h.Key(SDL_SCANCODE_END); Require(h.List().GetTop() > 0, "End scrolls long menu");
		h.Key(SDL_SCANCODE_HOME); Require(h.List().GetTop() == 0 && h.List().GetFocusID() == MENU_MAIN_DRIVE1, "Home restores first row");
		h.Key(SDL_SCANCODE_DOWN); Require(h.List().GetFocusID() == MENU_MAIN_DRIVE2, "Down moves one row");
		h.Key(SDL_SCANCODE_UP); Require(h.List().GetFocusID() == MENU_MAIN_DRIVE1, "Up moves one row");

		h.Enter(MENU_INPUT); h.FocusPoint(&x, &y);
		enabled = h.Settings().IsCursorToNumPad(); h.Mouse(true, x, y);
		Require(h.Settings().IsCursorToNumPad() == enabled, "mouse press only focuses checkbox");
		h.Mouse(false, x, y); Require(h.Settings().IsCursorToNumPad() != enabled, "mouse release activates checkbox");
		enabled = h.Settings().IsCursorToNumPad(); h.Mouse(true, x, y); h.Mouse(false, 0, 0);
		Require(h.Settings().IsCursorToNumPad() == enabled, "mouse release outside cancels activation");
		h.Mouse(false, 0, 0, SDL_BUTTON_RIGHT); Require(h.List().GetID() == MENU_MAIN, "right click returns to parent");

		h.Enter(MENU_INPUT); h.FocusPoint(&x, &y);
		enabled = h.Settings().IsCursorToNumPad();
		h.Finger(true, x / 640.0f, y / 400.0f); h.Finger(false, x / 640.0f, y / 400.0f);
		Require(h.Settings().IsCursorToNumPad() != enabled, "finger tap activates actual checkbox");
		h.Key(SDL_SCANCODE_HOME); h.FocusPoint(&x, &y);
		h.Finger(true, x / 640.0f, y / 400.0f); h.Finger(false, 1.0f, 1.0f);
		Require(h.List().GetID() == MENU_INPUT, "finger release outside cancels first-row activation");

		h.Finger(true, 1.0f, 1.0f); h.Finger(false, 1.0f, 1.0f);
		Require(h.List().GetID() == MENU_MAIN, "tap outside both ends goes Back");

		h.Enter(MENU_VIDEO);
		int brightness = h.List().GetSlider(MENU_VIDEO_BRIGHTNESS);
		h.Key(SDL_SCANCODE_LEFT);
		Require(h.List().GetSlider(MENU_VIDEO_BRIGHTNESS) == brightness - 1 && h.Settings().GetBrightness() == brightness - 1, "slider keyboard updates settings");
		for (int i = 0; i < 300; ++i) h.Key(SDL_SCANCODE_LEFT);
		Require(h.Settings().GetBrightness() == 0x40, "slider clamps minimum");
		for (int i = 0; i < 300; ++i) h.Key(SDL_SCANCODE_RIGHT);
		Require(h.Settings().GetBrightness() == 0xff, "slider clamps maximum");
		h.Key(SDL_SCANCODE_F11); Require(!h.InMenu(), "F11 leaves menu through actual App");
	}
	SDL_Quit();
	Require(Xm8Ra::RemoveRaTree(root), "remove generated host root");
	std::cout << "UI drawing coordinates, keyboard/mouse/finger menu and slider tests passed\n";
}

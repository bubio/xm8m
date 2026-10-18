//
// eXcellent Multi-platform emulator type 8 - 'XM8'
// based on ePC-8801MA
//
// Author (ePC-8801MA) : Takeda.Toshiya
// Author (XM8) : Tanaka.Yasushi
//
// [ input driver ]
//

#ifdef SDL

#include "os.h"
#include "common.h"
#include "emu.h"
#include "app.h"
#include "setting.h"
#include "platform.h"
#include "video.h"
#include "font.h"
#include "menuid.h"
#include "softkey.h"
#include "input.h"

//
// defines
//
#define JOYSTICK_HIGH_THRES		0x4000
										// joystick high threshold
#define JOYSTICK_LOW_THRES		-0x4000
										// joystick low threshold
#define SOFTKEY_INFINITE_TIME	20000
										// softkey infinite time (ms)
#define MENU_DELAY_TIME			400
										// menu delay time (ms)
#define KEY_SOFT_MIN_TIME		32
										// key minumum made time (ms)

//
// Input()
// constructor
//
Input::Input(App *a)
{
	// save parameter
	app = a;

	// object
	setting = NULL;
	platform = NULL;
	video = NULL;
	font = NULL;
	emu = NULL;

	// softkey
	key_list = NULL;
	key_disp = false;
	key_next = false;
	key_rebuild = false;
	key_tick = 0;
	key_shift = 0x100;

	// joystick
	joystick = NULL;
	joystick_num = 0;
	joystick_prev = 0;

	// menu
	menu_delay = 0;

	// keyboard
	memset(key_hard_pressed, 0, sizeof(key_hard_pressed));
	memset(key_soft_pressed, 0, sizeof(key_soft_pressed));
	memset(joystick_key_code, 0, sizeof(joystick_key_code));
	memset(key_status, 0, sizeof(key_status));
	memset(key_buf, 0, sizeof(key_buf));

	// default key table
	memcpy(key_table, key_base, sizeof(key_table));

	// key make tick (from softkey only)
	memset(key_soft_make_tick, 0, sizeof(key_soft_make_tick));
	memset(key_soft_break_flag, 0, sizeof(key_soft_break_flag));
}

//
// ~Input()
// destructor
//
Input::~Input()
{
	Deinit();
}

//
// Init()
// initialize
//
bool Input::Init()
{
	// get object
	setting = app->GetSetting();
	platform = app->GetPlatform();
	video = app->GetVideo();
	font = app->GetFont();
	emu = app->GetEmu();

	// Apply persisted keyboard remapping independently of auto-state loading.
	// RA mode intentionally skips the normal startup state, so relying on the
	// state-load path leaves these settings visible in the menu but inactive.
	ChangeCursorToNumPad(setting->IsCursorToNumPad());
	ChangeNumToNumPad(setting->IsNumToNumPad());

	// initialize joystick
	AddJoystick();

	// initialize softkey
	ChangeList(false, false);
	key_disp = false;
	video->SetSoftKey(false, true);

	// save menu leave tick
	menu_delay = SDL_GetTicks();

	return true;
}

//
// Deinit()
// deinitialize
//
void Input::Deinit()
{
	// joystick
	DelJoystick();

	// softkey
	DelList();
}

//
// AddJoystick()
// add joystick device
//
void Input::AddJoystick(void)
{
	int loop;

	// delete current joysticks
	DelJoystick();

	// get the number of joystick
	joystick_num = SDL_NumJoysticks();

	if (joystick_num > 0) {
		// allocate joystick pointer buffer
		joystick = (SDL_Joystick**)SDL_malloc(joystick_num * sizeof(SDL_Joystick*));

		if (joystick != NULL) {
			// open loop
			for (loop=0; loop<joystick_num; loop++) {
				joystick[loop] = SDL_JoystickOpen(loop);
			}
		}
	}
}

//
// DelJoystick()
// delete all joystick device
//
void Input::DelJoystick()
{
	int loop;

	if (joystick != NULL) {
		// close current joystick devices
		for (loop=0; loop<joystick_num; loop++) {
			if (joystick[loop] != NULL) {
				// opened joystick device
				SDL_JoystickClose(joystick[loop]);
				joystick[loop] = NULL;
			}
		}

		// free joystick pointer buffer
		SDL_free(joystick);
		joystick = NULL;
	}

	// previous state
	joystick_prev = 0;
}

//
// AddList()
// add softkey list
//
void Input::AddList(const softkey_param *param)
{
	SoftKey *key;

	// loop
	while (param->action >= 0) {
		// create new key
		key = new SoftKey(app);
		if (key->Init(param) == true) {
			if (key_list == NULL) {
				key_list = key;
			}
			else {
				key_list->Add(key);
			}
		}

		// next
		param++;
	}

	// update tick
	key_tick = SDL_GetTicks();
}

//
// DelList()
// delete softkey list
//
void Input::DelList()
{
	SoftKey *key;

	// null check
	if (key_list != NULL) {
		// loop
		for (;;) {
			// get next
			key = key_list->GetNext();

			// break
			if (key == NULL) {
				break;
			}

			// delete one key
			key->Del();
			delete key;
		}

		// delete key_list
		delete key_list;
		key_list = NULL;
	}
}

//
// DrawList()
// draw softkey list
//
void Input::DrawList(bool force)
{
	Uint32 *frame_buf;
	SoftKey *list;
	bool update;

	// initialize
	update = false;
	list = key_list;

	if (force == true) {
		// all clear
		frame_buf = video->GetSoftKeyFrame();
		memset(frame_buf, 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(Uint32));
		update = true;

		// update shift state
		if (setting->GetSoftKeyType() == 0) {
			// full key only
			key_shift = GetKeyShift();
			ShiftList();
		}
	}

	// loop
	while (list != NULL) {
		if (force == true) {
			// reset
			list->Reset();
		}

		// rendering
		if (list->Draw() == true) {
			update = true;
		}

		// next
		list = list->GetNext();
	}

	// update
	if (update == true) {
		video->UpdateSoftKey();
	}
}

//
// ProcessList()
// process softkey list
//
void Input::ProcessList()
{
	SoftKey *list;
	Uint32 shift;
	Uint32 diff;
	bool press;

	// get latest key status from vm
	app->GetKeyVM(key_buf);

	// next ?
	if (key_next == true) {
		key_next = false;
		ChangeList(true, false);
	}

	// display ?
	if (key_disp == true) {
		// compare shift state
		if (setting->GetSoftKeyType() == 0) {
			// full key only
			shift = GetKeyShift();
			if (key_shift != shift) {
				key_shift = shift;
				ShiftList();
			}
		}

		// drawing
		DrawList(false);

		// extend time if pressed
		press = false;
		list = key_list;
		while (list != NULL) {
			if (list->IsPress() == true) {
				press = true;
				break;
			}
			list = list->GetNext();
		}

		if (press == true) {
			// extend time
			key_tick = SDL_GetTicks();
		}
		else {
			// infinite time ?
			if (setting->GetSoftKeyTime() >= SOFTKEY_INFINITE_TIME) {
				// extend time
				key_tick = SDL_GetTicks();
			}
			else {
				// check timeout
				diff = (Uint32)(SDL_GetTicks() - key_tick);
				if (diff > setting->GetSoftKeyTime()) {
					// display off
					key_disp = false;
					video->SetSoftKey(false);
				}
			}
		}
	}
}

//
// ShiftList()
// update softkey text with shift state
//
void Input::ShiftList()
{
	SoftKey *list;

	list = key_list;
	while (list != NULL) {
		list->Shift(key_shift);
		list = list->GetNext();
	}
}

//
// ResetList()
// reset softkey list when leaving menu
//
void Input::ResetList()
{
	if (key_disp == true) {
		// display off
		key_disp = false;
		video->SetSoftKey(false, true);

		// save tick
		menu_delay = SDL_GetTicks();
	}
}

//
// NextList()
// next softkey list
//
void Input::NextList()
{
	key_next = true;
}

//
// RebuildList()
// rebuild softkey list
//
void Input::RebuildList()
{
	key_rebuild = true;
}

//
// ChangeList()
// next or prev softkey list
//
void Input::ChangeList(bool next, bool prev)
{
	int type;
	bool result;

	if ((next == true) || (prev == true)) {
		if (next == true) {
			// next
			result = setting->NextSoftKey();
		}
		else {
			// prev
			result = setting->PrevSoftKey();
		}

		// return false if same type
		if (result == false) {
			return;
		}
	}

	// get current type
	type = setting->GetSoftKeyType();

	// delete list
	DelList();

	// add list
	switch (type) {
	case 0:
		AddList(softkey_full);
		break;

	case 1:
		AddList(softkey_curten);
		break;

	case 2:
		AddList(softkey_functen);
		break;

	case 3:
		AddList(softkey_actmove);
		break;

	case 4:
		AddList(softkey_moveact);
		break;

	case 5:
		AddList(softkey_zxmove);
		break;

	case 6:
		AddList(softkey_movezx);
		break;

	case 7:
		AddList(softkey_tencur);
		break;

	case 8:
		AddList(softkey_tenfunc);
		break;

	case 9:
		AddList(softkey_tenleft);
		break;

	case 10:
		AddList(softkey_tenright);
		break;

	default:
		AddList(softkey_none);
		break;
	}

	// display
	DrawList(true);
	video->SetSoftKey(true);
	key_disp = true;
}

//
// GetJoystick()
// get joystick status
//
void Input::GetJoystick(Uint32 *status)
{
	int swap;
	int loop;
	Sint16 axis;
	int buttons;
	int button_loop;

	// init
	status[0] = 0;
	status[1] = 0;

	// joystick enable ?
	if (setting->IsJoyEnable() == false) {
		joystick_prev = 0;
		return;
	}

	// get enable and swap xor value
	if (setting->IsJoySwap() == true) {
		swap = 1;
	}
	else {
		swap = 0;
	}

	// loop
	for (loop=0; loop<joystick_num; loop++) {
		// check attach
		if (SDL_JoystickGetAttached(joystick[loop]) != SDL_TRUE) {
			continue;
		}

		// get the number of axis
		if (SDL_JoystickNumAxes(joystick[loop]) >= 2) {
			// get x-axis
			axis = SDL_JoystickGetAxis(joystick[loop], 0);
			if (axis >= JOYSTICK_HIGH_THRES) {
				// right
				status[0] |= 0x08;
			}
			if (axis <= JOYSTICK_LOW_THRES) {
				// left
				status[0] |= 0x04;
			}

			// get y-axis
			axis = SDL_JoystickGetAxis(joystick[loop], 1);
			if (axis >= JOYSTICK_HIGH_THRES) {
				// down
				status[0] |= 0x02;
			}
			if (axis <= JOYSTICK_LOW_THRES) {
				// up
				status[0] |= 0x01;
			}
		}

		// get the number of buttons
		buttons = SDL_JoystickNumButtons(joystick[loop]);
		if (buttons > (int)(SDL_arraysize(joystick_button) / 2)) {
			buttons = SDL_arraysize(joystick_button) / 2;
		}

		// button loop
		for (button_loop=0; button_loop<buttons; button_loop++) {
			// get button state
			if (SDL_JoystickGetButton(joystick[loop], button_loop) == 0) {
				continue;
			}

			// button A and B ?
			if (button_loop < 2) {
				// button A and B suppport swap
				status[0] |= joystick_button[(button_loop ^ swap) * 2 + 0];
				status[1] |= joystick_button[(button_loop ^ swap) * 2 + 1];
			}
			else {
				// other buttons
				status[0] |= joystick_button[button_loop * 2 + 0];
				status[1] |= joystick_button[button_loop * 2 + 1];
			}
		}
	}
}

//
// LostFocus()
// lost window focus
//
void Input::LostFocus()
{
	uint32 status[2];
	SoftKey *list;

	// softkey
	list = key_list;
	while (list != NULL) {
		list->LostFocus();
		list = list->GetNext();
	}

	// keyboard
	memset(key_hard_pressed, 0, sizeof(key_hard_pressed));
	memset(key_soft_pressed, 0, sizeof(key_soft_pressed));
	memset(joystick_key_code, 0, sizeof(joystick_key_code));
	memset(key_status, 0, sizeof(key_status));
	memset(key_buf, 0, sizeof(key_buf));
	memset(key_soft_make_tick, 0, sizeof(key_soft_make_tick));
	memset(key_soft_break_flag, 0, sizeof(key_soft_break_flag));
	joystick_prev = 0;
	SetKeyStatus();

	// joystick
	memset(status, 0, sizeof(status));
	emu->set_joy_buffer(status);
}

//
// OnKeyDown()
// key down
//
void Input::OnKeyDown(bool soft, SDL_Scancode code)
{
	Uint32 data;

	if ((unsigned int)code >= SDL_arraysize(key_table)) return;

	// get 32bit data
	data = key_table[code];
	if (data == 0) {
		return;
	}

	// soft ?
	if (soft == true) {
		// version 1.70
		key_soft_break_flag[code] = false;
		key_soft_make_tick[code] = SDL_GetTicks();
		if (key_soft_make_tick[code] == 0) {
			// 0 means not made the key by soft
			key_soft_make_tick[code]++;
		}

		// RSHIFT ?
		if (code == SDL_SCANCODE_RSHIFT) {
			// lock (toggle on each make)
			key_soft_pressed[code] = !key_soft_pressed[code];
			SetKeyStatus();
			return;
		}
	}

	// 8bit only
	if (soft) key_soft_pressed[code] = true;
	else key_hard_pressed[code] = true;

	// notify to VM
	app->OnKeyVM(code);

	// set key status
	SetKeyStatus();
}

//
// OnJoyKeyDown()
// joystick button down (keyboard emulation)
//
void Input::OnJoyKeyDown(int button)
{
	Uint32 data;
	Uint32 port;
	Uint32 bit;

	// get data from setting
	data = setting->GetJoystickToKey(button);

	// check data >= 0x1000
	if (data >= 0x1000) {
		switch (data & 3) {
		// 0x1000: (Menu)
		case 0:
			app->EnterMenu(MENU_MAIN);
			break;
		// 0x1001: (Next)
		case 1:
			ChangeList(true, true);
			break;
		// 0x1002: (Prev)
		case 2:
			ChangeList(false, true);
			break;
		// others
		default:
			break;
		}
		return;
	}

	// divide data to port and bit
	bit = data & 0x07;
	port = (data >> 8);

	// get code from vm
	data = app->GetKeyCode(port, bit);

	// 8bit only
	if (data < 0x100) {
		joystick_key_code[button] = data;
	}

	// notify to VM
	if ((port == 0x0a) && (bit == 7)) {
		app->OnKeyVM(SDL_SCANCODE_CAPSLOCK);
	}
	if ((port == 0x08) && (bit == 5)) {
		app->OnKeyVM(SDL_SCANCODE_SCROLLLOCK);
	}

	// set key status
	SetKeyStatus();
}

//
// OnKeyUp()
// key up
//
void Input::OnKeyUp(bool soft, SDL_Scancode code)
{
	Uint32 data;

	if ((unsigned int)code >= SDL_arraysize(key_table)) return;

	// get 32bit data
	data = key_table[code];
	if (data == 0) {
		return;
	}

	// soft ?
	if (soft == true) {
		// version 1.70
		if (key_soft_make_tick[code] != 0) {
			// made by soft
			if ((Uint32)(SDL_GetTicks() - key_soft_make_tick[code]) < KEY_SOFT_MIN_TIME) {
				// make->break : too fast!
				key_soft_break_flag[code] = true;
				return;
			}
			else {
				key_soft_make_tick[code] = 0;
				key_soft_break_flag[code] = false;
			}
		}

		// RSHIFT ?
		if (code == SDL_SCANCODE_RSHIFT) {
			// lock (toggle on each make)
			return;
		}
	}

	// 8bit only
	if (soft) key_soft_pressed[code] = false;
	else key_hard_pressed[code] = false;

	// set key status
	SetKeyStatus();
}

//
// DelayedBreak()
// break the key after minimum time
//
void Input::DelayedBreak()
{
	int code;

	for (code=0; code<SDL_arraysize(key_soft_break_flag); code++) {
		// delayed braek required ?
		if (key_soft_break_flag[code] == true) {
			// over?
			if ((Uint32)(SDL_GetTicks() - key_soft_make_tick[code]) >= KEY_SOFT_MIN_TIME) {
				// delayed break
				OnKeyUp(true, (SDL_Scancode)code);
			}
		}
	}
}

//
// OnJoyKeyUp()
// joystick button up (keyboard emulation)
//
void Input::OnJoyKeyUp(int button)
{
	// Release the mapping captured on press, even if the setting changed.
	joystick_key_code[button] = 0;
	SetKeyStatus();
}

//
// GetKeyShift()
// get shift bits
//
Uint32 Input::GetKeyShift()
{
	Uint32 bits;

	// initialize
	bits = 0;

	// SHIFT
	if (key_buf[0x10] != 0) {
		bits |= SHIFT_BIT;
	}

	// CAPS
	if (key_buf[0x14] != 0) {
		bits |= CAPS_BIT;
	}

	// KANA
	if (key_buf[0x15] != 0) {
		bits |= KANA_BIT;
	}

	// if KANA is on, mask CAPS
	if ((bits & KANA_BIT) != 0) {
		bits &= ~CAPS_BIT;
	}

	return bits;
}

//
// GetKeyStatus()
// get key status
//
bool Input::GetKeyStatus(SDL_Scancode code)
{
	Uint32 data;

	if ((unsigned int)code >= SDL_arraysize(key_table)) return false;

	// get 32bit data
	data = key_table[code];
	if (data == 0) {
		return false;
	}

	if (key_buf[data] != 0) {
		return true;
	}
	else {
		return false;
	}
}

//
// SetKeyStatus()
// set key status to EMU
//
void Input::SetKeyStatus()
{
	// Multiple physical, soft and joystick inputs may own the same VM key.
	memset(key_status, 0, sizeof(key_status));
	for (int code = 0; code < SDL_arraysize(key_table); ++code) {
		const Uint32 data = key_table[code];
		if (data == 0 || data >= SDL_arraysize(key_status)) continue;
		if (key_hard_pressed[code] || key_soft_pressed[code]) {
			key_status[data] = (Uint8)data;
		}
	}
	if (key_soft_pressed[SDL_SCANCODE_RSHIFT]) key_status[0xa1] = 0xff;
	for (int button = 0; button < SDL_arraysize(joystick_key_code); ++button) {
		const Uint32 data = joystick_key_code[button];
		if (data > 0 && data < SDL_arraysize(key_status)) key_status[data] = (Uint8)data;
	}
	if (emu != NULL) emu->set_key_buffer(key_status);
}

//
// OnMouseMotion()
// mouse motion
//
void Input::OnMouseMotion(SDL_Event *e)
{
	int x;
	int y;
	bool button;

	// true mouse only
	if (e->motion.which == SDL_TOUCH_MOUSEID) {
		return;
	}

	// show softkey when mouse move
	if (setting->IsShowSoftKeyMouseMove() == false) {
		return;
	}

	// convert point
	x = e->motion.x;
	y = e->motion.y;
	if (video->ConvertSoftKeyPoint(&x, &y) == false) {
		return;
	}

	// get button state
	if ((e->motion.state & SDL_BUTTON_LMASK) != 0) {
		// SDL 2.0.3 returns SDL_BUTTON_LMASK after drag & drop action in Windows
		button = platform->CheckMouseButton();
	}
	else {
		button = false;
	}

	OnInputCommon(x, y, button, -1);
}

//
// OnMouseButtonDown()
// mouse button down
//
void Input::OnMouseButtonDown(SDL_Event *e)
{
	int x;
	int y;
	Uint32 mask;
	bool button;

	// true mouse only
	if (e->motion.which == SDL_TOUCH_MOUSEID) {
		return;
	}

	// middle button or x1 button ?
	if ((e->button.button == SDL_BUTTON_MIDDLE) || (e->button.button == SDL_BUTTON_X1)) {
		// disappear softkey
		return;
	}

	// convert point
	x = e->button.x;
	y = e->button.y;
	if (video->ConvertSoftKeyPoint(&x, &y) == false) {
		return;
	}

	// get button state
	mask = SDL_GetMouseState(NULL, NULL);
	if ((mask & SDL_BUTTON_LMASK) != 0) {
		button = true;
	}
	else {
		button = false;
	}

	OnInputCommon(x, y, button, -1);
}

//
// OnMouseButtonUp()
// mouse button up
//
void Input::OnMouseButtonUp(SDL_Event *e)
{
	int x;
	int y;
	Uint32 mask;
	bool button;

	// true mouse only
	if (e->motion.which == SDL_TOUCH_MOUSEID) {
		return;
	}

	// middle buttton ?
	if (((e->button.button == SDL_BUTTON_MIDDLE) && (e->button.state == SDL_RELEASED)) ||
		((e->button.button == SDL_BUTTON_X1) && (e->button.state == SDL_RELEASED))) {
		ResetList();
		return;
	}

	// convert point
	x = e->button.x;
	y = e->button.y;
	if (video->ConvertSoftKeyPoint(&x, &y) == false) {
		return;
	}

	// get button state
	mask = SDL_GetMouseState(NULL, NULL);
	if ((mask & SDL_BUTTON_LMASK) != 0) {
		button = true;
	}
	else {
		button = false;
	}

	OnInputCommon(x, y, button, -1);
}

//
// OnMouseWheel()
// mouse wheel
//
void Input::OnMouseWheel(SDL_Event *e)
{
	// true mouse ?
	if (e->wheel.which != SDL_TOUCH_MOUSEID) {
		// mouse only
		if (e->wheel.y > 0) {
			ChangeList(false, true);
		}
		if (e->wheel.y < 0) {
			ChangeList(true, false);
		}
	}
}

//
// OnJoystick()
// joystick axis and button
//
void Input::OnJoystick()
{
	uint32 status[2];
	Uint32 mix;
	Uint32 eor;
	Uint32 prev;
	int loop;
	bool menu;

	GetJoystick((uint32*)status);

	// set joy status
	if (setting->IsJoyKey() == true) {
		// keyboard emulation
		mix = (Uint32)(status[0] | (status[1] << 8));
		eor = joystick_prev ^ mix;
		prev = joystick_prev;

		// loop table
		for (loop=0; loop<15; loop++) {
			// now_state != prev_state ?
			if ((eor & 1) != 0) {
				if ((prev & 1) == 0) {
					// release -> press
					OnJoyKeyDown(loop);
				}
				else {
					// press -> release
					OnJoyKeyUp(loop);
				}
			}

			// next key
			eor >>= 1;
			prev >>= 1;
		}

		// save for next state
		joystick_prev = (Uint32)(status[0] | (status[1] << 8));
		status[0] = 0;
		status[1] = 0;
	}
	else {
		// true joystick emulation
		menu = false;
		if ((status[0] & 0xc0) != 0) {
			menu = true;
		}
		if (status[1] != 0) {
			menu = true;
		}
		if (menu == true) {
			joystick_prev = 0;
			status[0] = 0;
			status[1] = 0;
			app->EnterMenu(MENU_MAIN);
		}
		else {
			joystick_prev = 0;
			status[0] &= 0x3f;
			status[1] = 0;
		}
	}

	// set to EMU class
	emu->set_joy_buffer(status);
}

//
// OnFingerDown()
// finger down
//
void Input::OnFingerDown(SDL_Event *e)
{
	int x;
	int y;

	SDL_assert(e->tfinger.fingerId >= 0);

	// convert finger
	x = 0;
	y = 0;
	if (video->ConvertSoftKeyFinger(e->tfinger.x, e->tfinger.y, &x, &y) == false) {
		return;
	}

	OnInputCommon(x, y, true, (int)e->tfinger.fingerId);
}

//
// OnFingerUp()
// finger up
//
void Input::OnFingerUp(SDL_Event *e)
{
	int x;
	int y;

	SDL_assert(e->tfinger.fingerId >= 0);

	// convert finger
	x = 0;
	y = 0;
	if (video->ConvertSoftKeyFinger(e->tfinger.x, e->tfinger.y, &x, &y) == false) {
		OnInputMove(NULL, (int)e->tfinger.fingerId);
		return;
	}

	OnInputCommon(x, y, false, (int)e->tfinger.fingerId);
}

//
// OnFingerMotion()
// finger motion
//
void Input::OnFingerMotion(SDL_Event *e)
{
	int x;
	int y;

	SDL_assert(e->tfinger.fingerId >= 0);

	// convert finger
	x = 0;
	y = 0;
	if (video->ConvertSoftKeyFinger(e->tfinger.x, e->tfinger.y, &x, &y) == false) {
		OnInputMove(NULL, (int)e->tfinger.fingerId);
		return;
	}

	OnInputCommon(x, y, true, (int)e->tfinger.fingerId);
}

//
// OnInputCommon()
// input common
//
void Input::OnInputCommon(int x, int y, bool button, int touch)
{
	SoftKey *list;

	// softkey remains visible
	if (key_disp == false) {
		// check menu_delay
		if ((Uint32)(SDL_GetTicks() - menu_delay) < MENU_DELAY_TIME) {
			// after leaving menu, do not sense mouse
			return;
		}

		// rebuild ?
		if (key_rebuild == true) {
			// rebuild softkey by ChangeList()
			ChangeList(false, false);
			key_rebuild = false;
		}
		else {
			// display softkey
			DrawList(true);
			video->SetSoftKey(true);
			key_disp = true;
		}
	}

	// update tick
	key_tick = SDL_GetTicks();

	// initialize
	list = key_list;

	// loop 
	while (list != NULL) {
		list->OnInput(x, y, button, touch);

		// next
		list = list->GetNext();
	}
}

//
// OnInputMove
// input move from OnInputCommon()
//
void Input::OnInputMove(SoftKey *key, int finger)
{
	SoftKey *list;

	// initialize
	list = key_list;

	// loop 
	while (list != NULL) {
		if (list != key) {
			list->OnInputMove(finger);
		}

		// next
		list = list->GetNext();
	}
}

//
// ChangeCursorToNumPad
//
void Input::ChangeCursorToNumPad(bool enable)
{
	if (enable == true)
	{
		key_table[SDL_SCANCODE_RIGHT] = 102;
		key_table[SDL_SCANCODE_LEFT] = 100;
		key_table[SDL_SCANCODE_DOWN] = 98;
		key_table[SDL_SCANCODE_UP] = 104;
	} else {
		key_table[SDL_SCANCODE_RIGHT] = key_base[SDL_SCANCODE_RIGHT];
		key_table[SDL_SCANCODE_LEFT] = key_base[SDL_SCANCODE_LEFT];
		key_table[SDL_SCANCODE_DOWN] = key_base[SDL_SCANCODE_DOWN];
		key_table[SDL_SCANCODE_UP] = key_base[SDL_SCANCODE_UP];
	}
	SetKeyStatus();
}

//
// ChangeNumToNumPad
//
void Input::ChangeNumToNumPad(bool enable)
{
	for (size_t i = 0; i < 10; i++)
	{
		if (enable == true)
		{
			if (i == 9) {
				key_table[SDL_SCANCODE_0] = 96;
			} else {
				key_table[SDL_SCANCODE_1 + i] = 97 + i;
			}
		} else {
			key_table[SDL_SCANCODE_1 + i] = key_base[SDL_SCANCODE_1 + i];
		}
	}	
	SetKeyStatus();
}


//
// key mapping table (base)
//
const Uint32 Input::key_base[0x120] = {
	// 0x00-0x0f
	0x00000000,		// (SDL_SCANCODE_UNKNOWN: menu)
	0x00000000,		// (SDL_SCANCODE_UNKNOWN+1:next softkey)
	0x00000000,
	0x00000000,
	0x00000041,		// SDL_SCANCODE_A
	0x00000042,		// SDL_SCANCODE_B
	0x00000043,		// SDL_SCANCODE_C
	0x00000044,		// SDL_SCANCODE_D
	0x00000045,		// SDL_SCANCODE_E
	0x00000046,		// SDL_SCANCODE_F
	0x00000047,		// SDL_SCANCODE_G
	0x00000048,		// SDL_SCANCODE_H
	0x00000049,		// SDL_SCANCODE_I
	0x0000004a,		// SDL_SCANCODE_J
	0x0000004b,		// SDL_SCANCODE_K
	0x0000004c,		// SDL_SCANCODE_L

	// 0x10-0x1f
	0x0000004d,		// SDL_SCANCODE_M
	0x0000004e,		// SDL_SCANCODE_N
	0x0000004f,		// SDL_SCANCODE_O
	0x00000050,		// SDL_SCANCODE_P
	0x00000051,		// SDL_SCANCODE_Q
	0x00000052,		// SDL_SCANCODE_R
	0x00000053,		// SDL_SCANCODE_S
	0x00000054,		// SDL_SCANCODE_T
	0x00000055,		// SDL_SCANCODE_U
	0x00000056,		// SDL_SCANCODE_V
	0x00000057,		// SDL_SCANCODE_W
	0x00000058,		// SDL_SCANCODE_X
	0x00000059,		// SDL_SCANCODE_Y
	0x0000005a,		// SDL_SCANCODE_Z
	0x00000031,		// SDL_SCANCODE_1
	0x00000032,		// SDL_SCANCODE_2

	// 0x20-0x2f
	0x00000033,		// SDL_SCANCODE_3
	0x00000034,		// SDL_SCANCODE_4
	0x00000035,		// SDL_SCANCODE_5
	0x00000036,		// SDL_SCANCODE_6
	0x00000037,		// SDL_SCANCODE_7
	0x00000038,		// SDL_SCANCODE_8
	0x00000039,		// SDL_SCANCODE_9
	0x00000030,		// SDL_SCANCODE_0
	0x0000001a,		// SDL_SCANCODE_RETURN
	0x0000001b,		// SDL_SCANCODE_ESCAPE
	0x00000008,		// SDL_SCANCODE_BACKSPACE
	0x00000009,		// SDL_SCANCODE_TAB
	0x00000020,		// SDL_SCANCODE_SPACE
	0x000000bd,		// SDL_SCANCODE_MINUS
	0x000000de,		// SDL_SCANCODE_EQUALS
	0x000000c0,		// SDL_SCANCODE_LEFTBRACKET

	// 0x30-0x3f
	0x000000db,		// SDL_SCANCODE_RIGHTBLACKET
	0x000000dd,		// SDL_SCANCODE_BACKSLASH
	0x000000e2,		// SDL_SCANCODE_NONUSHASH
	0x000000bb,		// SDL_SCANCODE_SEMICOLON
	0x000000ba,		// SDL_SCANCODE_APOSTROPHE
	0x000000dc,		// SDL_SCANCODE_GRAVE
	0x000000bc,		// SDL_SCANCODE_COMMA
	0x000000be,		// SDL_SCANCODE_PERIOD
	0x000000bf,		// SDL_SCANCODE_SLASH
	0x00000014,		// SDL_SCANCODE_CAPSLOCK
	0x00000070,		// SDL_SCANCODE_F1
	0x00000071,		// SDL_SCANCODE_F2
	0x00000072,		// SDL_SCANCODE_F3
	0x00000073,		// SDL_SCANCODE_F4
	0x00000074,		// SDL_SCANCODE_F5
	0x00000075,		// SDL_SCANCODE_F6

	// 0x40-0x4f
	0x00000076,		// SDL_SCANCODE_F7
	0x00000077,		// SDL_SCANCODE_F8
	0x00000078,		// SDL_SCANCODE_F9
	0x00000079,		// SDL_SCANCODE_F10
	0x00000000,		// [Unassigned]SDL_SCANCODE_F11
	0x00000000,		// [Unassigned]SDL_SCANCODE_F12
	0x0000007b,		// SDL_SCANCODE_PRINTSCREEN
	0x00000015,		// SDL_SCANCODE_SCROLLLOCK
	0x00000013,		// SDL_SCANCODE_PAUSE
	0x0000002d,		// SDL_SCANCODE_INSERT
	0x00000024,		// SDL_SCANCODE_HOME
	0x00000022,		// SDL_SCANCODE_PAGEUP
	0x0000003b,		// SDL_SCANCODE_DELETE
	0x00000023,		// SDL_SCANCODE_END
	0x00000021,		// SDL_SCANCODE_PAGEDOWN
	0x00000027,		// SDL_SCANCODE_RIGHT

	// 0x50-0x5f
	0x00000025,		// SDL_SCANCODE_LEFT
	0x00000028,		// SDL_SCANCODE_DOWN
	0x00000026,		// SDL_SCANCODE_UP
	0x00000000,		// [Unassigned]SDL_SCANCODE_NUMLOCKCLEAR
	0x000000bf,		// SDL_SCANCODE_KP_DIVIDE
	0x0000006a,		// SDL_SCANCODE_KP_MULTIPLY
	0x000000bd,		// SDL_SCANCODE_KP_MINUS
	0x0000006b,		// SDL_SCANCODE_KP_PLUS
	0x0000005e,		// SDL_SCANCODE_KP_ENTER
	0x00000061,		// SDL_SCANCODE_KP_1
	0x00000062,		// SDL_SCANCODE_KP_2
	0x00000063,		// SDL_SCANCODE_KP_3
	0x00000064,		// SDL_SCANCODE_KP_4
	0x00000065,		// SDL_SCANCODE_KP_5
	0x00000066,		// SDL_SCANCODE_KP_6
	0x00000067,		// SDL_SCANCODE_KP_7

	// 0x60-0x6f
	0x00000068,		// SDL_SCANCODE_KP_8
	0x00000069,		// SDL_SCANCODE_KP_9
	0x00000060,		// SDL_SCANCODE_KP_0
	0x0000006e,		// SDL_SCANCODE_KP_PERIOD
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000092,		// SD_SCANCODE_KP_EQUALS
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,

	// 0x70-0x7f
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,

	// 0x80-0x8f
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x0000006c,		// SDL_SCANCODE_KP_COMMA
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x0000001c,		// SDL_SCANCODE_INTERNATIONAL4
	0x0000001d,		// SDL_SCANCODE_INTERNATIONAL5
	0x00000018,		// SDL_SCANCODE_INTERNATIONAL6
	0x00000000,
	0x00000000,
	0x00000000,

	// 0x90-0x9f
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,

	// 0xa0-0xaf
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,

	// 0xb0-0xbf
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,

	// 0xc0-0xcf
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,

	// 0xd0-0xdf
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,

	// 0xe0-0xef
	0x00000011,		// SDL_SCANCODE_LCTRL
	0x000000a0,		// SDL_SCANCODE_LSHIFT
	0x00000012,		// SDL_SCANCODE_LALT
	0x00000000,
	0x00000019,		// SDL_SCANCODE_RCTRL
	0x000000a1,		// SDL_SCANCODE_RSHIFT
	0x00000000,		// [Unassigned]SDL_SCANCODE_RALT
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,

	// 0xf0-0xff
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,

	// 0x100-0x10f
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,

	// 0x110-0x11f
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000
};

//
// joystick button to bit table
//
const Uint32 Input::joystick_button[15 * 2] = {
	0x00000010, 0x00000000,				// SDL_CONTROLLER_BUTTON_A
	0x00000020, 0x00000000,				// SDL_CONTROLLER_BUTTON_B
	0x00000040, 0x00000000,				// SDL_CONTROLLER_BUTTON_X
	0x00000080, 0x00000000,				// SDL_CONTROLLER_BUTTON_Y
	0x00000000, 0x00000001,				// SDL_CONTROLLER_BUTTON_BACK
	0x00000000, 0x00000002,				// SDL_CONTROLLER_BUTTON_GUIDE
	0x00000000, 0x00000004,				// SDL_CONTROLLER_BUTTON_START
	0x00000000, 0x00000008,				// SDL_CONTROLLER_BUTTON_LEFTSTICK
	0x00000000, 0x00000010,				// SDL_CONTROLLER_BUTTON_RIGHTSTICK
	0x00000000, 0x00000020,				// SDL_CONTROLLER_BUTTON_LEFTSHOULDER
	0x00000000, 0x00000040,				// SDL_CONTROLLER_BUTTON_RIGHTSHOULDER
	0x00000001, 0x00000000,				// SDL_CONTROLLER_BUTTON_DPAD_UP
	0x00000002, 0x00000000,				// SDL_CONTROLLER_BUTTON_DPAD_DOWN
	0x00000004, 0x00000000,				// SDL_CONTROLLER_BUTTON_DPAD_LEFT
	0x00000008, 0x00000000,				// SDL_CONTROLLER_BUTTON_DPAD_RIGHT
};

//
// software keyboard (full key)
//
const softkey_param Input::softkey_full[66 + 1] = {
	{ 69, 1, 8, 6, 1, "MENU", 0 },
	{ 2, 1, 8, 6, 1, "NEXT", 1 },

	{  2 + 5 * 0, 12 + 7 * 0, 5, 6, 1, "ESC", (int)SDL_SCANCODE_ESCAPE },
	{  3 + 5 * 1, 12 + 7 * 0, 4, 6, 1, "1", (int)SDL_SCANCODE_1 },
	{  3 + 5 * 2, 12 + 7 * 0, 4, 6, 1, "2", (int)SDL_SCANCODE_2 },
	{  3 + 5 * 3, 12 + 7 * 0, 4, 6, 1, "3", (int)SDL_SCANCODE_3 },
	{  3 + 5 * 4, 12 + 7 * 0, 4, 6, 1, "4", (int)SDL_SCANCODE_4 },
	{  3 + 5 * 5, 12 + 7 * 0, 4, 6, 1, "5", (int)SDL_SCANCODE_5 },
	{  3 + 5 * 6, 12 + 7 * 0, 4, 6, 1, "6", (int)SDL_SCANCODE_6 },
	{  3 + 5 * 7, 12 + 7 * 0, 4, 6, 1, "7", (int)SDL_SCANCODE_7 },
	{  3 + 5 * 8, 12 + 7 * 0, 4, 6, 1, "8", (int)SDL_SCANCODE_8 },
	{  3 + 5 * 9, 12 + 7 * 0, 4, 6, 1, "9", (int)SDL_SCANCODE_9 },
	{  3 + 5 * 10, 12 + 7 * 0, 4, 6, 1, "0", (int)SDL_SCANCODE_0 },
	{  3 + 5 * 11, 12 + 7 * 0, 4, 6, 1, "-", (int)SDL_SCANCODE_MINUS },
	{  3 + 5 * 12, 12 + 7 * 0, 4, 6, 1, "^", (int)SDL_SCANCODE_EQUALS },
	{  3 + 5 * 13, 12 + 7 * 0, 4, 6, 1, "\\", (int)SDL_SCANCODE_GRAVE },
	{  3 + 5 * 14, 12 + 7 * 0, 4, 6, 1, "BS", (int)SDL_SCANCODE_BACKSPACE },

	{  2 + 5 * 0, 12 + 7 * 1, 8, 6, 1, "TAB", (int)SDL_SCANCODE_TAB },
	{  6 + 5 * 1, 12 + 7 * 1, 4, 6, 1, "Q", (int)SDL_SCANCODE_Q },
	{  6 + 5 * 2, 12 + 7 * 1, 4, 6, 1, "W", (int)SDL_SCANCODE_W },
	{  6 + 5 * 3, 12 + 7 * 1, 4, 6, 1, "E", (int)SDL_SCANCODE_E },
	{  6 + 5 * 4, 12 + 7 * 1, 4, 6, 1, "R", (int)SDL_SCANCODE_R },
	{  6 + 5 * 5, 12 + 7 * 1, 4, 6, 1, "T", (int)SDL_SCANCODE_T },
	{  6 + 5 * 6, 12 + 7 * 1, 4, 6, 1, "Y", (int)SDL_SCANCODE_Y },
	{  6 + 5 * 7, 12 + 7 * 1, 4, 6, 1, "U", (int)SDL_SCANCODE_U },
	{  6 + 5 * 8, 12 + 7 * 1, 4, 6, 1, "I", (int)SDL_SCANCODE_I },
	{  6 + 5 * 9, 12 + 7 * 1, 4, 6, 1, "O", (int)SDL_SCANCODE_O },
	{  6 + 5 * 10, 12 + 7 * 1, 4, 6, 1, "P", (int)SDL_SCANCODE_P },
	{  6 + 5 * 11, 12 + 7 * 1, 4, 6, 1, "@", (int)SDL_SCANCODE_LEFTBRACKET },
	{  6 + 5 * 12, 12 + 7 * 1, 4, 6, 1, "[", (int)SDL_SCANCODE_RIGHTBRACKET },

	{  2 + 5 * 0, 12 + 7 * 2, 4, 6, 1, "CT\nRL", (int)SDL_SCANCODE_LCTRL },
	{  2 + 5 * 1, 12 + 7 * 2, 4, 6, 1, "CA\nPS", (int)SDL_SCANCODE_CAPSLOCK },
	{  2 + 5 * 2, 12 + 7 * 2, 4, 6, 1, "A", (int)SDL_SCANCODE_A },
	{  2 + 5 * 3, 12 + 7 * 2, 4, 6, 1, "S", (int)SDL_SCANCODE_S },
	{  2 + 5 * 4, 12 + 7 * 2, 4, 6, 1, "D", (int)SDL_SCANCODE_D },
	{  2 + 5 * 5, 12 + 7 * 2, 4, 6, 1, "F", (int)SDL_SCANCODE_F },
	{  2 + 5 * 6, 12 + 7 * 2, 4, 6, 1, "G", (int)SDL_SCANCODE_G },
	{  2 + 5 * 7, 12 + 7 * 2, 4, 6, 1, "H", (int)SDL_SCANCODE_H },
	{  2 + 5 * 8, 12 + 7 * 2, 4, 6, 1, "J", (int)SDL_SCANCODE_J },
	{  2 + 5 * 9, 12 + 7 * 2, 4, 6, 1, "K", (int)SDL_SCANCODE_K },
	{  2 + 5 * 10, 12 + 7 * 2, 4, 6, 1, "L", (int)SDL_SCANCODE_L },
	{  2 + 5 * 11, 12 + 7 * 2, 4, 6, 1, ";", (int)SDL_SCANCODE_SEMICOLON },
	{  2 + 5 * 12, 12 + 7 * 2, 4, 6, 1, ":", (int)SDL_SCANCODE_APOSTROPHE },
	{  2 + 5 * 13, 12 + 7 * 2, 4, 6, 1, "]", (int)SDL_SCANCODE_BACKSLASH },

	{  2 + 6 * 0, 12 + 7 * 3, 10, 6, 1, "SHIFT", (int)SDL_SCANCODE_LSHIFT },
	{  8 + 5 * 1, 12 + 7 * 3, 4, 6, 1, "Z", (int)SDL_SCANCODE_Z },
	{  8 + 5 * 2, 12 + 7 * 3, 4, 6, 1, "X", (int)SDL_SCANCODE_X },
	{  8 + 5 * 3, 12 + 7 * 3, 4, 6, 1, "C", (int)SDL_SCANCODE_C },
	{  8 + 5 * 4, 12 + 7 * 3, 4, 6, 1, "V", (int)SDL_SCANCODE_V },
	{  8 + 5 * 5, 12 + 7 * 3, 4, 6, 1, "B", (int)SDL_SCANCODE_B },
	{  8 + 5 * 6, 12 + 7 * 3, 4, 6, 1, "N", (int)SDL_SCANCODE_N },
	{  8 + 5 * 7, 12 + 7 * 3, 4, 6, 1, "M", (int)SDL_SCANCODE_M },
	{  8 + 5 * 8, 12 + 7 * 3, 4, 6, 1, ",", (int)SDL_SCANCODE_COMMA },
	{  8 + 5 * 9, 12 + 7 * 3, 4, 6, 1, ".", (int)SDL_SCANCODE_PERIOD },
	{  8 + 5 * 10, 12 + 7 * 3, 4, 6, 1, "/", (int)SDL_SCANCODE_SLASH },
	{  8 + 5 * 11, 12 + 7 * 3, 4, 6, 1, "_", (int)SDL_SCANCODE_NONUSHASH },
	{  8 + 5 * 12, 12 + 7 * 3, 9, 6, 1, "SHIFT", (int)SDL_SCANCODE_RSHIFT },

	{  8 + 5 * 0, 12 + 7 * 4, 4, 6, 1, "\xb6""\xc5", (int)SDL_SCANCODE_SCROLLLOCK },
	{  8 + 5 * 1, 12 + 7 * 4, 4, 6, 1, "GR\nPH", (int)SDL_SCANCODE_LALT },
	{  8 + 5 * 2, 12 + 7 * 4, 9, 6, 1, "SPACE\n""\x8c""\x88""\x92""\xe8", (int)SDL_SCANCODE_INTERNATIONAL5 },
	{  8 + 5 * 4, 12 + 7 * 4, 11, 6, 1, "SPACE\n", (int)SDL_SCANCODE_SPACE },
	{  8 + 5 * 6 + 2, 12 + 7 * 4, 12, 6, 1, "SPACE\n""\x95""\xcf""\x8a""\xb7", (int)SDL_SCANCODE_INTERNATIONAL4 },
	{  8 + 5 * 9, 12 + 7 * 4, 4, 6, 1, "PC", (int)SDL_SCANCODE_INTERNATIONAL6 },
	{  8 + 5 * 10, 12 + 7 * 4, 4, 6, 1, "\x91""\x53""\n""\x8a""\x70", (int)SDL_SCANCODE_RCTRL },

	{  2 + 5 * 14, 12 + 7 * 1, 5, 13, 1, "RET", (int)SDL_SCANCODE_RETURN },

	{ 0, 0, 0, 0, 1, "", -1 }
};

//
// software keyboard (cursor key + ten key)
//
const softkey_param Input::softkey_curten[31 + 1] = {
	{ 69, 1, 8, 6, 1, "MENU", 0 },
	{ 2, 1, 8, 6, 1, "NEXT", 1 },

	{  5 + 7 * 0, 12 + 7 * 0, 6, 6, 1, "ROLL\nUP", (int)SDL_SCANCODE_PAGEDOWN },
	{  6 + 7 * 1, 12 + 7 * 0, 6, 6, 1, "ROLL\nDOWN", (int)SDL_SCANCODE_PAGEUP },

	{  5 + 7 * 0, 12 + 7 * 1, 6, 6, 1, "INS", (int)SDL_SCANCODE_INSERT },
	{  6 + 7 * 1, 12 + 7 * 1, 6, 6, 1, "DEL", (int)SDL_SCANCODE_DELETE },

	{  2 + 7 * 1, 12 + 7 * 2, 6, 6, 4, "\x1e", (int)SDL_SCANCODE_UP },
	{  2 + 7 * 1, 12 + 7 * 3, 6, 6, 4, "\x1f", (int)SDL_SCANCODE_DOWN },
	{  2 + 7 * 0, 12 + 7 * 3, 6, 6, 4, "\x1d", (int)SDL_SCANCODE_LEFT },
	{  2 + 7 * 2, 12 + 7 * 3, 6, 6, 4, "\x1c", (int)SDL_SCANCODE_RIGHT },

	{  2 + 7 * 0, 12 + 7 * 4, 20, 6, 1, "SPACE", (int)SDL_SCANCODE_SPACE },

	{ 50 + 7 * 0, 12 + 7 * 0, 6, 6, 1, "CLR", (int)SDL_SCANCODE_HOME },
	{ 50 + 7 * 1, 12 + 7 * 0, 6, 6, 1, "HELP", (int)SDL_SCANCODE_END },
	{ 50 + 7 * 2, 12 + 7 * 0, 6, 6, 4, "-", (int)SDL_SCANCODE_KP_MINUS },
	{ 50 + 7 * 3, 12 + 7 * 0, 6, 6, 4, "/", (int)SDL_SCANCODE_KP_DIVIDE },

	{ 50 + 7 * 0, 12 + 7 * 1, 6, 6, 4, "7", (int)SDL_SCANCODE_KP_7 },
	{ 50 + 7 * 1, 12 + 7 * 1, 6, 6, 4, "8", (int)SDL_SCANCODE_KP_8 },
	{ 50 + 7 * 2, 12 + 7 * 1, 6, 6, 4, "9", (int)SDL_SCANCODE_KP_9 },
	{ 50 + 7 * 3, 12 + 7 * 1, 6, 6, 4, "*", (int)SDL_SCANCODE_KP_MULTIPLY },

	{ 50 + 7 * 0, 12 + 7 * 2, 6, 6, 4, "4", (int)SDL_SCANCODE_KP_4 },
	{ 50 + 7 * 1, 12 + 7 * 2, 6, 6, 4, "5", (int)SDL_SCANCODE_KP_5 },
	{ 50 + 7 * 2, 12 + 7 * 2, 6, 6, 4, "6", (int)SDL_SCANCODE_KP_6 },
	{ 50 + 7 * 3, 12 + 7 * 2, 6, 6, 4, "+", (int)SDL_SCANCODE_KP_PLUS },

	{ 50 + 7 * 0, 12 + 7 * 3, 6, 6, 4, "1", (int)SDL_SCANCODE_KP_1 },
	{ 50 + 7 * 1, 12 + 7 * 3, 6, 6, 4, "2", (int)SDL_SCANCODE_KP_2 },
	{ 50 + 7 * 2, 12 + 7 * 3, 6, 6, 4, "3", (int)SDL_SCANCODE_KP_3 },
	{ 50 + 7 * 3, 12 + 7 * 3, 6, 6, 4, "=", (int)SDL_SCANCODE_KP_EQUALS },

	{ 50 + 7 * 0, 12 + 7 * 4, 6, 6, 4, "0", (int)SDL_SCANCODE_KP_0 },
	{ 50 + 7 * 1, 12 + 7 * 4, 6, 6, 4, ",", (int)SDL_SCANCODE_KP_COMMA },
	{ 50 + 7 * 2, 12 + 7 * 4, 6, 6, 4, ".", (int)SDL_SCANCODE_KP_PERIOD },
	{ 50 + 7 * 3, 12 + 7 * 4, 6, 6, 1, "ENT", (int)SDL_SCANCODE_KP_ENTER },

	{ 0, 0, 0, 0, 1, "", -1 }
};

//
// software keyboard (ten key + cursor key)
//
const softkey_param Input::softkey_tencur[31 + 1] = {
	{ 69, 1, 8, 6, 1, "MENU", 0 },
	{ 2, 1, 8, 6, 1, "NEXT", 1 },

	{ 60 + 7 * 0, 12 + 7 * 0, 6, 6, 1, "ROLL\nUP", (int)SDL_SCANCODE_PAGEDOWN },
	{ 61 + 7 * 1, 12 + 7 * 0, 6, 6, 1, "ROLL\nDOWN", (int)SDL_SCANCODE_PAGEUP },

	{ 60 + 7 * 0, 12 + 7 * 1, 6, 6, 1, "INS", (int)SDL_SCANCODE_INSERT },
	{ 61 + 7 * 1, 12 + 7 * 1, 6, 6, 1, "DEL", (int)SDL_SCANCODE_DELETE },

	{ 50 + 7 * 2, 12 + 7 * 2, 6, 6, 4, "\x1e", (int)SDL_SCANCODE_UP },
	{ 50 + 7 * 2, 12 + 7 * 3, 6, 6, 4, "\x1f", (int)SDL_SCANCODE_DOWN },
	{ 50 + 7 * 1, 12 + 7 * 3, 6, 6, 4, "\x1d", (int)SDL_SCANCODE_LEFT },
	{ 50 + 7 * 3, 12 + 7 * 3, 6, 6, 4, "\x1c", (int)SDL_SCANCODE_RIGHT },

	{ 50 + 7 * 1, 12 + 7 * 4, 20, 6, 1, "SPACE", (int)SDL_SCANCODE_SPACE },

	{  2 + 7 * 0, 12 + 7 * 0, 6, 6, 1, "CLR", (int)SDL_SCANCODE_HOME },
	{  2 + 7 * 1, 12 + 7 * 0, 6, 6, 1, "HELP", (int)SDL_SCANCODE_END },
	{  2 + 7 * 2, 12 + 7 * 0, 6, 6, 4, "-", (int)SDL_SCANCODE_KP_MINUS },
	{  2 + 7 * 3, 12 + 7 * 0, 6, 6, 4, "/", (int)SDL_SCANCODE_KP_DIVIDE },

	{  2 + 7 * 0, 12 + 7 * 1, 6, 6, 4, "7", (int)SDL_SCANCODE_KP_7 },
	{  2 + 7 * 1, 12 + 7 * 1, 6, 6, 4, "8", (int)SDL_SCANCODE_KP_8 },
	{  2 + 7 * 2, 12 + 7 * 1, 6, 6, 4, "9", (int)SDL_SCANCODE_KP_9 },
	{  2 + 7 * 3, 12 + 7 * 1, 6, 6, 4, "*", (int)SDL_SCANCODE_KP_MULTIPLY },

	{  2 + 7 * 0, 12 + 7 * 2, 6, 6, 4, "4", (int)SDL_SCANCODE_KP_4 },
	{  2 + 7 * 1, 12 + 7 * 2, 6, 6, 4, "5", (int)SDL_SCANCODE_KP_5 },
	{  2 + 7 * 2, 12 + 7 * 2, 6, 6, 4, "6", (int)SDL_SCANCODE_KP_6 },
	{  2 + 7 * 3, 12 + 7 * 2, 6, 6, 4, "+", (int)SDL_SCANCODE_KP_PLUS },

	{  2 + 7 * 0, 12 + 7 * 3, 6, 6, 4, "1", (int)SDL_SCANCODE_KP_1 },
	{  2 + 7 * 1, 12 + 7 * 3, 6, 6, 4, "2", (int)SDL_SCANCODE_KP_2 },
	{  2 + 7 * 2, 12 + 7 * 3, 6, 6, 4, "3", (int)SDL_SCANCODE_KP_3 },
	{  2 + 7 * 3, 12 + 7 * 3, 6, 6, 4, "=", (int)SDL_SCANCODE_KP_EQUALS },

	{  2 + 7 * 0, 12 + 7 * 4, 6, 6, 4, "0", (int)SDL_SCANCODE_KP_0 },
	{  2 + 7 * 1, 12 + 7 * 4, 6, 6, 4, ",", (int)SDL_SCANCODE_KP_COMMA },
	{  2 + 7 * 2, 12 + 7 * 4, 6, 6, 4, ".", (int)SDL_SCANCODE_KP_PERIOD },
	{  2 + 7 * 3, 12 + 7 * 4, 6, 6, 1, "ENT", (int)SDL_SCANCODE_KP_ENTER },

	{ 0, 0, 0, 0, 1, "", -1 }
};

//
// software keyboard (function key + ten key)
//
const softkey_param Input::softkey_functen[35 + 1] = {
	{ 69, 1, 8, 6, 1, "MENU", 0 },
	{ 2, 1, 8, 6, 1, "NEXT", 1 },

	{  2 + 7 * 0, 12 + 7 * 0, 6, 6, 1, "STOP", (int)SDL_SCANCODE_PAUSE },
	{  2 + 7 * 1, 12 + 7 * 0, 6, 6, 1, "COPY", (int)SDL_SCANCODE_PRINTSCREEN },

	{  2 + 7 * 0, 12 + 7 * 1, 13, 6, 1, "SPACE", (int)SDL_SCANCODE_SPACE },

	{  2 + 7 * 0 + 9 * 0, 12 + 7 * 3, 8, 6, 1, "f""\xa5""1", (int)SDL_SCANCODE_F1 },
	{  2 + 7 * 0 + 9 * 1, 12 + 7 * 3, 8, 6, 1, "f""\xa5""2", (int)SDL_SCANCODE_F2 },
	{  2 + 7 * 0 + 9 * 2, 12 + 7 * 3, 8, 6, 1, "f""\xa5""3", (int)SDL_SCANCODE_F3 },
	{  2 + 7 * 0 + 9 * 3, 12 + 7 * 3, 8, 6, 1, "f""\xa5""4", (int)SDL_SCANCODE_F4 },
	{  2 + 7 * 0 + 9 * 4, 12 + 7 * 3, 8, 6, 1, "f""\xa5""5", (int)SDL_SCANCODE_F5 },

	{  2 + 7 * 0 + 9 * 0, 12 + 7 * 4, 8, 6, 1, "f""\xa5""6", (int)SDL_SCANCODE_F6 },
	{  2 + 7 * 0 + 9 * 1, 12 + 7 * 4, 8, 6, 1, "f""\xa5""7", (int)SDL_SCANCODE_F7 },
	{  2 + 7 * 0 + 9 * 2, 12 + 7 * 4, 8, 6, 1, "f""\xa5""8", (int)SDL_SCANCODE_F8 },
	{  2 + 7 * 0 + 9 * 3, 12 + 7 * 4, 8, 6, 1, "f""\xa5""9", (int)SDL_SCANCODE_F9 },
	{  2 + 7 * 0 + 9 * 4, 12 + 7 * 4, 8, 6, 1, "f""\xa5""10", (int)SDL_SCANCODE_F10 },

	{ 50 + 7 * 0, 12 + 7 * 0, 6, 6, 1, "CLR", (int)SDL_SCANCODE_HOME },
	{ 50 + 7 * 1, 12 + 7 * 0, 6, 6, 1, "HELP", (int)SDL_SCANCODE_END },
	{ 50 + 7 * 2, 12 + 7 * 0, 6, 6, 4, "-", (int)SDL_SCANCODE_KP_MINUS },
	{ 50 + 7 * 3, 12 + 7 * 0, 6, 6, 4, "/", (int)SDL_SCANCODE_KP_DIVIDE },

	{ 50 + 7 * 0, 12 + 7 * 1, 6, 6, 4, "7", (int)SDL_SCANCODE_KP_7 },
	{ 50 + 7 * 1, 12 + 7 * 1, 6, 6, 4, "8", (int)SDL_SCANCODE_KP_8 },
	{ 50 + 7 * 2, 12 + 7 * 1, 6, 6, 4, "9", (int)SDL_SCANCODE_KP_9 },
	{ 50 + 7 * 3, 12 + 7 * 1, 6, 6, 4, "*", (int)SDL_SCANCODE_KP_MULTIPLY },

	{ 50 + 7 * 0, 12 + 7 * 2, 6, 6, 4, "4", (int)SDL_SCANCODE_KP_4 },
	{ 50 + 7 * 1, 12 + 7 * 2, 6, 6, 4, "5", (int)SDL_SCANCODE_KP_5 },
	{ 50 + 7 * 2, 12 + 7 * 2, 6, 6, 4, "6", (int)SDL_SCANCODE_KP_6 },
	{ 50 + 7 * 3, 12 + 7 * 2, 6, 6, 4, "+", (int)SDL_SCANCODE_KP_PLUS },

	{ 50 + 7 * 0, 12 + 7 * 3, 6, 6, 4, "1", (int)SDL_SCANCODE_KP_1 },
	{ 50 + 7 * 1, 12 + 7 * 3, 6, 6, 4, "2", (int)SDL_SCANCODE_KP_2 },
	{ 50 + 7 * 2, 12 + 7 * 3, 6, 6, 4, "3", (int)SDL_SCANCODE_KP_3 },
	{ 50 + 7 * 3, 12 + 7 * 3, 6, 6, 4, "=", (int)SDL_SCANCODE_KP_EQUALS },

	{ 50 + 7 * 0, 12 + 7 * 4, 6, 6, 4, "0", (int)SDL_SCANCODE_KP_0 },
	{ 50 + 7 * 1, 12 + 7 * 4, 6, 6, 4, ",", (int)SDL_SCANCODE_KP_COMMA },
	{ 50 + 7 * 2, 12 + 7 * 4, 6, 6, 4, ".", (int)SDL_SCANCODE_KP_PERIOD },
	{ 50 + 7 * 3, 12 + 7 * 4, 6, 6, 1, "ENT", (int)SDL_SCANCODE_KP_ENTER },

	{ 0, 0, 0, 0, 1, "", -1 }
};

//
// software keyboard (ten key + function key)
//
const softkey_param Input::softkey_tenfunc[35 + 1] = {
	{ 69, 1, 8, 6, 1, "MENU", 0 },
	{ 2, 1, 8, 6, 1, "NEXT", 1 },

	{ 50 + 7 * 2, 12 + 7 * 0, 6, 6, 1, "STOP", (int)SDL_SCANCODE_PAUSE },
	{ 50 + 7 * 3, 12 + 7 * 0, 6, 6, 1, "COPY", (int)SDL_SCANCODE_PRINTSCREEN },

	{ 50 + 7 * 2, 12 + 7 * 1, 13, 6, 1, "SPACE", (int)SDL_SCANCODE_SPACE },

	{ 33 + 7 * 0 + 9 * 0, 12 + 7 * 3, 8, 6, 1, "f""\xa5""1", (int)SDL_SCANCODE_F1 },
	{ 33 + 7 * 0 + 9 * 1, 12 + 7 * 3, 8, 6, 1, "f""\xa5""2", (int)SDL_SCANCODE_F2 },
	{ 33 + 7 * 0 + 9 * 2, 12 + 7 * 3, 8, 6, 1, "f""\xa5""3", (int)SDL_SCANCODE_F3 },
	{ 33 + 7 * 0 + 9 * 3, 12 + 7 * 3, 8, 6, 1, "f""\xa5""4", (int)SDL_SCANCODE_F4 },
	{ 33 + 7 * 0 + 9 * 4, 12 + 7 * 3, 8, 6, 1, "f""\xa5""5", (int)SDL_SCANCODE_F5 },

	{ 33 + 7 * 0 + 9 * 0, 12 + 7 * 4, 8, 6, 1, "f""\xa5""6", (int)SDL_SCANCODE_F6 },
	{ 33 + 7 * 0 + 9 * 1, 12 + 7 * 4, 8, 6, 1, "f""\xa5""7", (int)SDL_SCANCODE_F7 },
	{ 33 + 7 * 0 + 9 * 2, 12 + 7 * 4, 8, 6, 1, "f""\xa5""8", (int)SDL_SCANCODE_F8 },
	{ 33 + 7 * 0 + 9 * 3, 12 + 7 * 4, 8, 6, 1, "f""\xa5""9", (int)SDL_SCANCODE_F9 },
	{ 33 + 7 * 0 + 9 * 4, 12 + 7 * 4, 8, 6, 1, "f""\xa5""10", (int)SDL_SCANCODE_F10 },

	{  2 + 7 * 0, 12 + 7 * 0, 6, 6, 1, "CLR", (int)SDL_SCANCODE_HOME },
	{  2 + 7 * 1, 12 + 7 * 0, 6, 6, 1, "HELP", (int)SDL_SCANCODE_END },
	{  2 + 7 * 2, 12 + 7 * 0, 6, 6, 4, "-", (int)SDL_SCANCODE_KP_MINUS },
	{  2 + 7 * 3, 12 + 7 * 0, 6, 6, 4, "/", (int)SDL_SCANCODE_KP_DIVIDE },

	{  2 + 7 * 0, 12 + 7 * 1, 6, 6, 4, "7", (int)SDL_SCANCODE_KP_7 },
	{  2 + 7 * 1, 12 + 7 * 1, 6, 6, 4, "8", (int)SDL_SCANCODE_KP_8 },
	{  2 + 7 * 2, 12 + 7 * 1, 6, 6, 4, "9", (int)SDL_SCANCODE_KP_9 },
	{  2 + 7 * 3, 12 + 7 * 1, 6, 6, 4, "*", (int)SDL_SCANCODE_KP_MULTIPLY },

	{  2 + 7 * 0, 12 + 7 * 2, 6, 6, 4, "4", (int)SDL_SCANCODE_KP_4 },
	{  2 + 7 * 1, 12 + 7 * 2, 6, 6, 4, "5", (int)SDL_SCANCODE_KP_5 },
	{  2 + 7 * 2, 12 + 7 * 2, 6, 6, 4, "6", (int)SDL_SCANCODE_KP_6 },
	{  2 + 7 * 3, 12 + 7 * 2, 6, 6, 4, "+", (int)SDL_SCANCODE_KP_PLUS },

	{  2 + 7 * 0, 12 + 7 * 3, 6, 6, 4, "1", (int)SDL_SCANCODE_KP_1 },
	{  2 + 7 * 1, 12 + 7 * 3, 6, 6, 4, "2", (int)SDL_SCANCODE_KP_2 },
	{  2 + 7 * 2, 12 + 7 * 3, 6, 6, 4, "3", (int)SDL_SCANCODE_KP_3 },
	{  2 + 7 * 3, 12 + 7 * 3, 6, 6, 4, "=", (int)SDL_SCANCODE_KP_EQUALS },

	{  2 + 7 * 0, 12 + 7 * 4, 6, 6, 4, "0", (int)SDL_SCANCODE_KP_0 },
	{  2 + 7 * 1, 12 + 7 * 4, 6, 6, 4, ",", (int)SDL_SCANCODE_KP_COMMA },
	{  2 + 7 * 2, 12 + 7 * 4, 6, 6, 4, ".", (int)SDL_SCANCODE_KP_PERIOD },
	{  2 + 7 * 3, 12 + 7 * 4, 6, 6, 1, "ENT", (int)SDL_SCANCODE_KP_ENTER },

	{ 0, 0, 0, 0, 1, "", -1 }
};

//
// software keyboard (action + move)
//
const softkey_param Input::softkey_actmove[14 + 1] = {
	{ 69, 1, 8, 6, 1, "MENU", 0 },
	{ 2, 1, 8, 6, 1, "NEXT", 1 },

	{ 50 + 7 * 1,  8 + 7 * 2, 6, 6, 1, "4""\xa5""8", (int)SDL_SCANCODE_KP_4 << 8 | (int)SDL_SCANCODE_KP_8 },
	{ 50 + 7 * 2,  8 + 7 * 2, 6, 6, 4, "8", (int)SDL_SCANCODE_KP_8 },
	{ 50 + 7 * 3,  8 + 7 * 2, 6, 6, 1, "6""\xa5""8", (int)SDL_SCANCODE_KP_6 << 8 | (int)SDL_SCANCODE_KP_8 },

	{ 50 + 7 * 1,  8 + 7 * 3, 6, 6, 4, "4", (int)SDL_SCANCODE_KP_4 },
	{ 50 + 7 * 3,  8 + 7 * 3, 6, 6, 4, "6", (int)SDL_SCANCODE_KP_6 },

	{ 50 + 7 * 1,  8 + 7 * 4, 6, 6, 1, "4""\xa5""2", (int)SDL_SCANCODE_KP_4 << 8 | (int)SDL_SCANCODE_KP_2 },
	{ 50 + 7 * 2,  8 + 7 * 4, 6, 6, 4, "2", (int)SDL_SCANCODE_KP_2 },
	{ 50 + 7 * 3,  8 + 7 * 4, 6, 6, 1, "6""\xa5""2", (int)SDL_SCANCODE_KP_6 << 8 | (int)SDL_SCANCODE_KP_2 },

	{  2 + 7 * 0, 12 + 7 * 1, 13, 6, 4, "ESC", (int)SDL_SCANCODE_ESCAPE },
	{  2 + 7 * 0, 12 + 7 * 4, 13, 6, 4, "RET", (int)SDL_SCANCODE_RETURN },
	{  2 + 7 * 0, 12 + 7 * 3, 13, 6, 4, "SFT", (int)SDL_SCANCODE_LSHIFT },
	{  2 + 7 * 0, 12 + 7 * 2, 13, 6, 4, "SPC", (int)SDL_SCANCODE_SPACE },
	
	{ 0, 0, 0, 0, 1, "", -1 }
};

//
// software keyboard (move + action)
//
const softkey_param Input::softkey_moveact[14 + 1] = {
	{ 69, 1, 8, 6, 1, "MENU", 0 },
	{ 2, 1, 8, 6, 1, "NEXT", 1 },

	{  2 + 7 * 0,  8 + 7 * 2, 6, 6, 1, "4""\xa5""8", (int)SDL_SCANCODE_KP_4 << 8 | (int)SDL_SCANCODE_KP_8 },
	{  2 + 7 * 1,  8 + 7 * 2, 6, 6, 4, "8", (int)SDL_SCANCODE_KP_8 },
	{  2 + 7 * 2,  8 + 7 * 2, 6, 6, 1, "6""\xa5""8", (int)SDL_SCANCODE_KP_6 << 8 | (int)SDL_SCANCODE_KP_8 },

	{  2 + 7 * 0,  8 + 7 * 3, 6, 6, 4, "4", (int)SDL_SCANCODE_KP_4 },
	{  2 + 7 * 2,  8 + 7 * 3, 6, 6, 4, "6", (int)SDL_SCANCODE_KP_6 },

	{  2 + 7 * 0,  8 + 7 * 4, 6, 6, 1, "4""\xa5""2", (int)SDL_SCANCODE_KP_4 << 8 | (int)SDL_SCANCODE_KP_2 },
	{  2 + 7 * 1,  8 + 7 * 4, 6, 6, 4, "2", (int)SDL_SCANCODE_KP_2 },
	{  2 + 7 * 2,  8 + 7 * 4, 6, 6, 1, "6""\xa5""2", (int)SDL_SCANCODE_KP_6 << 8 | (int)SDL_SCANCODE_KP_2 },

	{ 50 + 7 * 2, 12 + 7 * 1, 13, 6, 4, "ESC", (int)SDL_SCANCODE_ESCAPE },
	{ 50 + 7 * 2, 12 + 7 * 4, 13, 6, 4, "RET", (int)SDL_SCANCODE_RETURN },
	{ 50 + 7 * 2, 12 + 7 * 3, 13, 6, 4, "SFT", (int)SDL_SCANCODE_LSHIFT },
	{ 50 + 7 * 2, 12 + 7 * 2, 13, 6, 4, "SPC", (int)SDL_SCANCODE_SPACE },
	
	{ 0, 0, 0, 0, 1, "", -1 }
};


//
// software keyboard (zx + move)
//
const softkey_param Input::softkey_zxmove[14 + 1] = {
	{ 69, 1, 8, 6, 1, "MENU", 0 },
	{ 2, 1, 8, 6, 1, "NEXT", 1 },

	{ 50 + 7 * 1,  8 + 7 * 2, 6, 6, 1, "4""\xa5""8", (int)SDL_SCANCODE_KP_4 << 8 | (int)SDL_SCANCODE_KP_8 },
	{ 50 + 7 * 2,  8 + 7 * 2, 6, 6, 4, "8", (int)SDL_SCANCODE_KP_8 },
	{ 50 + 7 * 3,  8 + 7 * 2, 6, 6, 1, "6""\xa5""8", (int)SDL_SCANCODE_KP_6 << 8 | (int)SDL_SCANCODE_KP_8 },

	{ 50 + 7 * 1,  8 + 7 * 3, 6, 6, 4, "4", (int)SDL_SCANCODE_KP_4 },
	{ 50 + 7 * 3,  8 + 7 * 3, 6, 6, 4, "6", (int)SDL_SCANCODE_KP_6 },

	{ 50 + 7 * 1,  8 + 7 * 4, 6, 6, 1, "4""\xa5""2", (int)SDL_SCANCODE_KP_4 << 8 | (int)SDL_SCANCODE_KP_2 },
	{ 50 + 7 * 2,  8 + 7 * 4, 6, 6, 4, "2", (int)SDL_SCANCODE_KP_2 },
	{ 50 + 7 * 3,  8 + 7 * 4, 6, 6, 1, "6""\xa5""2", (int)SDL_SCANCODE_KP_6 << 8 | (int)SDL_SCANCODE_KP_2 },

	{  2 + 7 * 0, 12 + 7 * 1, 13, 6, 4, "ESC", (int)SDL_SCANCODE_ESCAPE },
	{  2 + 7 * 0, 12 + 7 * 4, 13, 6, 4, "Z", (int)SDL_SCANCODE_Z },
	{  2 + 7 * 0, 12 + 7 * 3, 13, 6, 4, "X", (int)SDL_SCANCODE_X },
	{  2 + 7 * 0, 12 + 7 * 2, 13, 6, 4, "SPC", (int)SDL_SCANCODE_SPACE },
	
	{ 0, 0, 0, 0, 1, "", -1 }
};

//
// software keyboard (move + zx)
//
const softkey_param Input::softkey_movezx[14 + 1] = {
	{ 69, 1, 8, 6, 1, "MENU", 0 },
	{ 2, 1, 8, 6, 1, "NEXT", 1 },

	{  2 + 7 * 0,  8 + 7 * 2, 6, 6, 1, "4""\xa5""8", (int)SDL_SCANCODE_KP_4 << 8 | (int)SDL_SCANCODE_KP_8 },
	{  2 + 7 * 1,  8 + 7 * 2, 6, 6, 4, "8", (int)SDL_SCANCODE_KP_8 },
	{  2 + 7 * 2,  8 + 7 * 2, 6, 6, 1, "6""\xa5""8", (int)SDL_SCANCODE_KP_6 << 8 | (int)SDL_SCANCODE_KP_8 },

	{  2 + 7 * 0,  8 + 7 * 3, 6, 6, 4, "4", (int)SDL_SCANCODE_KP_4 },	
	{  2 + 7 * 2,  8 + 7 * 3, 6, 6, 4, "6", (int)SDL_SCANCODE_KP_6 },

	{  2 + 7 * 0,  8 + 7 * 4, 6, 6, 1, "4""\xa5""2", (int)SDL_SCANCODE_KP_4 << 8 | (int)SDL_SCANCODE_KP_2 },
	{  2 + 7 * 1,  8 + 7 * 4, 6, 6, 4, "2", (int)SDL_SCANCODE_KP_2 },
	{  2 + 7 * 2,  8 + 7 * 4, 6, 6, 1, "6""\xa5""2", (int)SDL_SCANCODE_KP_6 << 8 | (int)SDL_SCANCODE_KP_2 },

	{ 50 + 7 * 2, 12 + 7 * 1, 13, 6, 4, "ESC", (int)SDL_SCANCODE_ESCAPE },
	{ 50 + 7 * 2, 12 + 7 * 4, 13, 6, 4, "Z", (int)SDL_SCANCODE_Z },
	{ 50 + 7 * 2, 12 + 7 * 3, 13, 6, 4, "X", (int)SDL_SCANCODE_X },
	{ 50 + 7 * 2, 12 + 7 * 2, 13, 6, 4, "SPC", (int)SDL_SCANCODE_SPACE },
	
	{ 0, 0, 0, 0, 1, "", -1 }
};

//
// software keyboard (ten key left)
//
const softkey_param Input::softkey_tenleft[22 + 1] = {
	{ 69, 1, 8, 6, 1, "MENU", 0 },
	{ 2, 1, 8, 6, 1, "NEXT", 1 },

	{  2 + 7 * 0, 12 + 7 * 0, 6, 6, 1, "CLR", (int)SDL_SCANCODE_HOME },
	{  2 + 7 * 1, 12 + 7 * 0, 6, 6, 1, "HELP", (int)SDL_SCANCODE_END },
	{  2 + 7 * 2, 12 + 7 * 0, 6, 6, 4, "-", (int)SDL_SCANCODE_KP_MINUS },
	{  2 + 7 * 3, 12 + 7 * 0, 6, 6, 4, "/", (int)SDL_SCANCODE_KP_DIVIDE },

	{  2 + 7 * 0, 12 + 7 * 1, 6, 6, 4, "7", (int)SDL_SCANCODE_KP_7 },
	{  2 + 7 * 1, 12 + 7 * 1, 6, 6, 4, "8", (int)SDL_SCANCODE_KP_8 },
	{  2 + 7 * 2, 12 + 7 * 1, 6, 6, 4, "9", (int)SDL_SCANCODE_KP_9 },
	{  2 + 7 * 3, 12 + 7 * 1, 6, 6, 4, "*", (int)SDL_SCANCODE_KP_MULTIPLY },

	{  2 + 7 * 0, 12 + 7 * 2, 6, 6, 4, "4", (int)SDL_SCANCODE_KP_4 },
	{  2 + 7 * 1, 12 + 7 * 2, 6, 6, 4, "5", (int)SDL_SCANCODE_KP_5 },
	{  2 + 7 * 2, 12 + 7 * 2, 6, 6, 4, "6", (int)SDL_SCANCODE_KP_6 },
	{  2 + 7 * 3, 12 + 7 * 2, 6, 6, 4, "+", (int)SDL_SCANCODE_KP_PLUS },

	{  2 + 7 * 0, 12 + 7 * 3, 6, 6, 4, "1", (int)SDL_SCANCODE_KP_1 },
	{  2 + 7 * 1, 12 + 7 * 3, 6, 6, 4, "2", (int)SDL_SCANCODE_KP_2 },
	{  2 + 7 * 2, 12 + 7 * 3, 6, 6, 4, "3", (int)SDL_SCANCODE_KP_3 },
	{  2 + 7 * 3, 12 + 7 * 3, 6, 6, 4, "=", (int)SDL_SCANCODE_KP_EQUALS },

	{  2 + 7 * 0, 12 + 7 * 4, 6, 6, 4, "0", (int)SDL_SCANCODE_KP_0 },
	{  2 + 7 * 1, 12 + 7 * 4, 6, 6, 4, ",", (int)SDL_SCANCODE_KP_COMMA },
	{  2 + 7 * 2, 12 + 7 * 4, 6, 6, 4, ".", (int)SDL_SCANCODE_KP_PERIOD },
	{  2 + 7 * 3, 12 + 7 * 4, 6, 6, 1, "ENT", (int)SDL_SCANCODE_KP_ENTER },

	{ 0, 0, 0, 0, 1, "", -1 }
};

//
// software keyboard (ten key right)
//
const softkey_param Input::softkey_tenright[22 + 1] = {
	{ 69, 1, 8, 6, 1, "MENU", 0 },
	{ 2, 1, 8, 6, 1, "NEXT", 1 },

	{ 50 + 7 * 0, 12 + 7 * 0, 6, 6, 1, "CLR", (int)SDL_SCANCODE_HOME },
	{ 50 + 7 * 1, 12 + 7 * 0, 6, 6, 1, "HELP", (int)SDL_SCANCODE_END },
	{ 50 + 7 * 2, 12 + 7 * 0, 6, 6, 4, "-", (int)SDL_SCANCODE_KP_MINUS },
	{ 50 + 7 * 3, 12 + 7 * 0, 6, 6, 4, "/", (int)SDL_SCANCODE_KP_DIVIDE },

	{ 50 + 7 * 0, 12 + 7 * 1, 6, 6, 4, "7", (int)SDL_SCANCODE_KP_7 },
	{ 50 + 7 * 1, 12 + 7 * 1, 6, 6, 4, "8", (int)SDL_SCANCODE_KP_8 },
	{ 50 + 7 * 2, 12 + 7 * 1, 6, 6, 4, "9", (int)SDL_SCANCODE_KP_9 },
	{ 50 + 7 * 3, 12 + 7 * 1, 6, 6, 4, "*", (int)SDL_SCANCODE_KP_MULTIPLY },

	{ 50 + 7 * 0, 12 + 7 * 2, 6, 6, 4, "4", (int)SDL_SCANCODE_KP_4 },
	{ 50 + 7 * 1, 12 + 7 * 2, 6, 6, 4, "5", (int)SDL_SCANCODE_KP_5 },
	{ 50 + 7 * 2, 12 + 7 * 2, 6, 6, 4, "6", (int)SDL_SCANCODE_KP_6 },
	{ 50 + 7 * 3, 12 + 7 * 2, 6, 6, 4, "+", (int)SDL_SCANCODE_KP_PLUS },

	{ 50 + 7 * 0, 12 + 7 * 3, 6, 6, 4, "1", (int)SDL_SCANCODE_KP_1 },
	{ 50 + 7 * 1, 12 + 7 * 3, 6, 6, 4, "2", (int)SDL_SCANCODE_KP_2 },
	{ 50 + 7 * 2, 12 + 7 * 3, 6, 6, 4, "3", (int)SDL_SCANCODE_KP_3 },
	{ 50 + 7 * 3, 12 + 7 * 3, 6, 6, 4, "=", (int)SDL_SCANCODE_KP_EQUALS },

	{ 50 + 7 * 0, 12 + 7 * 4, 6, 6, 4, "0", (int)SDL_SCANCODE_KP_0 },
	{ 50 + 7 * 1, 12 + 7 * 4, 6, 6, 4, ",", (int)SDL_SCANCODE_KP_COMMA },
	{ 50 + 7 * 2, 12 + 7 * 4, 6, 6, 4, ".", (int)SDL_SCANCODE_KP_PERIOD },
	{ 50 + 7 * 3, 12 + 7 * 4, 6, 6, 1, "ENT", (int)SDL_SCANCODE_KP_ENTER },

	{ 0, 0, 0, 0, 1, "", -1 }
};

//
// software keyboard (none)
//
const softkey_param Input::softkey_none[2 + 1] = {
	{ 69, 1, 8, 6, 1, "MENU", 0 },
	{ 2, 1, 8, 6, 1, "NEXT", 1 },

	{ 0, 0, 0, 0, 1, "", -1 }
};

#endif // SDL

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

#ifndef INPUT_H
#define INPUT_H

#include "softkey.h"

//
// input driver
//
class Input
{
	friend class InputTestAccess;
public:
	Input(App *a);
										// constructor
	virtual ~Input();
										// destructor
	bool Init();
										// initialize
	void Deinit();
										// deinitialize

	// joystick
	void AddJoystick();
										// add joystick
	void DelJoystick();
										// delete joystick

	// softkey
	void AddList(const softkey_param *param);
										// add softkey list
	void DelList();
										// delete softkey list
	void DrawList(bool force);
										// draw softkey list
	void ProcessList();
										// process softkey list
	void ShiftList();
										// update softkey text with shift state
	void ResetList();
										// reset softkey list
	void NextList();
										// next softkey list
	void RebuildList();
										// rebuild softkey list
	void ChangeList(bool next, bool prev);
										// next or prev softkey list
	void DelayedBreak();
										// break the key after minimum time

	// action
	bool GetKeyStatus(SDL_Scancode code);
										// get key status
	void GetJoystick(Uint32 *status);
										// get joystick status
	void LostFocus();
										// lost window focus

	// event
	void OnKeyDown(bool soft, SDL_Scancode code);
										// key down
	void OnKeyUp(bool soft, SDL_Scancode code);
										// key up
	void OnMouseMotion(SDL_Event *e);
										// mouse motion
	void OnMouseButtonDown(SDL_Event *e);
										// mouse button down
	void OnMouseButtonUp(SDL_Event *e);
										// mouse button up
	void OnMouseWheel(SDL_Event *e);
										// mouse wheel
	void OnJoystick();
										// joystick axis and button
	void OnFingerDown(SDL_Event *e);
										// finger down
	void OnFingerUp(SDL_Event *e);
										// finger up
	void OnFingerMotion(SDL_Event *e);
										// finger motion
	void OnInputMove(SoftKey *key, int finger);
										// intput move from OnInputCommon()
	void ChangeCursorToNumPad(bool enable);
										// change cursor to num pad
	void ChangeNumToNumPad(bool enable);
										// change num to num pad

private:
	void OnInputCommon(int x, int y, bool button, int finger);
										// input common
	Uint32 GetKeyShift();
										// get shift bits
	void SetKeyStatus();
										// set key status to EMU
	void OnJoyKeyDown(int button);
										// joystick button down (keyboard emulation)
	void OnJoyKeyUp(int button);
										// joystick button up (keyboard emulation)
	App *app;
										// application
	Setting *setting;
										// setting
	Platform *platform;
										// platform
	Video *video;
										// video
	Font *font;
										// font
	EMU *emu;
										// emulator i/f
	SoftKey *key_list;
										// softkey list
	bool key_disp;
										// softkey display flag
	bool key_next;
										// softkey next flag
	bool key_rebuild;
										// softkey rebuild flag
	Uint32 key_tick;
										// softkey last tick
	Uint32 key_shift;
										// softkey shift state
	Uint32 menu_delay;
										// menu leave tick
	SDL_Joystick **joystick;
										// joystick pointer buffer
	int joystick_num;
										// joystick num
	Uint32 joystick_prev;
										// joystick previous state
	bool key_hard_pressed[0x120];
	bool key_soft_pressed[0x120];
	Uint32 joystick_key_code[15];
	Uint8 key_status[0x100];
										// key status for EMU::key_status
	Uint8 key_buf[0x100];
										// key buffer from PC88::key_status
	Uint32 key_table[0x120];
										// key map (current)
	Uint32 key_soft_make_tick[0x120];
										// key make tick (from softkey only)
	bool key_soft_break_flag[0x120];
										// key break flag (from softkey only)
	static const Uint32 key_base[0x120];
										// key map (base)
	static const Uint32 joystick_button[15 * 2];
										// joystick button bit map
	static const softkey_param softkey_full[66 + 1];
										// softkey (full key)
	static const softkey_param softkey_curten[31 + 1];
										// softkey (cursor key + ten key);
	static const softkey_param softkey_tencur[31 + 1];
										// softkey (ten key + cursor key);
	static const softkey_param softkey_functen[35 + 1];
										// softkey (tunction key + ten key);
	static const softkey_param softkey_tenfunc[35 + 1];
										// softkey (ten key + function key);
	static const softkey_param softkey_actmove[14 + 1];
										// softkey (action + move)
	static const softkey_param softkey_moveact[14 + 1];
										// softkey (move + action)
	static const softkey_param softkey_zxmove[14 + 1];
										// softkey (zx + move)
	static const softkey_param softkey_movezx[14 + 1];
										// softkey (move + zx)
	static const softkey_param softkey_tenleft[22 + 1];
										// softkey (ten key left);
	static const softkey_param softkey_tenright[22 + 1];
										// softkey (ten key left);
	static const softkey_param softkey_none[2 + 1];
										// softkey (none);
};

#endif // INPUT_H

#endif // SDL

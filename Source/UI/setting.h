//
// eXcellent Multi-platform emulator type 8 - 'XM8'
// based on ePC-8801MA
//
// Author (ePC-8801MA) : Takeda.Toshiya
// Author (XM8) : Tanaka.Yasushi
//
// [ setting ]
//

#ifdef SDL

#ifndef SETTING_H
#define SETTING_H

//
// defines
//
#define SETTING_V1S_MODE		0
										// BOOT MODE (V1S)
#define SETTING_V1H_MODE		1
										// BOOT MODE (V1H)
#define SETTING_V2_MODE			2
										// BOOT MODE (V2)
#define SETTING_N_MODE			3
										// BOOT MODE (N)
#define DIP_MEM_WAIT			0x00000001
										// memory wait (on)
#define DIP_CLOCK_8MHZH			0x00000002
										// 8MHzH (high)
#define DIP_DISABLE_EXRAM		0x00000004
										// disable EXRAM (disable)
#define DIP_TERMINAL_MODE		0x00000008
										// terminal mode (terminal)
#define DIP_WIDTH_40			0x00000010
										// width 40 (40)
#define DIP_LINE_25				0x00000020
										// line 20 (20)
#define DIP_BOOT_ROM			0x00000040
										// boot from ROM (ROM)
#define DIP_BAUDRATE			0x00000780
										// baudrate (0-6:1200bps 7:75bps 15:19200bps)
#define DIP_BAUDRATE_SHIFT		7
										// baudrate shift
#define DIP_HALFDUPLEX			0x00000800
										// duplex (half)
#define DIP_DATA7BIT			0x00001000
										// data bit (7bit)
#define DIP_STOP2BIT			0x00002000
										// stop bit (2bit)
#define DIP_DISABLE_X			0x00004000
										// X parameter (disable)
#define DIP_ENABLE_S			0x00008000
										// S parameter (enable)
#define DIP_DISABLE_DEL			0x00010000
										// DEL code (disable)
#define DIP_PARITY				0x00060000
										// parity (0:none 2:odd)
#define DIP_PARITY_SHIFT		17
										// parity shift
#define DIP_MAX					0x00ffffff
										// max settings - see Settings::GetSystems()

//
// platform dependent
//
class Setting
{
	friend class AppStateTestAccess;
public:
	enum RotationMode {
		RotationAuto = 0,
		RotationLandscape,
		RotationPortrait
	};
	Setting();
										// constructor
	virtual ~Setting();
										// destructor
	bool Init();
										// initialize
	void Deinit();
										// deinitialize
	bool LoadSetting(FILEIO *fio);
										// load
	void SaveSetting(FILEIO *fio);
										// save
	const char* GetSettingDir();
										// get setting directory

	// system
	int GetSystemMode();
										// get system mode (0-3)
	void SetSystemMode(int mode);
										// set system mode (0-3)
	int GetCPUClock();
										// get cpu clock (4 or 8)
	void SetCPUClock(int clock);
										// set cpu clock (4 or 8)
	bool Is8HMode();
										// get 8MHzH mode
	void Set8HMode(bool high);
										// set 8MHzH mode
	bool HasExRAM();
										// get extended RAM
	void SetExRAM(bool enable);
										// set extended RAM
	Uint32 GetDip();
										// get dip switch
	void SetDip(uint32 dip);
										// set dip switch
	Uint32 GetSystems();
										// get system information
	bool IsFastDisk();
										// get pseudo fast disk mode
	void SetFastDisk(bool enable);
										// set pseudo fast disk mode

	// video
	int GetWindowWidth();
										// get window width
	void SetWindowWidth(int width);
										// set window width
	int GetSkipFrame();
										// get skip frame
	void SetSkipFrame(int frame);
										// set skip frame
	Uint8 GetBrightness();
										// get brightness for vm
	void SetBrightness(Uint8 bri);
										// set brightness for vm
	Uint8 GetMenuAlpha();
										// get alpha blending level for menu
	bool IsLowReso();
										// get 15kHz monitor
	void SetLowReso(bool low);
										// set 15kHz monitor
	bool HasScanline();
										// get scan line
	void SetScanline(bool scanline);
										// set scan line
	bool HasStatusLine();
										// get status line
	void SetStatusLine(bool enable);
										// get status line
	Uint8 GetStatusAlpha();
										// get alpha blending level for status
	void SetStatusAlpha(Uint8 alpha);
										// set alpha blending level for status
	const char* GetScaleQuality();
										// get render scale quality
	void SetScaleQuality(int quality);
										// set render scale quality
	bool IsImageInterpolation();
										// get image interpolation
	bool IsForceRGB565();
										// get RGB565 mode (Android only)
	void SetForceRGB565(bool enable);
										// set RGB565 mode (Android only)
	int GetRotationMode();
										// get Android rotation mode
	void SetRotationMode(int mode);
										// set Android rotation mode

	// audio
	int GetAudioDevice();
										// get audio device
	void SetAudioDevice(int id);
										// set audio device
	int GetAudioFreq();
										// get audio frequency
	void SetAudioFreq(int freq);
										// set audio frequency
	int GetAudioPower();
										// get audio samples (for SDL_OpenAudioDevice)
	int GetAudioUnit();
										// get audio samples (for vm->create_sound)
	int GetAudioBuffer();
										// get audio buffer (ms)
	void SetAudioBuffer(int ms);
										// set audio buffer (ms)
	bool HasOPNA();
										// get sound board II
	void SetOPNA(bool opna);
										// set sound board II

	// input
	int GetSoftKeyType();
										// get softkey type
	bool NextSoftKey();
										// next softkey
	bool PrevSoftKey();
										// prev softkey
	int GetSoftKeySet(int set);
										// get softkey set
	bool SetSoftKeySet(int set, int type);
										// set softkey set
	Uint8 GetSoftKeyAlpha();
										// get alpha blending level for softkey
	void SetSoftKeyAlpha(Uint8 alpha);
										// set alpha blending level for softkey
	Uint32 GetSoftKeyTime();
										// get softkey timeout
	void SetSoftKeyTime(Uint32 ms);
										// set softkey timeout
	bool IsJoyEnable();
										// get joystick enable
	void SetJoyEnable(bool enable);
										// set joystick enable
	bool IsJoySwap();
										// get joystick button swap
	void SetJoySwap(bool swap);
										// set joystick button swap
	bool IsJoyKey();
										// get joystick to keyboard
	void SetJoyKey(bool enable);
										// set joystick to keyboard
	Uint32 GetMouseTime();
										// get mouse timeout
	void SetMouseTime(Uint32 ms);
										// set mouse timeout
	bool IsKeyEnable();
										// get keyboard enable
	void SetKeyEnable(bool enable);
										// set keyboard enable
	Uint32 GetJoystickToKey(int button);
										// get joystick to keyboard map
	void SetJoystickToKey(int button, Uint32 data);
										// set joystick to keyboard map
	void DefJoystickToKey();
										// default joystick to keyboard map

	// state
	int GetStateNum();
										// get state number
	void SetStateNum(int num);
										// set state number

	// power
	bool IsWatchBattery();
										// get watch battery
	void SetWatchBattery(bool enable);
										// set watch battery
	
	bool IsShowSoftKeyMouseMove();
										// get show softkey when mouse move
	void SetShowSoftKeyMouseMove(bool);
										// set show softkey when mouse move
	bool IsCursorToNumPad();
										// get cursor to num pad
	void SetCursorToNumPad(bool);
										// set cursor to num pad
	bool IsNumToNumPad();
										// get num to num pad
	void SetNumToNumPad(bool);
										// set num to num pad
	bool Is400Line();
										// is 400 line

private:
	bool LoadSettingData(FILEIO *fio);
	void Load();
										// load setting
	void Save();
										// save setting
	char setting_dir[_MAX_PATH * 3];
										// setting directory
	char setting_path[_MAX_PATH * 3];
										// setting path
	int window_width;
										// window width
	int skip_frame;
										// skip frame
	Uint8 brightness;
										// brightness
	int softkey_index;
										// softkey type
	int softkey_set[4];
										// softkey set
	Uint8 softkey_alpha;
										// softkey alpha
	Uint32 softkey_time;
										// softkey timeout
	bool joystick_enable;
										// joystick enable
	bool joystick_swap;
										// joystick button swap
	bool joystick_key;
										// joystick to keyboard
	Uint32 mouse_time;
										// mouse timeout
	int state_num;
										// state number
	bool status_line;
										// status line (version 1.05)
	Uint8 status_alpha;
										// status alpha (version 1.05)
	char scale_quality[2];
										// render scale quality (version 1.05)
	bool keyboard_enable;
										// keyboard enable (version 1.10)
	bool force_rgb565;
										// force RGB565 (version 1.20)
	int rotation_mode;
										// Android rotation mode (version 2.00)
	bool watch_battery;
										// watch battery (version 1.30)
	uint32 joystick_to_key[15];
										// joystick to keyboard map (version 1.50)
	int audio_output_device;
										// audio output device (version 1.71)
	bool mouse_move_softkey_enable;
										// show softkey mouse move enable (version 1.71)
	bool cursor_to_numpad_enable;
										// cursor to num pad enable (version 1.73)
	bool num_to_numpad_enable;
										// num to num pad enable (version 1.73)
};

#endif // SETTING_H

#endif // SDL

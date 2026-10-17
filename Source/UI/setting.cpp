//
// eXcellent Multi-Platform emulator type 8 - 'XM8'
// based on ePC-8801MA
//
// Author (ePC-8801MA) : Takeda.Toshiya
// Author (XM8) : Tanaka.Yasushi
//
// [ setting ]
//

#ifdef SDL

#include "os.h"
#include "common.h"
#include "fileio.h"
#include "vm.h"
#include "config.h"
#include "setting.h"

//
// defines
//
#define SETTING_CPU_8MHZ		0
										// uPD70008AC CPU (Z80H compatible, clock 8MHz)
#define SETTING_CPU_4MHZ		1
										// uPD780C-1 CPU (Z80A compatible, clock 4MHz)
#define SETTING_USE_JOYSTICK	0
										// Joystick for ATARI port
#define SETTING_MONITOR_24K		0
										// 24KHz analog monitor
#define SETTING_MONITOR_15K		1
										// 15kHz analog monitor
#define SETTING_SOUND_OPNA		0
										// YM2608 (PC-8801FA/MA/MA2/MC/VA2/VA3/DO+)
#define SETTING_SOUND_OPN		1
										// YM2203 (PC-8801mkIISR/TR/FR/MR/FH/MH/FE/FE2/VA/DO)
#define SETTING_ORG				"retro_pc_pi"
										// organization name
#define SETTING_APP				"xm8"
										// application name
#define SETTING_FILENAME		"setting.bin"
										// setting file name

// version
#define SETTING_VERSION_100		20150621
										// version 1.00
#define SETTING_VERSION_105		20150624
										// version 1.05
#define SETTING_VERSION_110		20150626
										// version 1.10
#define SETTING_VERSION_120		20150710
										// version 1.20
#define SETTING_VERSION_130		20150822
										// version 1.30
#define SETTING_VERSION_150		20151020
										// version 1.50
#define SETTING_VERSION_170		20180120
										// version 1.70
#define SETTING_VERSION_171		20230401
										// version 1.71
#define SETTING_VERSION_173		20230501
										// version 1.73
#define SETTING_VERSION_200		20260801
										// version 2.00

// video
#define DEFAULT_WINDOW_WIDTH	640
										// window width
#define DEFAULT_SKIP_FRAME		0
										// skip frame
#define DEFAULT_SCAN_LINE		(true)
										// scan line
#define DEFAULT_BRIGHTNESS		0xFF
										// brightness for vm
#define DEFAULT_MENU_ALPHA		0xD0
										// alpha blending level for menu
#define DEFAULT_STATUS_LINE		(true)
										// status line
#define DEFAULT_STATUS_ALPHA	0x60
										// alpha blending level for status
#define DEFAULT_SCALE_QUALITY	2
										// render scale quality
#define DEFAULT_FORCE_RGB565	(true)
										// force RGB565

// audio
#ifdef __ANDROID__
#define DEFAULT_AUDIO_FREQ		48000
										// audio freqency (Hz)
#else
#define DEFAULT_AUDIO_FREQ		55467
										// audio freqency (Hz)
#endif // __ANDROID__
#define DEFAULT_AUDIO_POWER		11
										// audio power (2^n)
#define DEFAULT_AUDIO_UNIT		15
										// audio unit (ms)
#ifdef __ANDROID__
#define DEFAULT_AUDIO_BUFFER	140
										// audio buffer (ms)
#else
#define DEFAULT_AUDIO_BUFFER	120
										// audio buffer (ms)
#endif // __ANDROID__

#define DEFAULT_AUDIO_OUTPUT_SYSTEM_DEFAULT	0
										// audio output device

// input
#define DEFAULT_SOFTKEY_ALPHA	0x60
										// alpha blending level for softkey
#define DEFAULT_SOFTKEY_TIME	4000
										// softkey timeout (ms)
#define DEFAULT_MOUSE_TIME		3000
										// mouse timeout (ms)
#ifdef __ANDROID__
#define DEFAULT_KEYBOARD_ENABLE	(false)
										// keyboard enable
#else
#define DEFAULT_KEYBOARD_ENABLE	(true)
										// keyboard enable
#endif // __ANDROID__

#define DEFAULT_SHOW_SOFTKEY_MOUSE_MOVE	(false)
										// show softkey when mouse move
#define DEFAULT_CURSOR_NUMPAD			(false)
										// cursor to num pad
#define DEFAULT_NUM_TO_NUMPAD			(false)
										// num to num pad

namespace {

// Read settings without FILEIO's scalar-read assertions on a short record.
// Boolean bytes are checked before constructing a C++ bool value.
class SettingReader {
public:
	explicit SettingReader(FILEIO *file) : file(file), valid(true) {}

	template<typename T> T Read()
	{
		T value = {};
		if (valid && file->Fread(&value, sizeof(value), 1) != 1) {
			valid = false;
		}
		return value;
	}

	bool ReadBool()
	{
		const Uint8 value = Read<Uint8>();
		if (value > 1) valid = false;
		return value == 1;
	}

	bool Good() const { return valid && !file->HasError(); }

private:
	FILEIO *file;
	bool valid;
};

} // namespace

//
// Setting()
// constructor
//
Setting::Setting()
{
	int loop;

	// virtual machine
	SDL_zero(config);

	// directory and path
	setting_dir[0] = '\0';
	setting_path[0] = '\0';

	// setting (video)
	window_width = DEFAULT_WINDOW_WIDTH;
	skip_frame = DEFAULT_SKIP_FRAME;
	brightness = DEFAULT_BRIGHTNESS;
	status_line = DEFAULT_STATUS_LINE;
	status_alpha = DEFAULT_STATUS_ALPHA;
	scale_quality[0] = (char)('0' + DEFAULT_SCALE_QUALITY);
	scale_quality[1] = '\0';
	force_rgb565 = DEFAULT_FORCE_RGB565;
	rotation_mode = RotationAuto;

	// setting (input)
	softkey_index = 0;
	for (loop=0; loop<SDL_arraysize(softkey_set); loop++) {
		softkey_set[loop] = loop;
	}
	softkey_alpha = DEFAULT_SOFTKEY_ALPHA;
	softkey_time = DEFAULT_SOFTKEY_TIME;
	joystick_enable = true;
	joystick_swap = false;
	joystick_key = true;
	mouse_time = DEFAULT_MOUSE_TIME;
	keyboard_enable = DEFAULT_KEYBOARD_ENABLE;
	
	audio_output_device = DEFAULT_AUDIO_OUTPUT_SYSTEM_DEFAULT;
	mouse_move_softkey_enable = DEFAULT_SHOW_SOFTKEY_MOUSE_MOVE;
	cursor_to_numpad_enable = DEFAULT_CURSOR_NUMPAD;
	num_to_numpad_enable = DEFAULT_NUM_TO_NUMPAD;

	// setting (power)
	watch_battery = true;

	// setting (joystick to keyboard map)
	DefJoystickToKey();

	// save state
	state_num = 0;
}

//
// ~Setting()
// destructor
//
Setting::~Setting()
{
}

//
// Init()
// initialize
//
bool Setting::Init()
{
	// directory
#ifdef __ANDROID__
	const char* storage_path = SDL_AndroidGetExternalStoragePath();
	if (storage_path == NULL)
	{
		storage_path = SDL_AndroidGetInternalStoragePath();
	}
	if ((storage_path == NULL) || ((strlen(storage_path) + 2) > sizeof(setting_dir)))
	{
		return false;
	}
	strcpy(setting_dir, storage_path);
	strcat(setting_dir, "/");
#else
	strcpy(setting_dir, SDL_GetPrefPath(SETTING_ORG, SETTING_APP));
#endif // __ANDROID__
	strcpy(setting_path, setting_dir);
	strcat(setting_path, SETTING_FILENAME);

	// default settings (misc)
	config.use_direct_input = false;
	config.disable_dwm = false;

	// default settings (system)
	config.boot_mode = SETTING_V2_MODE;
	config.cpu_type = SETTING_CPU_4MHZ;
	config.dipswitch = DIP_LINE_25;
	config.device_type = SETTING_USE_JOYSTICK;

	// defult settings (floppy disk)
	config.ignore_crc = false;

	// default settings (tape)
	config.tape_sound = false;
	config.wave_shaper = false;
	config.direct_load_mzt = false;
	config.baud_high = false;

	// default settings (directory and path)
	config.initial_disk_dir[0] = '\0';
	memset(config.recent_disk_path, 0, sizeof(config.recent_disk_path));
	config.initial_tape_dir[0] = '\0';
	memset(config.recent_tape_path, 0, sizeof(config.recent_tape_path));

	// default settings (screen)
	config.window_mode = 0;
	config.use_d3d9 = false;
	config.wait_vsync = false;
	config.stretch_type = 0;
	config.monitor_type = SETTING_MONITOR_24K;
	config.crt_filter = false;
	config.scan_line = DEFAULT_SCAN_LINE;

	// default settings (sound)
	config.sound_frequency = DEFAULT_AUDIO_FREQ;
	config.sound_latency = DEFAULT_AUDIO_BUFFER;
	config.sound_device_type = SETTING_SOUND_OPN;
	config.fmgen_dll_path[0] = '\0';

	// load
	Load();
	return true;
}

//
// Deinit()
// deinitialize
//
void Setting::Deinit()
{
	Save();
}

//
// Load()
// load setting
//
void Setting::Load()
{
	FILEIO fileio;

	// open
	if (fileio.Fopen(setting_path, FILEIO_READ_BINARY) == true) {
		// common
		LoadSetting(&fileio);

		// close
		fileio.Fclose();
	}
}

//
// LoadSetting()
// load common
//
bool Setting::LoadSetting(FILEIO *fio)
{
	if (fio == NULL || !fio->IsOpened() || fio->HasError()) return false;

	// Both the UI object and the shared VM configuration must commit together.
	Setting candidate = *this;
	const config_t previous_config = config;
	if (!candidate.LoadSettingData(fio)) {
		config = previous_config;
		return false;
	}
	*this = candidate;
	return true;
}

bool Setting::LoadSettingData(FILEIO *fio)
{
	SettingReader reader(fio);
	Uint32 version;
	int loop;

	// check version
	version = reader.Read<Uint32>();
	if (version >= SETTING_VERSION_100 && version <= SETTING_VERSION_200) {
		// system
		config.boot_mode = reader.Read<int32>();
		config.cpu_type = reader.Read<int32>();
		config.dipswitch = reader.Read<Uint32>();

		// video
		window_width = reader.Read<int32>();
		skip_frame = reader.Read<int32>();
		config.monitor_type = reader.Read<int32>();
		config.scan_line = reader.ReadBool();
		brightness = reader.Read<Uint8>();

		// audio
		config.sound_frequency = reader.Read<int32>();
		config.sound_latency = reader.Read<int32>();
		config.sound_device_type = reader.Read<int32>();

		// input
		softkey_index = reader.Read<int32>();
		for (loop=0; loop<SDL_arraysize(softkey_set); loop++) {
			softkey_set[loop] = reader.Read<int32>();
		}
		softkey_alpha = reader.Read<Uint8>();
		softkey_time = reader.Read<Uint32>();
		joystick_enable = reader.ReadBool();
		joystick_swap = reader.ReadBool();
		joystick_key = reader.ReadBool();
		mouse_time = reader.Read<Uint32>();

		// load and save state
		state_num = reader.Read<int32>();

		// version 1.05
		if (version >= SETTING_VERSION_105) {
			status_line = reader.ReadBool();
			status_alpha = reader.Read<Uint8>();
			const int quality = reader.Read<int32>();
			if (quality < 0 || quality > 3) return false;
			scale_quality[0] = (char)('0' + quality);

#ifndef _WIN32
			// version 1.70
			if (scale_quality[0] == '2') {
				scale_quality[0] = '1';
			}
#endif // !_WIN32
		}

		// version 1.10
		if (version >= SETTING_VERSION_110) {
			keyboard_enable = reader.ReadBool();
		}

		// version 1.20
		if (version >= SETTING_VERSION_120) {
			config.ignore_crc = reader.ReadBool();
			force_rgb565 = reader.ReadBool();
		}

		// version 1.30
		if (version >= SETTING_VERSION_130) {
			watch_battery = reader.ReadBool();
		}

		// version 1.50
		if (version >= SETTING_VERSION_150) {
			for (loop=0; loop<SDL_arraysize(joystick_to_key); loop++) {
				joystick_to_key[loop] = reader.Read<Uint32>();
			}
		}

#ifdef __ANDROID__
		// version 1.00-1.61 to version 1.70
		if (version < SETTING_VERSION_170) {
			brightness = DEFAULT_BRIGHTNESS;
			config.scan_line = DEFAULT_SCAN_LINE;
		}
#endif // __ANDROID__

		// version 1.71
		if (version >= SETTING_VERSION_171) {
			audio_output_device = reader.Read<int32>();
			mouse_move_softkey_enable = reader.ReadBool();
		}

		// version 1.73
		if (version >= SETTING_VERSION_173) {
			cursor_to_numpad_enable = reader.ReadBool();
			num_to_numpad_enable = reader.ReadBool();
		}

		// version 2.00
		if (version >= SETTING_VERSION_200) {
			rotation_mode = reader.Read<int32>();
			if (rotation_mode < RotationAuto || rotation_mode > RotationPortrait) {
				rotation_mode = RotationAuto;
			}
		}

		if (!reader.Good() || config.boot_mode < SETTING_V1S_MODE ||
			config.boot_mode > SETTING_N_MODE || config.cpu_type < SETTING_CPU_8MHZ ||
			config.cpu_type > SETTING_CPU_4MHZ || config.monitor_type < SETTING_MONITOR_24K ||
			config.monitor_type > SETTING_MONITOR_15K || config.sound_device_type < SETTING_SOUND_OPNA ||
			config.sound_device_type > SETTING_SOUND_OPN || softkey_index < 0 ||
			softkey_index >= (int)SDL_arraysize(softkey_set) || window_width <= 0 ||
			skip_frame < 0 || skip_frame > 3 || config.sound_frequency <= 0 ||
			config.sound_latency <= 0 || state_num < 0 || state_num > 9 ||
			audio_output_device < 0) {
			return false;
		}
		for (loop = 0; loop < SDL_arraysize(softkey_set); ++loop) {
			if (softkey_set[loop] < 0 || softkey_set[loop] > 10) return false;
		}
		return true;
	}

	return false;
}

//
// Save()
// save setting
//
void Setting::Save()
{
	FILEIO fileio;

	// open
	if (fileio.Fopen(setting_path, FILEIO_WRITE_BINARY) == true) {
		// common
		SaveSetting(&fileio);

		// close
		fileio.Fclose();
	}
}

//
// SaveSetting()
// save common
//
void Setting::SaveSetting(FILEIO *fio)
{
	int loop;

	// version
	fio->FputUint32(SETTING_VERSION_200);

	// system
	fio->FputInt32(config.boot_mode);
	fio->FputInt32(config.cpu_type);
	fio->FputUint32(config.dipswitch);

	// video
	fio->FputInt32(window_width);
	fio->FputInt32(skip_frame);
	fio->FputInt32(config.monitor_type);
	fio->FputBool(config.scan_line);
	fio->FputUint8(brightness);

	// audio
	fio->FputInt32(config.sound_frequency);
	fio->FputInt32(config.sound_latency);
	fio->FputInt32(config.sound_device_type);

	// input
	fio->FputInt32(softkey_index);
	for (loop=0; loop<SDL_arraysize(softkey_set); loop++) {
		fio->FputInt32(softkey_set[loop]);
	}
	fio->FputUint8(softkey_alpha);
	fio->FputUint32(softkey_time);
	fio->FputBool(joystick_enable);
	fio->FputBool(joystick_swap);
	fio->FputBool(joystick_key);
	fio->FputUint32(mouse_time);

	// load and save state
	fio->FputInt32(state_num);

	// version 1.05
	fio->FputBool(status_line);
	fio->FputUint8(status_alpha);
	fio->FputInt32((int)(scale_quality[0] - '0'));

	// version 1.10
	fio->FputBool(keyboard_enable);

	// version 1.20
	fio->FputBool(config.ignore_crc);
	fio->FputBool(force_rgb565);

	// verison 1.30
	fio->FputBool(watch_battery);

	// version 1.50
	for (loop=0; loop<SDL_arraysize(joystick_to_key); loop++) {
		fio->FputUint32(joystick_to_key[loop]);
	}

	// version 1.71
	fio->FputInt32(audio_output_device);
	fio->FputBool(mouse_move_softkey_enable);

	// version 1.73
	fio->FputBool(cursor_to_numpad_enable);
	fio->FputBool(num_to_numpad_enable);

	// version 2.00
	fio->FputInt32(rotation_mode);
}

//
// GetSettingDir()
// get setting directory
//
const char* Setting::GetSettingDir()
{
	return setting_dir;
}

//
// GetSystemMode()
// get system mode (0-3)
//
int Setting::GetSystemMode()
{
	return config.boot_mode;
}

//
// SetSystemMode()
// set system mode (0-3)
//
void Setting::SetSystemMode(int mode)
{
	config.boot_mode = mode;
}

//
// GetCPUClock()
// get cpu clock (4 or 8)
//
int Setting::GetCPUClock()
{
	if (config.cpu_type == SETTING_CPU_8MHZ) {
		return 8;
	}
	else {
		return 4;
	}
}

//
// SetCPUClock()
// set cpu clock (4 or 8)
//
void Setting::SetCPUClock(int clock)
{
	switch (clock) {
	// 4MHz
	case 4:
		config.cpu_type = SETTING_CPU_4MHZ;
		break;

	// 8MHz
	case 8:
		config.cpu_type = SETTING_CPU_8MHZ;
		break;

	default:
		break;
	}
}

//
// Is8HMode()
// get 8MHzH mode
//
bool Setting::Is8HMode()
{
	if ((config.dipswitch & DIP_CLOCK_8MHZH) != 0) {
		return true;
	}
	else {
		return false;
	}
}

//
// Set8HMode()
// set 8MHzH mode
//
void Setting::Set8HMode(bool high)
{
	if (high == true) {
		config.dipswitch |= DIP_CLOCK_8MHZH;
	}
	else {
		config.dipswitch &= ~DIP_CLOCK_8MHZH;
	}
}

//
// HasExRAM()
// get extended RAM
//
bool Setting::HasExRAM()
{
	if ((config.dipswitch & DIP_DISABLE_EXRAM) != 0) {
		return false;
	}
	else {
		return true;
	}
}

//
// SetExRAM()
// set extended RAM
//
void Setting::SetExRAM(bool enable)
{
	if (enable == true) {
		config.dipswitch &= ~DIP_DISABLE_EXRAM;
	}
	else {
		config.dipswitch |= DIP_DISABLE_EXRAM;
	}
}

//
// GetDip()
// get dip switch
//
Uint32 Setting::GetDip()
{
	return (Uint32)config.dipswitch;
}

//
// SetDip()
// set dip switch
//
void Setting::SetDip(Uint32 dip)
{
	config.dipswitch = (uint32)dip;
}

//
// GetSystems()
// get system information
//
Uint32 Setting::GetSystems()
{
	Uint32 info;

	info = (Uint32)config.dipswitch;
	info <<= 4;
	info |= (Uint32)config.cpu_type;
	info <<= 4;
	info |= (Uint32)config.boot_mode;

	return info;
}

//
// IsFastDisk()
// get pseudo fast disk mode
//
bool Setting::IsFastDisk()
{
	return config.ignore_crc;
}

//
// SetFastDisk()
// set pseudo fast disk mode
//
void Setting::SetFastDisk(bool enable)
{
	config.ignore_crc = enable;
}

//
// GetWindowWidth()
// get window width
//
int Setting::GetWindowWidth()
{
	return window_width;
}

//
// SetWindowWidth()
// set window width
//
void Setting::SetWindowWidth(int width)
{
	window_width = width;
}

//
// GetRotationMode()
// get Android rotation mode
//
int Setting::GetRotationMode()
{
	return rotation_mode;
}

//
// SetRotationMode()
// set Android rotation mode
//
void Setting::SetRotationMode(int mode)
{
	if (mode < RotationAuto || mode > RotationPortrait) {
		mode = RotationAuto;
	}
	rotation_mode = mode;
}

//
// GetSkipFrame()
// get skip frame
//
int Setting::GetSkipFrame()
{
	return skip_frame;
}

//
// SetSkipFrame()
// set skip frame
//
void Setting::SetSkipFrame(int frame)
{
	skip_frame = frame;
}

//
// GetBrightness()
// get brightness for vm
//
Uint8 Setting::GetBrightness()
{
	return brightness;
}

//
// SetBrightness()
// set brightness for vm
//
void Setting::SetBrightness(Uint8 bri)
{
	brightness = bri;
}

//
// GetMenuAlpha
// get alpha blending level for menu
//
Uint8 Setting::GetMenuAlpha()
{
	return DEFAULT_MENU_ALPHA;
}

//
// IsLowReso()
// get 15kHz monitor
//
bool Setting::IsLowReso()
{
	if (config.monitor_type == SETTING_MONITOR_15K) {
		return true;
	}
	else {
		return false;
	}
}

//
// SetLowReso()
// set 15KHz monitor
//
void Setting::SetLowReso(bool low)
{
	if (low == true) {
		config.monitor_type = SETTING_MONITOR_15K;
	}
	else {
		config.monitor_type = SETTING_MONITOR_24K;
	}
}

//
// HasScanline()
// get scan line
//
bool Setting::HasScanline()
{
	return config.scan_line;
}

//
// SetScanline()
// set scan line
//
void Setting::SetScanline(bool scanline)
{
	config.scan_line = scanline;
}

//
// HasStatusLine()
// get status line
//
bool Setting::HasStatusLine()
{
	return status_line;
}

//
// SetStatusLine()
// set status line
//
void Setting::SetStatusLine(bool enable)
{
	status_line = enable;
}

//
// GetStatusAlpha()
// get alpha blending level for status
//
Uint8 Setting::GetStatusAlpha()
{
	return status_alpha;
}

//
// SetStatusAlpha()
// set alpha blending level for status
//
void Setting::SetStatusAlpha(Uint8 alpha)
{
	status_alpha = alpha;
}

//
// GetScaleQuality()
// get render scale quality
//
const char* Setting::GetScaleQuality()
{
	return scale_quality;
}

//
// SetScaleQuality()
// set render scale quality
//
void Setting::SetScaleQuality(int quality)
{
	scale_quality[0] = (char)('0' + quality);
}

//
// IsImageInterpolation()
// Is image interpolation
//
bool Setting::IsImageInterpolation()
{
	return scale_quality[0] == '3';
}

//
// IsForceRGB565()
// get force RGB565 mode (Android only)
//
bool Setting::IsForceRGB565()
{
	return force_rgb565;
}

//
// SetForceRGB565()
// set force RGB565 mode (Android only)
//
void Setting::SetForceRGB565(bool enable)
{
	force_rgb565 = enable;
}

//
// GetAudioDevice()
// get audio device index
//
int Setting::GetAudioDevice()
{
	return audio_output_device;
}

// SetAudioDevice()
// set audio device index
//
void Setting::SetAudioDevice(int id)
{
	audio_output_device = id;
}


//
// GetAudioFreq()
// get audio frequency
//
int Setting::GetAudioFreq()
{
	return config.sound_frequency;
}

//
// SetAudioFreq()
// set audio frequency
//
void Setting::SetAudioFreq(int freq)
{
	config.sound_frequency = freq;
}

//
// GetAudioPower()
// get audio samples (for SDL_OpenAudioDevice)
//
int Setting::GetAudioPower()
{
	return DEFAULT_AUDIO_POWER;
}

//
// GetAudioUnit()
// get audio samples (for vm->create_sound)
//
int Setting::GetAudioUnit()
{
	return DEFAULT_AUDIO_UNIT;
}

//
// GetAudioBuffer()
// get audio buffer (ms)
//
int Setting::GetAudioBuffer()
{
	return config.sound_latency;
}

//
// SetAudioBuffer()
// set audio buffer (ms)
//
void Setting::SetAudioBuffer(int ms)
{
	config.sound_latency = ms;
}

//
// HasOPNA()
// get sound board II
//
bool Setting::HasOPNA()
{
	if (config.sound_device_type == SETTING_SOUND_OPNA) {
		return true;
	}
	else {
		return false;
	}
}

//
// SetOPNA()
// set sound board II
//
void Setting::SetOPNA(bool opna)
{
	if (opna == true) {
		config.sound_device_type = SETTING_SOUND_OPNA;
	}
	else {
		config.sound_device_type = SETTING_SOUND_OPN;
	}
}

//
// GetSoftKeyType()
// get softkey type
//
int Setting::GetSoftKeyType()
{
	return softkey_set[softkey_index];
}

//
// NextSoftKey()
// next softkey
//
bool Setting::NextSoftKey()
{
	int loop;
	int current;
	int index;

	// get current
	current = GetSoftKeyType();
	index = softkey_index;

	// loop
	for (loop=0; loop<SDL_arraysize(softkey_set); loop++) {
		// next index
		index++;
		if (index >= SDL_arraysize(softkey_set)) {
			index = 0;
		}

		// compare
		if (softkey_set[index] != current) {
			break;
		}
	}

	// compare
	if (softkey_set[index] != current) {
		softkey_index = index;
		return true;
	}

	// all equal
	return false;
}

//
// PrevSoftKey()
// prev softkey
//
bool Setting::PrevSoftKey()
{
	int loop;
	int current;
	int index;

	// get current
	current = GetSoftKeyType();
	index = softkey_index;

	// loop
	for (loop=0; loop<SDL_arraysize(softkey_set); loop++) {
		// prev index
		index--;
		if (index < 0) {
			index = SDL_arraysize(softkey_set) - 1;
		}

		// compare
		if (softkey_set[index] != current) {
			break;
		}
	}

	// compare
	if (softkey_set[index] != current) {
		softkey_index = index;
		return true;
	}

	// all equal
	return false;
}

//
// GetSoftKeySet()
// get softkey set
//
int Setting::GetSoftKeySet(int set)
{
	return softkey_set[set];
}

//
// SetSoftKeySet()
// set softkey set
//
bool Setting::SetSoftKeySet(int set, int type)
{
	if (softkey_set[set] == type) {
		return false;
	}

	softkey_set[set] = type;
	return true;
}


//
// GetSoftKeyAlpha()
// get alpha blending level for softkey
//
Uint8 Setting::GetSoftKeyAlpha()
{
	return softkey_alpha;
}

//
// SetSoftKeyAlpha()
// set alpha blending level for softkey
//
void Setting::SetSoftKeyAlpha(Uint8 alpha)
{
	softkey_alpha = alpha;
}

//
// GetSoftKeyTime()
// get softkey timeout
//
Uint32 Setting::GetSoftKeyTime()
{
	return softkey_time;
}

//
// SetSoftKeyTime()
// set softkey timeout
//
void Setting::SetSoftKeyTime(Uint32 ms)
{
	softkey_time = ms;
}

//
// IsJoyEnable()
// get joystick enable
//
bool Setting::IsJoyEnable()
{
	return joystick_enable;
}

//
// SetJoyEnable()
// set joystick enable
//
void Setting::SetJoyEnable(bool enable)
{
	joystick_enable = enable;
}

//
// IsJoySwap()
// get joystick button swap
//
bool Setting::IsJoySwap()
{
	return joystick_swap;
}

//
// SetJoySwap()
// set joystick button swap
//
void Setting::SetJoySwap(bool swap)
{
	joystick_swap = swap;
}

//
// IsJoyKey()
// get joystick to key
//
bool Setting::IsJoyKey()
{
	return joystick_key;
}

//
// SetJoyKey()
// set joystick to key
//
void Setting::SetJoyKey(bool enable)
{
	joystick_key = enable;
}

//
// GetMouseTime()
// get mouse timeout
//
Uint32 Setting::GetMouseTime()
{
	return mouse_time;
}

//
// SetMouseTime()
// set mouse timeout
//
void Setting::SetMouseTime(Uint32 ms)
{
	mouse_time = ms;
}

//
// IsKeyEnable()
// get keyboard enable
//
bool Setting::IsKeyEnable()
{
	return keyboard_enable;
}

//
// SetKeyEnable()
// set keyboard enable
//
void Setting::SetKeyEnable(bool enable)
{
	keyboard_enable = enable;
}

//
// GetJoystickToKey()
// get joystick to keyboard map
//
Uint32 Setting::GetJoystickToKey(int button)
{
	return joystick_to_key[button];
}

//
// SetJoystickToKey()
// set joystick to keyboard map
//
void Setting::SetJoystickToKey(int button, Uint32 data)
{
	joystick_to_key[button] = data;
}

//
// DefJoystickToKey()
// default joystick to keyboard map
//
void Setting::DefJoystickToKey()
{
	joystick_to_key[0] = 0x0100;
	joystick_to_key[1] = 0x0002;
	joystick_to_key[2] = 0x0004;
	joystick_to_key[3] = 0x0006;
	joystick_to_key[4] = 0x0906;
	joystick_to_key[5] = 0x0e00;
	joystick_to_key[6] = 0x0907;
	joystick_to_key[7] = 0x0e02;
	joystick_to_key[8] = 0x0502;
	joystick_to_key[9] = 0x0500;
	joystick_to_key[10] = 0x0501;
	joystick_to_key[11] = 0x0306;
	joystick_to_key[12] = 0x1000;
	joystick_to_key[13] = 0x1000;
	joystick_to_key[14] = 0x1000;
}

//
// IsWatchBattery()
// get watch battery
//
bool Setting::IsWatchBattery()
{
	return watch_battery;
}

//
// SetWatchBattery()
// set watch battery
//
void Setting::SetWatchBattery(bool enable)
{
	watch_battery = enable;
}

//
// IsShowSoftKeyMouseMove()
// get show softkey mouse move
//
bool Setting::IsShowSoftKeyMouseMove()
{
	return mouse_move_softkey_enable;
}

//
// SetShowSoftKeyMouseMove()
// set show softkey mouse move
//
void Setting::SetShowSoftKeyMouseMove(bool enable)
{
	mouse_move_softkey_enable = enable;
}

//
// IsCursorToNumPad()
// get cursor to num pad
//
bool Setting::IsCursorToNumPad()
{
	return cursor_to_numpad_enable;
}

//
// SetCursorToNumPad()
// set cursor to num pad
//
void Setting::SetCursorToNumPad(bool enable)
{
	cursor_to_numpad_enable = enable;
}

//
// IsNumToNumPad()
// get num to num pad
//
bool Setting::IsNumToNumPad()
{
	return num_to_numpad_enable;
}

//
// SetNumToNumPad()
// set num to num pad
//
void Setting::SetNumToNumPad(bool enable)
{
	num_to_numpad_enable = enable;
}

//
// GetStateNum()
// get state number
//
int Setting::GetStateNum()
{
	return state_num;
}

//
// SetStateNum()
// set state number
//
void Setting::SetStateNum(int num)
{
	state_num = num;
}

//
// Is400Line()
// get 400/200 Lines
//
bool Setting::Is400Line()
{
	return config.is400Line;
}


#endif // SDL

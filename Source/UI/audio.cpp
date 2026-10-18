//
// eXcellent Multi-platform emulator type 8 - 'XM8'
// based on ePC-8801MA
//
// Author (ePC-8801MA) : Takeda.Toshiya
// Author (XM8) : Tanaka.Yasushi
//
// [ audio driver ]
//

#ifdef SDL

#include "os.h"
#include "common.h"
#include "audio.h"

//
// Audio()
// constructor
//
Audio::Audio()
{
	// object and device
	audio_sem = NULL;
	num_of_devices = 0;
	name_of_devices = NULL;
	device_id = 0;

	// control flag
	device_pause = true;
	play_pause = true;

	// buffer
	sample_buffer = NULL;
	sample_num = 0;
	sample_size = 0;
	sample_read = 0;
	sample_write = 0;
	sample_per = 0;
}

//
// ~Audio()
// destructor
//
Audio::~Audio()
{
	Deinit();
}

//
// Init()
// initialize
//
bool Audio::Init()
{
	size_t single_len;
	size_t total_len;
	int loop;
	char *ptr;

	// create semaphore
	audio_sem = SDL_CreateSemaphore(1);
	if (audio_sem == NULL) {
		return false;
	}

	// get the number of devices
	num_of_devices = SDL_GetNumAudioDevices(0);

	// no device ?
	if (num_of_devices == 0) {
		return true;
	}

	// get total length of device names (includes null terminator)
	total_len = 0;
	for (loop=0; loop<num_of_devices; loop++) {
		single_len = strlen(SDL_GetAudioDeviceName(loop, 0));
		single_len++;
		total_len += single_len;
	}

	// get devices names
	name_of_devices = (char *)SDL_malloc(total_len);
	if (name_of_devices == NULL) {
		SDL_assert(false);
		return false;
	}
	ptr = name_of_devices;
	for (loop=0; loop<num_of_devices; loop++) {
		single_len = strlen(SDL_GetAudioDeviceName(loop, 0));
		strcpy(ptr, SDL_GetAudioDeviceName(loop, 0));
		ptr += single_len;
		ptr++;
	}

	// initialize
	device_id = 0;
	play_pause = true;

	return true;
}

//
// Deinit()
// deinitialize
//
void Audio::Deinit()
{
	Close();

	// name of devices
	if (name_of_devices != NULL) {
		SDL_free(name_of_devices);
		name_of_devices = NULL;
	}

	// number of devices
	num_of_devices = 0;

	// semaphore
	if (audio_sem != NULL) {
		SDL_DestroySemaphore(audio_sem);
		audio_sem = NULL;
	}
}

//
// Open()
// open audio device
//
bool Audio::Open(const OpenParam *param)
{
	SDL_AudioSpec device_want;
	const char *name;

	// for GetRealFreq()
	device_spec.freq = param->freq;

	// check parameter
	if (param->device >= num_of_devices) {
		return false;
	}

	// init
	SDL_zero(device_want);

	// parameter
	device_want.freq = param->freq;
	device_want.format = AUDIO_S16SYS;
	device_want.channels = 2;
	device_want.samples = (Uint16)param->samples;
	device_want.callback = CommonCallback;
	device_want.userdata = (void*)this;

	if (param->device > 0) {
		// open device
		name = GetDeviceName(param->device - 1);
		if (name == NULL) {
			return false;
		}

		device_id = SDL_OpenAudioDevice(name, 0, &device_want, &device_spec, 0);
	} else {
		// Output audio device synchronizes with system settings.
		device_id = SDL_OpenAudioDevice(NULL, 0, &device_want, &device_spec, 0);
	}

	// result
	if (device_id == 0) {
		// device is already used -> no sound
		return true;
	}
	if (device_spec.channels != 2) {
		// mono device
		Close();
		return true;
	}

	// sample buffer
	sample_num = 0;
	sample_read = 0;
	sample_write = 0;
	sample_size = (device_spec.freq * device_spec.channels * sizeof(Sint16) * param->buffer) / 1000;
	sample_per = param->per * 2 * sizeof(Sint16);

	// allocate
	sample_buffer = (Uint8*)SDL_malloc(sample_size);
	if (sample_buffer == NULL) {
		Close();
		return false;
	}

	// once opened, the device starts with pause
	device_pause = true;

	return true;
}

//
// Close()
// close audio device
//
void Audio::Close()
{
	// free buffer
	if (sample_buffer != NULL) {
		SDL_free(sample_buffer);
		sample_buffer = NULL;
	}

	// close device
	if (device_id > 0) {
		SDL_CloseAudioDevice(device_id);
		device_id = 0;

		device_pause = true;
	}
}

//
// GetDeviceNum
// get number of device
//
int Audio::GetDeviceNum()
{
	return num_of_devices;
}

// GetDeviceName()
// get the name of specified audio device
//
const char* Audio::GetDeviceName(int device)
{
	int loop;
	char *ptr;
	size_t single_len;

	// check num
	if (device >= num_of_devices) {
		return NULL;
	}

	// init
	ptr = name_of_devices;

	// loop
	for (loop=0; loop<num_of_devices; loop++) {
		if (loop == device) {
			break;
		}

		single_len = strlen(ptr);
		ptr += single_len;
		ptr++;
	}

	return ptr;
}

//
// Play()
// play audio device
//
void Audio::Play()
{
	if (play_pause == true) {
		Reset();
		play_pause = false;
	}

	if (device_id > 0) {
		if (device_pause == true) {
			SDL_PauseAudioDevice(device_id, 0);
			device_pause = false;
		}
	}
}

//
// Stop()
// stop audio device
//
void Audio::Stop()
{
	play_pause = true;

	if (device_id > 0) {
		if (device_pause == false) {
			SDL_PauseAudioDevice(device_id, 1);
			device_pause = true;
		}
	}
}

//
// IsPlay()
// check play or pause
//
bool Audio::IsPlay()
{
	if (play_pause == true) {
		return false;
	}
	else {
		return true;
	}
}

//
// Reset()
// reset ring buffer
//
void Audio::Reset()
{
	if (sample_buffer != NULL) {
		memset(sample_buffer, 0, sample_size);

		// initial:50%
		sample_num = sample_size / 2;
		sample_num = (sample_num + 3) & ~3;
		sample_read = 0;
		sample_write = sample_num;
	}
}

//
// GetFreeSamples()
// get free samples
//
int Audio::GetFreeSamples()
{
	int samples;

	// lock
	SDL_SemWait(audio_sem);

	if (sample_buffer != NULL) {
		samples = sample_size - sample_num;
		samples /= (device_spec.channels * sizeof(Sint16));
	}
	else {
		// no sound
		samples = 0xffff;
	}

	// unlock
	SDL_SemPost(audio_sem);

	return samples;;
}

//
// Write()
// write to sample buffer
//
int Audio::Write(Uint8 *stream, int len)
{
	int size1;
	int size2;
	int loop;
	Sint32 *src;
	Sint16 *dest;
	int pct;

	SDL_assert(stream != NULL);

	// is audio device opened ?
	if (sample_buffer == NULL) {
		return 0x80;
	}

	len *= (device_spec.channels * sizeof(Sint16));

	// lock
	SDL_SemWait(audio_sem);

	// percent of current buffer before writting (256:100%)
	pct = (sample_num << 8) / sample_size;
	if (pct >= 0x100) {
		pct = 0xff;
	}

	// check overflow
	if ((sample_num + len) >= sample_size) {
		len = sample_size - sample_num;
	}

	// ring buffer
	size2 = (sample_write + len) - sample_size;
	if (size2 > 0) {
		size1 = len - size2;
	}
	else {
		size1 = len;
	}

	// copy
	if (size1 > 0) {
		src = (Sint32*)stream;
		dest = (Sint16*)&sample_buffer[sample_write];
		for (loop=0; loop<(int)(size1/sizeof(Sint16)); loop++) {
			if (src[loop] > 0xbfff) {
				dest[loop] = 0x7fff;
			}
			else {
				if (src[loop] < -0xc000) {
					dest[loop] = -0x8000;
				}
				else {
					dest[loop] = (Sint16)((src[loop] * 2) / 3);
				}
			}
		}
		sample_write += size1;
		sample_num += size1;
	}

	if (size2 > 0) {
		src = (Sint32*)&stream[size1 * 2];
		dest = (Sint16*)sample_buffer;
		for (loop=0; loop<(int)(size2/sizeof(Sint16)); loop++) {
			if (src[loop] > 0xbfff) {
				dest[loop] = 0x7fff;
			}
			else if (src[loop] < -0xc000) {
				dest[loop] = -0x8000;
			}
			else {
				dest[loop] = (Sint16)((src[loop] * 2) / 3);
			}
		}
		sample_write = size2;
		sample_num += size2;
	}

	// unlock
	SDL_SemPost(audio_sem);

	return pct;
}

//
// CommonCallback()
// common callback from SDL
//
void Audio::CommonCallback(void *userdata, Uint8 *stream, int len)
{
	Audio *audio;

	audio = (Audio*)userdata;

	if ((stream != NULL) && (len > 0)) {
		audio->Callback(stream, len);
	}
}

//
// Callback()
// callback to fill buffer
//
void Audio::Callback(Uint8 *stream, int len)
{
	int size1;
	int size2;

	// lock
	SDL_SemWait(audio_sem);

	if (len > sample_num) {
		// buffer underrun (ex: move window)
		Reset();
		if (len > sample_num) {
			// The request may exceed even the reset half-buffer (or full ring).
			// Fill its tail with silence and consume only the queued bytes.
			memset(stream, 0, len);
			len = sample_num;
		}
	}

	// ring buffer
	size2 = (sample_read + len) - sample_size;
	if (size2 > 0) {
		size1 = len - size2;
	}
	else {
		size1 = len;
	}

	// copy
	if (size1 > 0) {
		memcpy(stream, &sample_buffer[sample_read], size1);
		sample_read += size1;
		sample_num -= size1;
	}
	if (size2 > 0) {
		memcpy(&stream[size1], sample_buffer, size2);
		sample_read = size2;
		sample_num -= size2;
	}

	// unlock
	SDL_SemPost(audio_sem);
}

#endif // SDL

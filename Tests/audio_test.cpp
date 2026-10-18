// Use the real Audio ring buffer with a deterministic SDL audio-device boundary.
// SDL allocation and semaphores remain real; no device thread, window or ROM.
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "audio.h"

namespace {
void Require(bool ok, const char *message)
{
	if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
struct Device {
	SDL_AudioSpec spec{};
	bool opened = false;
	bool paused = true;
	int pauses = 0;
	int resumes = 0;
	int closes = 0;
} device;
int SDLCALL TestGetNumAudioDevices(int capture)
{
	Require(capture == 0, "output device enumeration");
	return 1;
}
const char* SDLCALL TestGetAudioDeviceName(int index, int capture)
{
	Require(index == 0 && capture == 0, "output device name");
	return "deterministic output";
}
SDL_AudioDeviceID SDLCALL TestOpenAudioDevice(const char *name, int capture,
	const SDL_AudioSpec *wanted, SDL_AudioSpec *obtained, int changes)
{
	Require(!device.opened, "previous device closed before open");
	Require(name == nullptr && capture == 0 && changes == 0, "default output device selection");
	Require(wanted->format == AUDIO_S16SYS && wanted->channels == 2, "signed 16-bit stereo output");
	Require(wanted->callback != nullptr && wanted->userdata != nullptr, "real callback registered");
	device.spec = *wanted;
	*obtained = *wanted;
	device.opened = true;
	device.paused = true;
	return 7;
}
void SDLCALL TestPauseAudioDevice(SDL_AudioDeviceID id, int pause)
{
	Require(id == 7 && device.opened, "pause valid device");
	device.paused = pause != 0;
	if (pause) ++device.pauses;
	else ++device.resumes;
}
void SDLCALL TestCloseAudioDevice(SDL_AudioDeviceID id)
{
	Require(id == 7 && device.opened, "close valid device once");
	device.opened = false;
	device.paused = true;
	++device.closes;
}
}

// Only this translation unit substitutes SDL's device boundary. The production
// Audio implementation, including Init/Open/Write/Play/Stop/Callback, is intact.
#define SDL_GetNumAudioDevices TestGetNumAudioDevices
#define SDL_GetAudioDeviceName TestGetAudioDeviceName
#define SDL_OpenAudioDevice TestOpenAudioDevice
#define SDL_PauseAudioDevice TestPauseAudioDevice
#define SDL_CloseAudioDevice TestCloseAudioDevice
#include "../Source/UI/audio.cpp"
#undef SDL_GetNumAudioDevices
#undef SDL_GetAudioDeviceName
#undef SDL_OpenAudioDevice
#undef SDL_PauseAudioDevice
#undef SDL_CloseAudioDevice
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace {
using Samples = std::vector<Sint16>;
Samples Silence(int frames) { return Samples(frames * 2, 0); }
Samples Expected(int first, int frames)
{
	Samples result;
	for (int i = first; i < first + frames; ++i) {
		result.push_back(static_cast<Sint16>(i * 2));
		result.push_back(static_cast<Sint16>(-i * 2));
	}
	return result;
}
int WriteFrames(Audio& audio, int first, int frames)
{
	// Distinct stereo frames; the documented 2/3 scale maps +/-3*n to +/-2*n.
	std::vector<Sint32> input;
	for (int i = first; i < first + frames; ++i) {
		input.push_back(i * 3); input.push_back(-i * 3);
	}
	if (input.empty()) input.resize(2); // Write's non-null precondition still holds.
	return audio.Write(reinterpret_cast<Uint8*>(input.data()), frames);
}
Samples Pull(int frames)
{
	Require(device.opened && !device.paused, "callback only while playing");
	constexpr Sint16 guard = 0x5a5a;
	Samples output(frames * 2 + 2, guard);
	device.spec.callback(device.spec.userdata,
		reinterpret_cast<Uint8*>(output.data() + 1), frames * 2 * sizeof(Sint16));
	Require(output.front() == guard && output.back() == guard, "callback output bounds");
	return Samples(output.begin() + 1, output.end() - 1);
}
struct Harness {
	Audio audio;
	explicit Harness(int rate = 1000, int milliseconds = 8)
	{
		Require(!device.opened, "isolated device fixture");
		device = {};
		Require(audio.Init(), "audio initialization");
		Audio::OpenParam param{0, rate, 2, 2, milliseconds};
		Require(audio.Open(&param), "open deterministic output");
		Require(device.paused && !audio.IsPlay(), "open starts stopped");
	}
	void StartEmpty(int initial_frames = 4)
	{
		audio.Play();
		Require(audio.IsPlay() && !device.paused, "Play resumes output");
		Require(Pull(initial_frames) == Silence(initial_frames), "initial half-buffer silence");
	}
};
}

int main()
{
	Require(SDL_Init(SDL_INIT_TIMER) == 0, "SDL timer only, no physical audio subsystem");
	{
		Harness h;
		Require(h.audio.GetDeviceNum() == 1 && h.audio.GetDeviceName(0) != nullptr, "device enumeration routed");
		Require(h.audio.GetFreeSamples() == 8, "new ring has eight free stereo frames");
		h.StartEmpty();
		Require(h.audio.GetFreeSamples() == 8, "initial silence consumed");
		Require(WriteFrames(h.audio, 1, 6) == 0, "occupancy before first write");
		Require(Pull(5) == Expected(1, 5), "read contiguous stereo frames");
		WriteFrames(h.audio, 7, 6); // Exact-end write with one unread frame present.
		Require(h.audio.GetFreeSamples() == 1, "wrapped write retains exact occupancy");
		Require(Pull(7) == Expected(6, 7), "wrapped read preserves channel order and unread frame");
		Require(h.audio.GetFreeSamples() == 8, "wrapped drain returns full capacity");
		WriteFrames(h.audio, 13, 8);
		Require(Pull(8) == Expected(13, 8), "whole-ring transfer across end");
		WriteFrames(h.audio, 21, 2);
		Require(Pull(2) == Expected(21, 2), "input works after exact-end boundary");
	}
	{
		Harness h;
		h.StartEmpty();
		WriteFrames(h.audio, 1, 6);
		Require(WriteFrames(h.audio, 7, 4) == 192, "partial overflow reports pre-write occupancy");
		Require(h.audio.GetFreeSamples() == 0, "overflow fills only available capacity");
		Require(WriteFrames(h.audio, 100, 2) == 255, "full ring reports capped occupancy");
		Require(h.audio.GetFreeSamples() == 0, "full write does not overfill");
		Require(Pull(8) == Expected(1, 8), "overflow discards excess input without overwriting unread frames");
		Require(WriteFrames(h.audio, 1, 0) == 0 && h.audio.GetFreeSamples() == 8, "zero-length write is inert");
		WriteFrames(h.audio, 23, 12);
		Require(Pull(8) == Expected(23, 8), "oversized write keeps first capacity frames");
	}
	{
		Harness h;
		h.StartEmpty();
		WriteFrames(h.audio, 1, 2);
		Require(Pull(3) == Silence(3), "underrun discards stale data and returns silence");
		Require(h.audio.GetFreeSamples() == 7, "small underrun retains remaining reset silence");
		Require(Pull(1) == Silence(1), "consume remaining reset silence");
		Require(Pull(4) == Silence(4), "empty ring recovers with half-buffer silence");
		for (int frames : {5, 8, 9, 17}) {
			Require(Pull(frames) == Silence(frames), "large underrun fills the entire request with silence");
			Require(h.audio.GetFreeSamples() == 8, "large underrun never makes queued byte count negative");
			WriteFrames(h.audio, 31, 3);
			Require(Pull(3) == Expected(31, 3), "data delivery recovers after large underrun");
		}
	}
	{
		Harness h;
		h.StartEmpty();
		WriteFrames(h.audio, 1, 2);
		h.audio.Play();
		Require(device.resumes == 1 && h.audio.GetFreeSamples() == 6, "repeated Play neither resets data nor resumes twice");
		Require(Pull(2) == Expected(1, 2), "repeated Play retains queued samples");
		WriteFrames(h.audio, 7, 3);
		h.audio.Stop(); h.audio.Stop();
		Require(!h.audio.IsPlay() && device.paused && device.pauses == 1, "Stop is idempotent and pauses output");
		for (int cycle = 0; cycle < 3; ++cycle) {
			h.audio.Play();
			Require(h.audio.IsPlay() && !device.paused && h.audio.GetFreeSamples() == 4, "restart resets buffer to half-full silence");
			Require(Pull(4) == Silence(4), "restart discards pre-stop samples");
			WriteFrames(h.audio, 41, 2);
			Require(Pull(2) == Expected(41, 2), "normal output after restart");
			h.audio.Stop();
		}
		h.audio.Close(); h.audio.Close();
		Require(!device.opened && device.closes == 1, "Close releases device exactly once");
	}
	{
		Harness h;
		h.StartEmpty();
		// Conversion boundaries apply to both ring segments; saturating values
		// distinguish negative clipping and channel ordering as well as scaling.
		WriteFrames(h.audio, 1, 2);
		Require(Pull(2) == Expected(1, 2), "position conversion across ring end");
		std::vector<Sint32> input{-49153, -49152, -3, 0, 3, 49151, 49152, 999999};
		h.audio.Write(reinterpret_cast<Uint8*>(input.data()), 4);
		Require(Pull(4) == Samples({-32768, -32768, -2, 0, 2, 32767, 32767, 32767}), "signed stereo conversion and clipping across wrap");
		const int free_before = h.audio.GetFreeSamples();
		device.spec.callback(device.spec.userdata, nullptr, 8);
		Uint8 sentinel = 0x7b;
		device.spec.callback(device.spec.userdata, &sentinel, 0);
		Require(sentinel == 0x7b && h.audio.GetFreeSamples() == free_before, "callback facade ignores null/empty requests");
	}
	// Production rates exercise buffer-size arithmetic independently of the
	// deliberately tiny ring used to force read/write boundaries above.
	const int rates[] = {44100, 48000, 55467};
	const int capacities[] = {5292, 5760, 6656};
	for (int i = 0; i < 3; ++i) {
		Harness h(rates[i], 120);
		Require(h.audio.GetFreeSamples() == capacities[i], "production-rate buffer capacity");
		h.StartEmpty(capacities[i] / 2);
		WriteFrames(h.audio, 51, 2);
		Require(Pull(2) == Expected(51, 2), "output at production sample rate");
	}
	Require(!device.opened, "all device fixtures closed before SDL shutdown");
	SDL_Quit();
	std::cout << "Audio wrapping, overflow/underrun, stereo conversion and Play/Stop tests passed\n";
}

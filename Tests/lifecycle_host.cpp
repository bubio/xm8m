// Only host boundaries are adapted. App, settings, rendering, VM and file I/O
// remain production implementations, including the full Init/Run/Deinit path.
#include <string>
#include <SDL.h>
#include "lifecycle_host.h"
namespace {
std::string preference_root;
bool reject_preferences = false;
int live_audio = 0;
char* SDLCALL TestPrefPath(const char*, const char*)
{
	if (reject_preferences) { SDL_SetError("injected preferences lookup failure"); return nullptr; }
	return SDL_strdup((preference_root + "/").c_str());
}
int SDLCALL TestMessageBox(const SDL_MessageBoxData*, int *button)
{
	if (button) *button = 1;
	return 0;
}
SDL_AudioDeviceID SDLCALL TestAudioOpen(const char*, int, const SDL_AudioSpec *wanted,
	SDL_AudioSpec *obtained, int)
{
	*obtained = *wanted;
	++live_audio;
	return 7;
}
void SDLCALL TestAudioPause(SDL_AudioDeviceID, int) {}
void SDLCALL TestAudioClose(SDL_AudioDeviceID) { --live_audio; }
}
namespace LifecycleHost {
void Configure(const std::string& root, bool reject)
{
	preference_root = root; reject_preferences = reject;
}
int LiveAudioDevices() { return live_audio; }
}
#define SDL_GetPrefPath TestPrefPath
#include "../Source/UI/setting.cpp"
#include "../Source/UI/emu_sdl.cpp"
#undef SDL_GetPrefPath
#define SDL_ShowMessageBox TestMessageBox
#include "../Source/UI/platform.cpp"
#undef SDL_ShowMessageBox
#define SDL_OpenAudioDevice TestAudioOpen
#define SDL_PauseAudioDevice TestAudioPause
#define SDL_CloseAudioDevice TestAudioClose
#include "../Source/UI/audio.cpp"
#undef SDL_OpenAudioDevice
#undef SDL_PauseAudioDevice
#undef SDL_CloseAudioDevice

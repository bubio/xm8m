// Standalone native tests have no SDLActivity or Android SAF provider.
// The real FILEIO Android branch opens generated private files through SDL.
#include <cstdlib>
#include <SDL.h>
#include "xm8jni.h"
namespace {
struct MainReady {
	MainReady() { SDL_SetMainReady(); }
} main_ready;
}
extern "C" int Android_GetSdkVersion() { return 23; }
extern "C" int Android_IsExternalSD(const char*) { return 0; }
// Any unexpected attempt to use an external provider fails the test.
extern "C" int Android_HasTreeUri() { std::abort(); }
extern "C" int Android_GetFileDescriptor(const char*, int) { std::abort(); }

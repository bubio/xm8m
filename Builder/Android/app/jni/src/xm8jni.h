//
// eXcellent Multi-platform emulator type 8 - 'XM8'
// based on ePC-8801MA
//
// Author (ePC-8801MA) : Takeda.Toshiya
// Author (XM8) : Tanaka.Yasushi
//
// [ Java Native Interface for Android ]
//


#ifdef SDL

#ifdef __ANDROID__

#ifndef XM8JNI_H
#define XM8JNI_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

//
// Android_HasIntent()
// check valid/invalid intent
//
int Android_HasIntent(void);

// atomically copy and clear the intent buffer
int Android_TakeIntent(char *buffer, size_t buffer_size);

//
// Android_GetSdkVersion()
// get Build.VERSION.SDK_INT
//
int Android_GetSdkVersion(void);

//
// Android_ChkSkipMain()
// check skip flag (main app)
//
int Android_ChkSkipMain(void);

//
// Android_RequestActivity()
// request start activity on next launch
//
void Android_RequestActivity(void);

//
// Android_Utf8macToUtf8()
// UTF-8 NFD to UTF-8 NFC
//
int Android_Utf8macToUtf8(const char *src, char *dst, size_t len);

//
// Android_HasExternalSD()
// get external SD flag
//
int Android_HasExternalSD(void);

//
// Android_HasTreeUri()
// get treeUri flag
//
int Android_HasTreeUri(void);

//
// Android_ClearTreeUri()
// clear treeUri
//
void Android_ClearTreeUri(void);

//
// Android_IsExternalSD()
// check path for external SD
//
int Android_IsExternalSD(const char *path);

//
// Android_GetFileDescriptor()
// get file descriptor through storage access framework (for Android 5.0 or later)
//
int Android_GetFileDescriptor(const char *path, int type);

//
// Android_ChDir()
// change directory (internal storage <-> exernal storage)
//
int Android_ChDir(char *dir, const char *name);

//
// Android_PollJoystick()
// call SDLControllerManager.pollInputDevices()
//
void Android_PollJoystick(void);

// Android_SetRotationMode()
// apply auto, landscape, or portrait Activity rotation mode
void Android_SetRotationMode(int mode);

// RetroAchievements Android bridge (available only in RA-enabled builds).
int Android_RaHttpSend(unsigned long long request_id, const char *url,
	const char *post_data, const char *content_type, const char *user_agent,
	int connect_timeout_ms, int total_timeout_ms, int max_response_bytes);
void Android_RaHttpCancel(unsigned long long request_id);
void Android_RaHttpCancelAll(void);
int Android_RaHasNetwork(void);
int Android_RaSaveCredential(const char *username, const unsigned char *token,
	size_t token_size);
int Android_RaLoadCredential(const char *username, unsigned char **token,
	size_t *token_size);
int Android_RaDeleteCredential(const char *username);
void Android_RaFreeCredential(unsigned char *token);
void Android_RaShowLogin(const char *username);
void Android_RaSetLoginResult(const char *message, int success);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // XM8JNI_H

#endif // __ANDROID__

#endif // SDL

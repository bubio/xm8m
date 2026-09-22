//
// eXcellent Multi-platform emulator type 8 - 'XM8'
// based on ePC-8801MA
//
// Author (ePC-8801MA) : Takeda.Toshiya
// Author (XM8) : Tanaka.Yasushi
//
// [ intent interface for Android ]
//

#ifdef SDL

#ifdef __ANDROID__

#include <string.h>
#include <pthread.h>
#include <jni.h>
#include <android/log.h>
#include "xm8jni.h"

//
// intent buffer
//
static char intent_buffer[4096 * 3];
static pthread_mutex_t intent_mutex = PTHREAD_MUTEX_INITIALIZER;

//
// nativeIntent()
// pass intent data to application through JNI
//
JNIEXPORT void JNICALL Java_net_retropc_pi_XM8_nativeIntent(JNIEnv* env, jclass jcls, jstring file_name)
{
	const char *src;
	unsigned char *dest;
	unsigned char high;
	unsigned char low;
	size_t remaining;

	// get string
	const char *path = (*env)->GetStringUTFChars(env, file_name, NULL);
	if (path == NULL) {
		return;
	}
	__android_log_print(ANDROID_LOG_INFO, "XM8" ,"nativeIntent() path=""\x22""%s""\x22", path);

	// pass string to application
	pthread_mutex_lock(&intent_mutex);
	src = path;
	dest = (unsigned char*)intent_buffer;
	remaining = sizeof(intent_buffer) - 1;
	while (*src != '\0' && remaining != 0) {
		// Java encodes UTF-8 string into '%hex' style
		if (*src == '%') {
			// get high and low
			high = (unsigned char)src[1];
			if (high == '\0') {
				break;
			}
			low = (unsigned char)src[2];
			if (low == '\0') {
				break;
			}
			src += 3;

			// high
			if ((high >= '0') && (high <= '9')) {
				high -= '0';
			}
			else {
				high |= 0x20;
				high -= 0x57;
			}
			high <<= 4;

			// low
			if ((low >= '0') && (low <= '9')) {
				low -= '0';
			}
			else {
				low |= 0x20;
				low -= 0x57;
			}

			*dest++ = (unsigned char)(high | low);
			remaining--;
		}
		else {
			*dest++ = (unsigned char)*src++;
			remaining--;
		}
	}

	// terminate string
	if (*src != '\0') {
		// Never open a truncated path as a different file.
		dest = (unsigned char*)intent_buffer;
	}
	*dest = '\0';
	pthread_mutex_unlock(&intent_mutex);

	// release string
	(*env)->ReleaseStringUTFChars(env, file_name, path);
}

//
// Android_HasIntent()
// check valid/invalid intent
//
int Android_HasIntent(void)
{
	pthread_mutex_lock(&intent_mutex);
	const int result = intent_buffer[0] != '\0';
	pthread_mutex_unlock(&intent_mutex);
	return result;
}

int Android_TakeIntent(char *buffer, size_t buffer_size)
{
	if (buffer == NULL || buffer_size == 0) return 0;
	pthread_mutex_lock(&intent_mutex);
	const size_t length = strlen(intent_buffer);
	if (length == 0 || length >= buffer_size) {
		buffer[0] = '\0';
		intent_buffer[0] = '\0';
		pthread_mutex_unlock(&intent_mutex);
		return 0;
	}
	memcpy(buffer, intent_buffer, length + 1);
	intent_buffer[0] = '\0';
	pthread_mutex_unlock(&intent_mutex);
	return 1;
}
#endif // __ANDROID__

#endif // SDL

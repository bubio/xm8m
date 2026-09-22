//
// eXcellent Multi-platform emulator type 8 - 'XM8'
// based on ePC-8801MA
//
// Author (ePC-8801MA) : Takeda.Toshiya
// Author (XM8) : Tanaka.Yasushi
//
// [ storage access interface for Android ]
//

#ifdef SDL

#ifdef __ANDROID__

#include <string.h>
#include <stdlib.h>
#include <jni.h>
#include <pthread.h>
#include <android/log.h>
#include "os.h"
#include "xm8jni.h"

//
// log tag
//
#define LOG_TAG						"XM8"
										// for __android_log_print() function

//
// path data (see emu_sdl.cpp)
//
#define EXTERNAL_PATH_ANDROID		"/Android/data/"
										// SDL_AndroidGetExtrernalStoragePath()


//
// Java class and method
//
#define JAVA_CLASS_NAME				"net/retropc/pi/XM8"
										// class name
#define JAVA_REQUEST_METHOD_NAME	"requestActivity"
										// method name (requestActivity)
#define JAVA_REQUEST_SIGNATURE_NAME	"()V"
										// sigunature name (requestActivity)
#define JAVA_GET_METHOD_NAME		"getFileDescriptor"
										// method name (getFileDescriptor)
#define JAVA_GET_SIGNATURE_NAME		"(Ljava/lang/String;I)I"
										// signature name (getFileDescriptor)
#define JAVA_CLEAR_METHOD_NAME		"clearTreeUri"
										// method name (clearTreeUri)
#define JAVA_CLEAR_SIGNATURE_NAME	"()V"
										// signature name (clearTreeUri)

//
// static variable
//
static pthread_key_t java_thread_key;
										// pthread key for detach
static JavaVM *java_vm;
										// Java virtual machine
static jobject java_activity;
										// Java activity instance
static jclass java_class;
										// Java activity class
static int sdk_version;
										// Build.VERSION.SDK_INT
static int skip_main;
										// skip flag (main)
static char abs_dir[0x200];
										// Activity.GetExternalFilesDir(null).GetAbsolutePath()
static char ext_dir[0x200];
										// Activity.GetExternalFilesDirs(null) if removable
static char tree_uri[0x200];
										// Uri got by Intent.ACTION_OPEN_DOCUMENT_TREE

//
// in SDL library
//
//extern void Android_JNI_PollInputDevices(void);
										// SDL_android.c
//extern void SDL_SYS_JoystickDetect(void);
										// SDL_joystick.c
extern void SDL_JoystickUpdate(void);

extern SDL_sem *Android_ResumeSem;
										// SDL_androidevents.c

//
// JNI_DetachThread()
// detach thread 
//
static void JNI_DetachThread(void *value)
{
	JNIEnv *env;

	// get Java environment
	env = (JNIEnv*)value;
	if (env != NULL) {
		// Only threads attached by JNI_GetEnvironment have this key.
		__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "JNI:DetachCurrentThread");
		(*java_vm)->DetachCurrentThread(java_vm);

		// clear pthread key
		pthread_setspecific(java_thread_key, NULL);
	}
}

//
// JNI_OnLoad()
// hook function on loading libmain.so
//
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved)
{
	// save java vm
	java_vm = vm;

	// create pthread_key for detach thread
	pthread_key_create(&java_thread_key, JNI_DetachThread);

	// see SDL_android.c
	return JNI_VERSION_1_4;
}

//
// JNI_GetEnvironment()
// get Java Environment for each thread
//
static JNIEnv* JNI_GetEnvironment(void)
{
	JNIEnv *env;
	int status;

	status = (*java_vm)->GetEnv(java_vm, (void**)&env, JNI_VERSION_1_4);
	if (status == JNI_EDETACHED) {
		status = (*java_vm)->AttachCurrentThread(java_vm, &env, NULL);
		if (status < 0) return NULL;
		pthread_setspecific(java_thread_key, (void*)env);
		__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "JNI:AttachCurrentThread");
	}
	else if (status != JNI_OK) return NULL;

	return env;
}

// nativeBuildVer()
// set Build.VERSION.SDK_INT
//
JNIEXPORT void JNICALL Java_net_retropc_pi_XM8_nativeBuildVer(JNIEnv *env, jclass jcls, jint ver)
{
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Build.VERSION.SDK_INT=%d", (int)ver);

	// The Java declaration is an instance method. Keep its object for method
	// calls and a real Class object for GetMethodID; CheckJNI rejects using an
	// Activity object where a jclass is required.
	if (java_activity != NULL) {
		(*env)->DeleteGlobalRef(env, java_activity);
		java_activity = NULL;
	}
	if (java_class != NULL) {
		(*env)->DeleteGlobalRef(env, java_class);
		java_class = NULL;
	}
	java_activity = (*env)->NewGlobalRef(env, jcls);
	jclass local_class = (*env)->GetObjectClass(env, jcls);
	if (local_class != NULL) {
		java_class = (jclass)(*env)->NewGlobalRef(env, local_class);
		(*env)->DeleteLocalRef(env, local_class);
	}

	// set running sdk version
	sdk_version = ver;
}

//
// nativeAbsDir()
// set Activity.GetExternalFilesDir(null).GetAbsolutePath()
//
JNIEXPORT void JNICALL Java_net_retropc_pi_XM8_nativeAbsDir(JNIEnv *env, jclass jcls, jstring jabs_dir)
{
	int len;
	char *root;

	// get string
	const char *dir = (*env)->GetStringUTFChars(env, jabs_dir, NULL);

	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "AbsoluteDir=%s", dir);

	// copy
	len = strlen(dir);
	if (len < sizeof(abs_dir)) {
		strcpy(abs_dir, dir);

		root = strstr(abs_dir, EXTERNAL_PATH_ANDROID);
		if (root != NULL) {
			// like "/storage/emulated/0/"
			root[1] = '\0';
		}
	}

	// release string
	(*env)->ReleaseStringUTFChars(env, jabs_dir, dir);
}

//
// nativeExtDir()
// set Activity.GetExternalFilesDirs(null) if removable
//
JNIEXPORT void JNICALL Java_net_retropc_pi_XM8_nativeExtDir(JNIEnv *env, jclass jcls, jstring jext_dir)
{
	int len;

	// get string
	const char *dir = (*env)->GetStringUTFChars(env, jext_dir, NULL);

	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "ExternalDir=%s", dir);

	// copy
	len = strlen(dir);
	if (len < (sizeof(ext_dir) - 1)) {
		strcpy(ext_dir, dir);

		if (ext_dir[len - 1] != '/') {
			strcat(ext_dir, "/");
		}
	}

	// release string
	(*env)->ReleaseStringUTFChars(env, jext_dir, dir);
}

//
// nativeUri()
// set tree Uri for SD card
//
JNIEXPORT void JNICALL Java_net_retropc_pi_XM8_nativeUri(JNIEnv *env, jclass jcls, jstring jtree_uri)
{
	int len;

	// get string
	const char *uri = (*env)->GetStringUTFChars(env, jtree_uri, NULL);

	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "treeUri=%s", uri);

	// copy
	len = strlen(uri);
	if (len < sizeof(tree_uri)) {
		strcpy(tree_uri, uri);
	}

	// release string
	(*env)->ReleaseStringUTFChars(env, jtree_uri, uri);
}

//
// nativeSkipMain()
// set skip flag (main)
//
JNIEXPORT void JNICALL Java_net_retropc_pi_XM8_nativeSkipMain(JNIEnv *env, jclass jcls, jint skip)
{
	__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "SkipMain=%d", (int)skip);
	skip_main = skip;
}

//
// nativeDelete()
// call DeleteGlobalRef()
//
JNIEXPORT void JNICALL Java_net_retropc_pi_XM8_nativeDelete(JNIEnv *env, jclass jcls)
{
	// clear activity references
	if (java_activity != NULL) {
		(*env)->DeleteGlobalRef(env, java_activity);
		java_activity = NULL;
	}
	if (java_class != NULL) {
		(*env)->DeleteGlobalRef(env, java_class);
		java_class = NULL;
	}
}

//
// Android_RequestActivity()
// request activity for SAF
//
void Android_RequestActivity(void)
{
	JNIEnv *env;
	jclass jcls;
	jmethodID id;

	// get Java environment
	env = JNI_GetEnvironment();
	if (env == NULL) {
		return;
	}

	// find class
	jcls = (*env)->FindClass(env, JAVA_CLASS_NAME);
	if (jcls == NULL) {
		return;
	}

	// get method id
	id = (*env)->GetMethodID(env, jcls, JAVA_REQUEST_METHOD_NAME, JAVA_REQUEST_SIGNATURE_NAME);
	if (id == NULL) {
		return;
	}

	// call
	(*env)->CallVoidMethod(env, java_activity, id);

    (*env)->DeleteLocalRef(env, jcls);
}

//
// Android_Utf8macToUtf8()
// UTF-8 NFD to UTF-8 NFC
//
int Android_Utf8macToUtf8(const char *src, char *dst, size_t len)
{
	JNIEnv *env;
	jclass jcls;

	// get Java environment
    env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    if (env == 0) {
        return -1;
    }

	// find class
	jcls = (*env)->FindClass(env, JAVA_CLASS_NAME);
	if (jcls == NULL) {
		return -1;
	}

	// get method id
	jmethodID convertToNFCMethod = (*env)->GetStaticMethodID(env, jcls, "convertToNFC",
														  "(Ljava/lang/String;)Ljava/lang/String;");
	if (convertToNFCMethod == NULL) {
		return -1;
	}

    jstring srcString = (*env)->NewStringUTF(env, src);
	jstring nfcResult = (jstring)(*env)->CallStaticObjectMethod(env, jcls, convertToNFCMethod, srcString);
    (*env)->DeleteLocalRef(env, srcString);
    (*env)->DeleteLocalRef(env, jcls);

	// nfcStrをC++のdstにコピー
	const char *dstStr = (*env)->GetStringUTFChars(env, nfcResult, NULL);
	strncpy(dst, dstStr, len);
	(*env)->ReleaseStringUTFChars(env, nfcResult, dstStr);
    (*env)->DeleteLocalRef(env, nfcResult);

	return 0;
}

//
// Android_HasExternalSD()
// get external SD flag
//
int Android_HasExternalSD(void)
{
	if (ext_dir[0] != '\0') {
		return 1;
	}
	else {
		return 0;
	}
}

//
// Android_HasTreeUri()
// get treeUri flag
//
int Android_HasTreeUri(void)
{
	if (tree_uri[0] != '\0') {
		return 1;
	}
	else {
		return 0;
	}
}

//
// Android_ClearTreeUri()
// clear treeUri
//
void Android_ClearTreeUri(void)
{
	JNIEnv *env;
	jclass jcls;
	jmethodID id;

	// clear
	tree_uri[0] = '\0';

	// get Java environment
	env = JNI_GetEnvironment();
	if (env == NULL) {
		return;
	}

	// find class
	jcls = (*env)->FindClass(env, JAVA_CLASS_NAME);
	if (jcls == NULL) {
		return;
	}

	// get method id
	id = (*env)->GetMethodID(env, jcls, JAVA_CLEAR_METHOD_NAME, JAVA_CLEAR_SIGNATURE_NAME);
	if (id == NULL) {
		return;
	}

	// call
	(*env)->CallVoidMethod(env, java_activity, id);

    (*env)->DeleteLocalRef(env, jcls);
}

//
// Android_IsExternalSD()
// check path for external SD
//
int Android_IsExternalSD(const char *path)
{
	int len;

	if (Android_HasExternalSD() == 0) {
		return 0;
	}

	// vaild ext_dir[]
	len = strlen(ext_dir);
	if (strncmp(path, ext_dir, len) == 0) {
		// external SD
		return 1;
	}

	// not external SD
	return 0;
}

//
// Android_GetFileDescriptor()
// get file descriptor through storage access framework (for Android 5.0 or later)
//
int Android_GetFileDescriptor(const char *path, int type)
{
	JNIEnv *env;
	jclass jcls;
	jmethodID id;
	jstring jstr;

	// get Java environment
	env = JNI_GetEnvironment();
	if (env == NULL) {
		return -1;
	}

	// find class
	jcls = (*env)->FindClass(env, JAVA_CLASS_NAME);
	if (jcls == NULL) {
		return -1;
	}

	// get method id
	id = (*env)->GetMethodID(env, jcls, JAVA_GET_METHOD_NAME, JAVA_GET_SIGNATURE_NAME);
	if (id == NULL) {
		return -1;
	}

	// call
	jstr = (*env)->NewStringUTF(env, path);
	int ret = (*env)->CallIntMethod(env, java_activity, id, jstr, type);

    (*env)->DeleteLocalRef(env, jstr);
    (*env)->DeleteLocalRef(env, jcls);

	return ret;
}

//
// Android_ChDir()
// change directory (internal storage <-> exernal storage)
//
int Android_ChDir(char *dir, const char *name)
{
	// check SDK version
	if (Android_GetSdkVersion() < 21) {
		return 0;
	}

	// "../" only
	if (strcmp(name, "../") != 0) {
		return 0;
	}

	// compare
	if (strcmp(abs_dir, dir) == 0) {
		// go to external storage or keep current directory
		if ((Android_HasExternalSD() != 0) && (Android_HasTreeUri() != 0)) {
			// go to external storage
			strcpy(dir, ext_dir);
			return 1;
		}

		// keep current directory
		strcpy(dir, abs_dir);
		return 1;
	}

	if (Android_HasExternalSD() != 0) {
		// compare
		if (strcmp(ext_dir, dir) == 0) {
			// go to internal storage
			strcpy(dir, abs_dir);
			return 1;
		}
	}

	return 0;
}

//
// Android_GetSdkVersion()
// get Build.VERSION.SDK_INT
//
int Android_GetSdkVersion(void)
{
	return sdk_version;
}

//
// Android_ChkSkipMain()
// check skip flag (main)
//
int Android_ChkSkipMain(void)
{
	return skip_main;
}

//
// Android_PollJoystick()
// call SDLControllerManager.pollInputDevices()
//
void Android_PollJoystick(void)
{
	// restore pause/resume behavior (force resume)
	if (!SDL_SemValue(Android_ResumeSem)) {
		SDL_SemPost(Android_ResumeSem);
	}

	// see SDL_SYS_JoystickDetect() in SDL_sysjoystick.c (static variables are not initialized after second launch)
//	Android_JNI_PollInputDevices();
//	SDL_SYS_JoystickDetect();
	SDL_JoystickUpdate();
}

//
// RetroAchievements bridge. Java owns TLS verification and the background
// executor; JNI only marshals requests and copies completed responses.
//
static int JNI_HasException(JNIEnv *env)
{
	if (!(*env)->ExceptionCheck(env)) return 0;
	(*env)->ExceptionClear(env);
	return 1;
}

static jmethodID JNI_RaMethod(JNIEnv *env, const char *name, const char *signature)
{
	if (java_class == NULL) return NULL;
	return (*env)->GetMethodID(env, java_class, name, signature);
}

//
// Android_SetRotationMode()
// apply Android Activity rotation mode
//
void Android_SetRotationMode(int mode)
{
	JNIEnv *env = JNI_GetEnvironment();
	if (env == NULL || java_activity == NULL || java_class == NULL) return;
	jmethodID id = JNI_RaMethod(env, "setRotationMode", "(I)V");
	if (id == NULL) return;
	(*env)->CallVoidMethod(env, java_activity, id, (jint)mode);
	JNI_HasException(env);
}

void Android_RaShowLogin(const char *username)
{
	JNIEnv *env = JNI_GetEnvironment();
	if (env == NULL || java_activity == NULL) return;
	jmethodID id = JNI_RaMethod(env, "raShowLogin", "(Ljava/lang/String;)V");
	if (id == NULL) return;
	jstring user = (*env)->NewStringUTF(env, username == NULL ? "" : username);
	if (user == NULL) return;
	(*env)->CallVoidMethod(env, java_activity, id, user);
	JNI_HasException(env);
	(*env)->DeleteLocalRef(env, user);
}

void Android_RaSetLoginResult(const char *message, int success)
{
	JNIEnv *env = JNI_GetEnvironment();
	if (env == NULL || java_activity == NULL) return;
	jmethodID id = JNI_RaMethod(env, "raSetLoginResult", "(Ljava/lang/String;Z)V");
	if (id == NULL) return;
	jstring text = (*env)->NewStringUTF(env, message == NULL ? "" : message);
	if (text == NULL) return;
	(*env)->CallVoidMethod(env, java_activity, id, text,
		success ? JNI_TRUE : JNI_FALSE);
	JNI_HasException(env);
	(*env)->DeleteLocalRef(env, text);
}

#ifdef XM8_ENABLE_RETROACHIEVEMENTS
extern void Android_RaLoginSubmitted(const char *username, const char *password);
extern void Android_RaLoginCanceled(void);
#endif
extern void Android_MenuBackRequested(void);
extern void Android_MouseBackRequested(void);

JNIEXPORT void JNICALL Java_net_retropc_pi_XM8_nativeMenuBackRequested(JNIEnv *env,
	jclass jcls)
{
	(void)env;
	(void)jcls;
	Android_MenuBackRequested();
}

JNIEXPORT void JNICALL Java_net_retropc_pi_XM8_nativeMouseBackRequested(JNIEnv *env,
	jclass jcls)
{
	(void)env;
	(void)jcls;
	Android_MouseBackRequested();
}

JNIEXPORT void JNICALL Java_net_retropc_pi_XM8_nativeRaLoginSubmitted(JNIEnv *env,
	jclass jcls, jstring username, jstring password)
{
	const char *user = username == NULL ? "" : (*env)->GetStringUTFChars(env, username, NULL);
	const char *pass = password == NULL ? "" : (*env)->GetStringUTFChars(env, password, NULL);
	#ifdef XM8_ENABLE_RETROACHIEVEMENTS
	if (user != NULL && pass != NULL) Android_RaLoginSubmitted(user, pass);
	#endif
	if (pass != NULL && password != NULL) (*env)->ReleaseStringUTFChars(env, password, pass);
	if (user != NULL && username != NULL) (*env)->ReleaseStringUTFChars(env, username, user);
}

JNIEXPORT void JNICALL Java_net_retropc_pi_XM8_nativeRaLoginCanceled(JNIEnv *env,
	jclass jcls)
{
	(void)env;
	(void)jcls;
	#ifdef XM8_ENABLE_RETROACHIEVEMENTS
	Android_RaLoginCanceled();
	#endif
}

int Android_RaHttpSend(unsigned long long request_id, const char *url,
	const char *post_data, const char *content_type, const char *user_agent,
	int connect_timeout_ms, int total_timeout_ms, int max_response_bytes)
{
	JNIEnv *env = JNI_GetEnvironment();
	if (env == NULL || url == NULL || user_agent == NULL || user_agent[0] == '\0') return 0;
	jmethodID id = JNI_RaMethod(env, "raSendHttp",
		"(JLjava/lang/String;[BLjava/lang/String;Ljava/lang/String;III)V");
	if (id == NULL) return 0;
	jstring jurl = (*env)->NewStringUTF(env, url);
	jstring jtype = content_type == NULL ? NULL : (*env)->NewStringUTF(env, content_type);
	jstring juser_agent = (*env)->NewStringUTF(env, user_agent);
	jbyteArray jpost = NULL;
	if (post_data != NULL) {
		size_t length = strlen(post_data);
		if (length > 0x7fffffff) {
			if (juser_agent) (*env)->DeleteLocalRef(env, juser_agent);
			if (jtype) (*env)->DeleteLocalRef(env, jtype);
			if (jurl) (*env)->DeleteLocalRef(env, jurl);
			return 0;
		}
		jpost = (*env)->NewByteArray(env, (jsize)length);
		if (jpost != NULL && length != 0) (*env)->SetByteArrayRegion(env, jpost, 0, (jsize)length, (const jbyte*)post_data);
	}
	if (jurl == NULL || juser_agent == NULL ||
		(post_data != NULL && jpost == NULL) || JNI_HasException(env)) {
		if (jpost) (*env)->DeleteLocalRef(env, jpost);
		if (juser_agent) (*env)->DeleteLocalRef(env, juser_agent);
		if (jtype) (*env)->DeleteLocalRef(env, jtype);
		if (jurl) (*env)->DeleteLocalRef(env, jurl);
		return 0;
	}
	(*env)->CallVoidMethod(env, java_activity, id, (jlong)request_id, jurl, jpost,
		jtype, juser_agent,
		(jint)connect_timeout_ms, (jint)total_timeout_ms, (jint)max_response_bytes);
	const int ok = !JNI_HasException(env);
	if (jpost) (*env)->DeleteLocalRef(env, jpost);
	(*env)->DeleteLocalRef(env, juser_agent);
	if (jtype) (*env)->DeleteLocalRef(env, jtype);
	(*env)->DeleteLocalRef(env, jurl);
	return ok;
}

void Android_RaHttpCancel(unsigned long long request_id)
{
	JNIEnv *env = JNI_GetEnvironment(); if (env == NULL) return;
	jmethodID id = JNI_RaMethod(env, "raCancelHttp", "(J)V");
	if (id == NULL) return;
	(*env)->CallVoidMethod(env, java_activity, id, (jlong)request_id); JNI_HasException(env);
}

void Android_RaHttpCancelAll(void)
{
	JNIEnv *env = JNI_GetEnvironment(); if (env == NULL) return;
	jmethodID id = JNI_RaMethod(env, "raCancelAllHttp", "()V");
	if (id == NULL) return;
	(*env)->CallVoidMethod(env, java_activity, id); JNI_HasException(env);
}

int Android_RaHasNetwork(void)
{
	JNIEnv *env = JNI_GetEnvironment(); if (env == NULL) return 0;
	jmethodID id = JNI_RaMethod(env, "raHasNetwork", "()Z");
	if (id == NULL) return 0;
	const jboolean result = (*env)->CallBooleanMethod(env, java_activity, id);
	return !JNI_HasException(env) && result == JNI_TRUE;
}

int Android_RaSaveCredential(const char *username, const unsigned char *token, size_t token_size)
{
	JNIEnv *env = JNI_GetEnvironment(); if (env == NULL || username == NULL || token == NULL || token_size > 0x7fffffff) return 0;
	jmethodID id = JNI_RaMethod(env, "raSaveCredential", "(Ljava/lang/String;[B)Z");
	if (id == NULL) return 0;
	jstring user = (*env)->NewStringUTF(env, username); jbyteArray bytes = (*env)->NewByteArray(env, (jsize)token_size);
	if (user == NULL || bytes == NULL) { if (user) (*env)->DeleteLocalRef(env, user); if (bytes) (*env)->DeleteLocalRef(env, bytes); return 0; }
	(*env)->SetByteArrayRegion(env, bytes, 0, (jsize)token_size, (const jbyte*)token);
	const jboolean result = (*env)->CallBooleanMethod(env, java_activity, id, user, bytes);
	const int ok = !JNI_HasException(env) && result == JNI_TRUE;
	(*env)->DeleteLocalRef(env, bytes); (*env)->DeleteLocalRef(env, user); return ok;
}

int Android_RaLoadCredential(const char *username, unsigned char **token, size_t *token_size)
{
	if (token == NULL || token_size == NULL) return 0;
	*token = NULL; *token_size = 0;
	JNIEnv *env = JNI_GetEnvironment(); if (env == NULL || username == NULL) return 0;
	jmethodID id = JNI_RaMethod(env, "raLoadCredential", "(Ljava/lang/String;)[B");
	if (id == NULL) return 0;
	jstring user = (*env)->NewStringUTF(env, username); if (user == NULL) return 0;
	jbyteArray bytes = (jbyteArray)(*env)->CallObjectMethod(env, java_activity, id, user);
	(*env)->DeleteLocalRef(env, user);
	if (JNI_HasException(env) || bytes == NULL) return 0;
	const jsize length = (*env)->GetArrayLength(env, bytes);
	if (length <= 0) { (*env)->DeleteLocalRef(env, bytes); return 0; }
	unsigned char *copy = (unsigned char*)malloc((size_t)length);
	if (copy == NULL) { (*env)->DeleteLocalRef(env, bytes); return 0; }
	(*env)->GetByteArrayRegion(env, bytes, 0, length, (jbyte*)copy); (*env)->DeleteLocalRef(env, bytes);
	if (JNI_HasException(env)) { free(copy); return 0; }
	*token = copy; *token_size = (size_t)length; return 1;
}

int Android_RaDeleteCredential(const char *username)
{
	JNIEnv *env = JNI_GetEnvironment(); if (env == NULL || username == NULL) return 0;
	jmethodID id = JNI_RaMethod(env, "raDeleteCredential", "(Ljava/lang/String;)Z");
	if (id == NULL) return 0;
	jstring user = (*env)->NewStringUTF(env, username); if (user == NULL) return 0;
	const jboolean result = (*env)->CallBooleanMethod(env, java_activity, id, user);
	const int ok = !JNI_HasException(env) && result == JNI_TRUE; (*env)->DeleteLocalRef(env, user); return ok;
}

void Android_RaFreeCredential(unsigned char *token) { free(token); }

#endif // __ANDROID__

#endif // SDL

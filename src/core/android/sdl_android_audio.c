/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2023 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "../../SDL_internal.h"

#include "SDL_main.h"

#include "SDL_android_common.h"
#include "sdl_android_audio.h"

#include "SDL_android.h"
#include "SDL_system.h"
#include "SDL_timer.h"

#include <android/log.h>

#define SDL_JAVA_PREFIX                               org_libsdl_app
#define CONCAT1(prefix, class, function)              CONCAT2(prefix, class, function)
#define CONCAT2(prefix, class, function)              Java_##prefix##_##class##_##function
#define SDL_JAVA_AUDIO_INTERFACE(function)            CONCAT1(SDL_JAVA_PREFIX, SDLAudioManager, function)

/* Audio encoding definitions */
#define ENCODING_PCM_8BIT  3
#define ENCODING_PCM_16BIT 2
#define ENCODING_PCM_FLOAT 4

static SDL_atomic_t bPermissionRequestPending;
static SDL_bool bPermissionRequestResult;

/* Java class SDLAudioManager */
JNIEXPORT void JNICALL SDL_JAVA_AUDIO_INTERFACE(nativeSetupJNI)(
    JNIEnv *env, jclass jcls);

JNIEXPORT void JNICALL
    SDL_JAVA_AUDIO_INTERFACE(addAudioDevice)(JNIEnv *env, jclass jcls, jboolean is_capture,
                                             jint device_id);

JNIEXPORT void JNICALL
    SDL_JAVA_AUDIO_INTERFACE(removeAudioDevice)(JNIEnv *env, jclass jcls, jboolean is_capture,
                                                jint device_id);

JNIEXPORT void JNICALL SDL_JAVA_AUDIO_INTERFACE(nativePermissionResult)(
    JNIEnv *env, jclass cls,
    jint requestCode, jboolean result);

static JNINativeMethod SDLAudioManager_tab[] = {
    { "nativeSetupJNI", "()I", SDL_JAVA_AUDIO_INTERFACE(nativeSetupJNI) },
    { "addAudioDevice", "(ZI)V", SDL_JAVA_AUDIO_INTERFACE(addAudioDevice) },
    { "removeAudioDevice", "(ZI)V", SDL_JAVA_AUDIO_INTERFACE(removeAudioDevice) },
    { "nativePermissionResult", "(IZ)V", SDL_JAVA_AUDIO_INTERFACE(nativePermissionResult) }
};


/* audio manager */
jclass mAudioManagerClass;

/* method signatures */
static jmethodID midGetAudioOutputDevices;
static jmethodID midGetAudioInputDevices;
static jmethodID midAudioOpen;
static jmethodID midAudioWriteByteBuffer;
static jmethodID midAudioWriteShortBuffer;
static jmethodID midAudioWriteFloatBuffer;
static jmethodID midAudioClose;
static jmethodID midCaptureOpen;
static jmethodID midCaptureReadByteBuffer;
static jmethodID midCaptureReadShortBuffer;
static jmethodID midCaptureReadFloatBuffer;
static jmethodID midCaptureClose;
static jmethodID midAudioSetThreadPriority;
static jmethodID midRequestPermission;

static void register_methods(JNIEnv *env, const char *classname, JNINativeMethod *methods, int nb)
{
    jclass clazz = (*env)->FindClass(env, classname);
    if (clazz == NULL || (*env)->RegisterNatives(env, clazz, methods, nb) < 0) {
        __android_log_print(ANDROID_LOG_ERROR, "SDL", "Failed to register methods of %s", classname);
        return;
    }
}

/* Library init */

void register_audio_methods(JNIEnv *env, JavaVM *vm, void *reserved)
{
    register_methods(env, "org/libsdl/app/SDLAudioManager", SDLAudioManager_tab, SDL_arraysize(SDLAudioManager_tab));

}

/* Audio initialization -- called before SDL_main() to initialize JNI bindings */
JNIEXPORT void JNICALL SDL_JAVA_AUDIO_INTERFACE(nativeSetupJNI)(JNIEnv *env, jclass cls)
{
    __android_log_print(ANDROID_LOG_VERBOSE, "SDL", "AUDIO nativeSetupJNI()");

#ifdef SDL_ANDROID_AUDIO_STRIPPED
    /*
     * Create mThreadKey so we can keep track of the JNIEnv assigned to each thread
     * Refer to http://developer.android.com/guide/practices/design/jni.html for the rationale behind this
     */
    Android_JNI_CreateKey_once();

    /* Save JNIEnv of SDLActivity */
    Android_JNI_SetEnv(env);
#endif

    mAudioManagerClass = (jclass)((*env)->NewGlobalRef(env, cls));

    midGetAudioOutputDevices = (*env)->GetStaticMethodID(env, mAudioManagerClass,
                                                         "getAudioOutputDevices",
                                                         "()[I");
    midGetAudioInputDevices = (*env)->GetStaticMethodID(env, mAudioManagerClass,
                                                        "getAudioInputDevices",
                                                        "()[I");
    midAudioOpen = (*env)->GetStaticMethodID(env, mAudioManagerClass,
                                             "audioOpen", "(IIIII)[I");
    midAudioWriteByteBuffer = (*env)->GetStaticMethodID(env, mAudioManagerClass,
                                                        "audioWriteByteBuffer", "([B)V");
    midAudioWriteShortBuffer = (*env)->GetStaticMethodID(env, mAudioManagerClass,
                                                         "audioWriteShortBuffer", "([S)V");
    midAudioWriteFloatBuffer = (*env)->GetStaticMethodID(env, mAudioManagerClass,
                                                         "audioWriteFloatBuffer", "([F)V");
    midAudioClose = (*env)->GetStaticMethodID(env, mAudioManagerClass,
                                              "audioClose", "()V");
    midCaptureOpen = (*env)->GetStaticMethodID(env, mAudioManagerClass,
                                               "captureOpen", "(IIIII)[I");
    midCaptureReadByteBuffer = (*env)->GetStaticMethodID(env, mAudioManagerClass,
                                                         "captureReadByteBuffer", "([BZ)I");
    midCaptureReadShortBuffer = (*env)->GetStaticMethodID(env, mAudioManagerClass,
                                                          "captureReadShortBuffer", "([SZ)I");
    midCaptureReadFloatBuffer = (*env)->GetStaticMethodID(env, mAudioManagerClass,
                                                          "captureReadFloatBuffer", "([FZ)I");
    midCaptureClose = (*env)->GetStaticMethodID(env, mAudioManagerClass,
                                                "captureClose", "()V");
    midAudioSetThreadPriority = (*env)->GetStaticMethodID(env, mAudioManagerClass,
                                                          "audioSetThreadPriority", "(ZI)V");

    midRequestPermission = (*env)->GetStaticMethodID(env, mAudioManagerClass,
                                                     "requestPermission", "(Ljava/lang/String;I)V");

    if (!midGetAudioOutputDevices || !midGetAudioInputDevices || !midAudioOpen ||
        !midAudioWriteByteBuffer || !midAudioWriteShortBuffer || !midAudioWriteFloatBuffer ||
        !midAudioClose ||
        !midCaptureOpen || !midCaptureReadByteBuffer || !midCaptureReadShortBuffer ||
        !midCaptureReadFloatBuffer || !midCaptureClose || !midAudioSetThreadPriority) {
        __android_log_print(ANDROID_LOG_WARN, "SDL",
                            "Missing some Java callbacks, do you have the latest version of SDLAudioManager.java?");
    }

    checkJNIReady();
}

#ifdef SDL_ANDROID_AUDIO_STRIPPED
void checkJNIReady(void)
{
    if (!mAudioManagerClass) {
        /* We aren't fully initialized, let's just return. */
        return;
    }

    SDL_SetMainReady();
}
#endif

extern void SDL_AddAudioDevice(const SDL_bool iscapture, const char *name, SDL_AudioSpec *spec, void *handle);
extern void SDL_RemoveAudioDevice(const SDL_bool iscapture, void *handle);

JNIEXPORT void JNICALL
SDL_JAVA_AUDIO_INTERFACE(addAudioDevice)(JNIEnv *env, jclass jcls, jboolean is_capture,
                                         jint device_id)
{
    if (SDL_GetCurrentAudioDriver() != NULL) {
        char device_name[64];
        SDL_snprintf(device_name, sizeof(device_name), "%d", device_id);
        SDL_Log("Adding device with name %s, capture %d", device_name, is_capture);
        SDL_AddAudioDevice(is_capture, SDL_strdup(device_name), NULL, (void *)((size_t)device_id + 1));
    }
}

JNIEXPORT void JNICALL
SDL_JAVA_AUDIO_INTERFACE(removeAudioDevice)(JNIEnv *env, jclass jcls, jboolean is_capture,
                                            jint device_id)
{
    if (SDL_GetCurrentAudioDriver() != NULL) {
        SDL_Log("Removing device with handle %d, capture %d", device_id + 1, is_capture);
        SDL_RemoveAudioDevice(is_capture, (void *)((size_t)device_id + 1));
    }
}

JNIEXPORT void JNICALL SDL_JAVA_AUDIO_INTERFACE(nativePermissionResult)(
    JNIEnv *env, jclass cls,
    jint requestCode, jboolean result)
{
    bPermissionRequestResult = result;
    SDL_AtomicSet(&bPermissionRequestPending, SDL_FALSE);
}

/*
 * Audio support
 */
static int audioBufferFormat = 0;
static jobject audioBuffer = NULL;
static void *audioBufferPinned = NULL;
static int captureBufferFormat = 0;
static jobject captureBuffer = NULL;

static void Android_JNI_GetAudioDevices(int *devices, int *length, int max_len, int is_input)
{
    JNIEnv *env = Android_JNI_GetEnv();
    jintArray result;

    if (is_input) {
        result = (*env)->CallStaticObjectMethod(env, mAudioManagerClass, midGetAudioInputDevices);
    } else {
        result = (*env)->CallStaticObjectMethod(env, mAudioManagerClass, midGetAudioOutputDevices);
    }

    *length = (*env)->GetArrayLength(env, result);

    *length = SDL_min(*length, max_len);

    (*env)->GetIntArrayRegion(env, result, 0, *length, devices);
}

void Android_DetectDevices(void)
{
    int inputs[100];
    int outputs[100];
    int inputs_length = 0;
    int outputs_length = 0;

    SDL_zeroa(inputs);

    Android_JNI_GetAudioDevices(inputs, &inputs_length, 100, 1 /* input devices */);

    for (int i = 0; i < inputs_length; ++i) {
        int device_id = inputs[i];
        char device_name[64];
        SDL_snprintf(device_name, sizeof(device_name), "%d", device_id);
        SDL_Log("Adding input device with name %s", device_name);
        SDL_AddAudioDevice(SDL_TRUE, SDL_strdup(device_name), NULL, (void *)((size_t)device_id + 1));
    }

    SDL_zeroa(outputs);

    Android_JNI_GetAudioDevices(outputs, &outputs_length, 100, 0 /* output devices */);

    for (int i = 0; i < outputs_length; ++i) {
        int device_id = outputs[i];
        char device_name[64];
        SDL_snprintf(device_name, sizeof(device_name), "%d", device_id);
        SDL_Log("Adding output device with name %s", device_name);
        SDL_AddAudioDevice(SDL_FALSE, SDL_strdup(device_name), NULL, (void *)((size_t)device_id + 1));
    }
}

int Android_JNI_OpenAudioDevice(int iscapture, int device_id, SDL_AudioSpec *spec)
{
    int audioformat;
    jobject jbufobj = NULL;
    jobject result;
    int *resultElements;
    jboolean isCopy;

    JNIEnv *env = Android_JNI_GetEnv();

    switch (spec->format) {
    case AUDIO_U8:
        audioformat = ENCODING_PCM_8BIT;
        break;
    case AUDIO_S16:
        audioformat = ENCODING_PCM_16BIT;
        break;
    case AUDIO_F32:
        audioformat = ENCODING_PCM_FLOAT;
        break;
    default:
        return SDL_SetError("Unsupported audio format: 0x%x", spec->format);
    }

    if (iscapture) {
        __android_log_print(ANDROID_LOG_VERBOSE, "SDL", "SDL audio: opening device for capture");
        result = (*env)->CallStaticObjectMethod(env, mAudioManagerClass, midCaptureOpen, spec->freq, audioformat, spec->channels, spec->samples, device_id);
    } else {
        __android_log_print(ANDROID_LOG_VERBOSE, "SDL", "SDL audio: opening device for output");
        result = (*env)->CallStaticObjectMethod(env, mAudioManagerClass, midAudioOpen, spec->freq, audioformat, spec->channels, spec->samples, device_id);
    }
    if (result == NULL) {
        /* Error during audio initialization, error printed from Java */
        return SDL_SetError("Java-side initialization failed");
    }

    if ((*env)->GetArrayLength(env, (jintArray)result) != 4) {
        return SDL_SetError("Unexpected results from Java, expected 4, got %d", (*env)->GetArrayLength(env, (jintArray)result));
    }
    isCopy = JNI_FALSE;
    resultElements = (*env)->GetIntArrayElements(env, (jintArray)result, &isCopy);
    spec->freq = resultElements[0];
    audioformat = resultElements[1];
    switch (audioformat) {
    case ENCODING_PCM_8BIT:
        spec->format = AUDIO_U8;
        break;
    case ENCODING_PCM_16BIT:
        spec->format = AUDIO_S16;
        break;
    case ENCODING_PCM_FLOAT:
        spec->format = AUDIO_F32;
        break;
    default:
        return SDL_SetError("Unexpected audio format from Java: %d\n", audioformat);
    }
    spec->channels = resultElements[2];
    spec->samples = resultElements[3];
    (*env)->ReleaseIntArrayElements(env, (jintArray)result, resultElements, JNI_ABORT);
    (*env)->DeleteLocalRef(env, result);

    /* Allocating the audio buffer from the Java side and passing it as the return value for audioInit no longer works on
     * Android >= 4.2 due to a "stale global reference" error. So now we allocate this buffer directly from this side. */
    switch (audioformat) {
    case ENCODING_PCM_8BIT:
    {
        jbyteArray audioBufferLocal = (*env)->NewByteArray(env, spec->samples * spec->channels);
        if (audioBufferLocal) {
            jbufobj = (*env)->NewGlobalRef(env, audioBufferLocal);
            (*env)->DeleteLocalRef(env, audioBufferLocal);
        }
    } break;
    case ENCODING_PCM_16BIT:
    {
        jshortArray audioBufferLocal = (*env)->NewShortArray(env, spec->samples * spec->channels);
        if (audioBufferLocal) {
            jbufobj = (*env)->NewGlobalRef(env, audioBufferLocal);
            (*env)->DeleteLocalRef(env, audioBufferLocal);
        }
    } break;
    case ENCODING_PCM_FLOAT:
    {
        jfloatArray audioBufferLocal = (*env)->NewFloatArray(env, spec->samples * spec->channels);
        if (audioBufferLocal) {
            jbufobj = (*env)->NewGlobalRef(env, audioBufferLocal);
            (*env)->DeleteLocalRef(env, audioBufferLocal);
        }
    } break;
    default:
        return SDL_SetError("Unexpected audio format from Java: %d\n", audioformat);
    }

    if (jbufobj == NULL) {
        __android_log_print(ANDROID_LOG_WARN, "SDL", "SDL audio: could not allocate an audio buffer");
        return SDL_OutOfMemory();
    }

    if (iscapture) {
        captureBufferFormat = audioformat;
        captureBuffer = jbufobj;
    } else {
        audioBufferFormat = audioformat;
        audioBuffer = jbufobj;
    }

    if (!iscapture) {
        isCopy = JNI_FALSE;

        switch (audioformat) {
        case ENCODING_PCM_8BIT:
            audioBufferPinned = (*env)->GetByteArrayElements(env, (jbyteArray)audioBuffer, &isCopy);
            break;
        case ENCODING_PCM_16BIT:
            audioBufferPinned = (*env)->GetShortArrayElements(env, (jshortArray)audioBuffer, &isCopy);
            break;
        case ENCODING_PCM_FLOAT:
            audioBufferPinned = (*env)->GetFloatArrayElements(env, (jfloatArray)audioBuffer, &isCopy);
            break;
        default:
            return SDL_SetError("Unexpected audio format from Java: %d\n", audioformat);
        }
    }
    return 0;
}

void *Android_JNI_GetAudioBuffer(void)
{
    return audioBufferPinned;
}

void Android_JNI_WriteAudioBuffer(void)
{
    JNIEnv *env = Android_JNI_GetEnv();

    switch (audioBufferFormat) {
    case ENCODING_PCM_8BIT:
        (*env)->ReleaseByteArrayElements(env, (jbyteArray)audioBuffer, (jbyte *)audioBufferPinned, JNI_COMMIT);
        (*env)->CallStaticVoidMethod(env, mAudioManagerClass, midAudioWriteByteBuffer, (jbyteArray)audioBuffer);
        break;
    case ENCODING_PCM_16BIT:
        (*env)->ReleaseShortArrayElements(env, (jshortArray)audioBuffer, (jshort *)audioBufferPinned, JNI_COMMIT);
        (*env)->CallStaticVoidMethod(env, mAudioManagerClass, midAudioWriteShortBuffer, (jshortArray)audioBuffer);
        break;
    case ENCODING_PCM_FLOAT:
        (*env)->ReleaseFloatArrayElements(env, (jfloatArray)audioBuffer, (jfloat *)audioBufferPinned, JNI_COMMIT);
        (*env)->CallStaticVoidMethod(env, mAudioManagerClass, midAudioWriteFloatBuffer, (jfloatArray)audioBuffer);
        break;
    default:
        __android_log_print(ANDROID_LOG_WARN, "SDL", "SDL audio: unhandled audio buffer format");
        break;
    }

    /* JNI_COMMIT means the changes are committed to the VM but the buffer remains pinned */
}

int Android_JNI_CaptureAudioBuffer(void *buffer, int buflen)
{
    JNIEnv *env = Android_JNI_GetEnv();
    jboolean isCopy = JNI_FALSE;
    jint br = -1;

    switch (captureBufferFormat) {
    case ENCODING_PCM_8BIT:
        SDL_assert((*env)->GetArrayLength(env, (jshortArray)captureBuffer) == buflen);
        br = (*env)->CallStaticIntMethod(env, mAudioManagerClass, midCaptureReadByteBuffer, (jbyteArray)captureBuffer, JNI_TRUE);
        if (br > 0) {
            jbyte *ptr = (*env)->GetByteArrayElements(env, (jbyteArray)captureBuffer, &isCopy);
            SDL_memcpy(buffer, ptr, br);
            (*env)->ReleaseByteArrayElements(env, (jbyteArray)captureBuffer, ptr, JNI_ABORT);
        }
        break;
    case ENCODING_PCM_16BIT:
        SDL_assert((*env)->GetArrayLength(env, (jshortArray)captureBuffer) == (buflen / sizeof(Sint16)));
        br = (*env)->CallStaticIntMethod(env, mAudioManagerClass, midCaptureReadShortBuffer, (jshortArray)captureBuffer, JNI_TRUE);
        if (br > 0) {
            jshort *ptr = (*env)->GetShortArrayElements(env, (jshortArray)captureBuffer, &isCopy);
            br *= sizeof(Sint16);
            SDL_memcpy(buffer, ptr, br);
            (*env)->ReleaseShortArrayElements(env, (jshortArray)captureBuffer, ptr, JNI_ABORT);
        }
        break;
    case ENCODING_PCM_FLOAT:
        SDL_assert((*env)->GetArrayLength(env, (jfloatArray)captureBuffer) == (buflen / sizeof(float)));
        br = (*env)->CallStaticIntMethod(env, mAudioManagerClass, midCaptureReadFloatBuffer, (jfloatArray)captureBuffer, JNI_TRUE);
        if (br > 0) {
            jfloat *ptr = (*env)->GetFloatArrayElements(env, (jfloatArray)captureBuffer, &isCopy);
            br *= sizeof(float);
            SDL_memcpy(buffer, ptr, br);
            (*env)->ReleaseFloatArrayElements(env, (jfloatArray)captureBuffer, ptr, JNI_ABORT);
        }
        break;
    default:
        __android_log_print(ANDROID_LOG_WARN, "SDL", "SDL audio: unhandled capture buffer format");
        break;
    }
    return br;
}

void Android_JNI_FlushCapturedAudio(void)
{
    JNIEnv *env = Android_JNI_GetEnv();
#if 0 /* !!! FIXME: this needs API 23, or it'll do blocking reads and never end. */
    switch (captureBufferFormat) {
    case ENCODING_PCM_8BIT:
        {
            const jint len = (*env)->GetArrayLength(env, (jbyteArray)captureBuffer);
            while ((*env)->CallStaticIntMethod(env, mActivityClass, midCaptureReadByteBuffer, (jbyteArray)captureBuffer, JNI_FALSE) == len) { /* spin */ }
        }
        break;
    case ENCODING_PCM_16BIT:
        {
            const jint len = (*env)->GetArrayLength(env, (jshortArray)captureBuffer);
            while ((*env)->CallStaticIntMethod(env, mActivityClass, midCaptureReadShortBuffer, (jshortArray)captureBuffer, JNI_FALSE) == len) { /* spin */ }
        }
        break;
    case ENCODING_PCM_FLOAT:
        {
            const jint len = (*env)->GetArrayLength(env, (jfloatArray)captureBuffer);
            while ((*env)->CallStaticIntMethod(env, mActivityClass, midCaptureReadFloatBuffer, (jfloatArray)captureBuffer, JNI_FALSE) == len) { /* spin */ }
        }
        break;
    default:
        __android_log_print(ANDROID_LOG_WARN, "SDL", "SDL audio: flushing unhandled capture buffer format");
        break;
    }
#else
    switch (captureBufferFormat) {
    case ENCODING_PCM_8BIT:
        (*env)->CallStaticIntMethod(env, mAudioManagerClass, midCaptureReadByteBuffer, (jbyteArray)captureBuffer, JNI_FALSE);
        break;
    case ENCODING_PCM_16BIT:
        (*env)->CallStaticIntMethod(env, mAudioManagerClass, midCaptureReadShortBuffer, (jshortArray)captureBuffer, JNI_FALSE);
        break;
    case ENCODING_PCM_FLOAT:
        (*env)->CallStaticIntMethod(env, mAudioManagerClass, midCaptureReadFloatBuffer, (jfloatArray)captureBuffer, JNI_FALSE);
        break;
    default:
        __android_log_print(ANDROID_LOG_WARN, "SDL", "SDL audio: flushing unhandled capture buffer format");
        break;
    }
#endif
}

void Android_JNI_CloseAudioDevice(const int iscapture)
{
    JNIEnv *env = Android_JNI_GetEnv();

    if (iscapture) {
        (*env)->CallStaticVoidMethod(env, mAudioManagerClass, midCaptureClose);
        if (captureBuffer) {
            (*env)->DeleteGlobalRef(env, captureBuffer);
            captureBuffer = NULL;
        }
    } else {
        (*env)->CallStaticVoidMethod(env, mAudioManagerClass, midAudioClose);
        if (audioBuffer) {
            (*env)->DeleteGlobalRef(env, audioBuffer);
            audioBuffer = NULL;
            audioBufferPinned = NULL;
        }
    }
}

void Android_JNI_AudioSetThreadPriority(int iscapture, int device_id)
{
    JNIEnv *env = Android_JNI_GetEnv();
    (*env)->CallStaticVoidMethod(env, mAudioManagerClass, midAudioSetThreadPriority, iscapture, device_id);
}

#ifdef SDL_ANDROID_AUDIO_STRIPPED

/* Library init */
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved)
{
    JNIEnv *env = NULL;

    mJavaVM = vm;

    if ((*mJavaVM)->GetEnv(mJavaVM, (void **)&env, JNI_VERSION_1_4) != JNI_OK) {
        __android_log_print(ANDROID_LOG_ERROR, "SDL", "Failed to get JNI Env");
        return JNI_VERSION_1_4;
    }

    register_audio_methods(env, vm, reserved);

    return JNI_VERSION_1_4;
}

#include "SDL_rwops.h"

int Android_JNI_FileOpen(SDL_RWops* ctx, const char* fileName, const char* mode)
{
    __android_log_print(ANDROID_LOG_ERROR, "SDL", "%s is unsupported in this compilation mode, please recompile with stripped audio flag off", __FUNCTION__);
    SDL_SetError("Unsupported in this compilation mode");
    return -1;
}

Sint64 Android_JNI_FileSize(SDL_RWops* ctx)
{
    __android_log_print(ANDROID_LOG_ERROR, "SDL", "%s is unsupported in this compilation mode, please recompile with stripped audio flag off", __FUNCTION__);
    SDL_SetError("Unsupported in this compilation mode");
    return -1;
}

Sint64 Android_JNI_FileSeek(SDL_RWops* ctx, Sint64 offset, int whence)
{
    __android_log_print(ANDROID_LOG_ERROR, "SDL", "%s is unsupported in this compilation mode, please recompile with stripped audio flag off", __FUNCTION__);
    SDL_SetError("Unsupported in this compilation mode");
    return -1;
}

size_t Android_JNI_FileRead(SDL_RWops* ctx, void* buffer, size_t size, size_t maxnum)
{
    __android_log_print(ANDROID_LOG_ERROR, "SDL", "%s is unsupported in this compilation mode, please recompile with stripped audio flag off", __FUNCTION__);
    SDL_SetError("Unsupported in this compilation mode");
    return -1;
}

size_t Android_JNI_FileWrite(SDL_RWops* ctx, const void* buffer, size_t size, size_t num)
{
    __android_log_print(ANDROID_LOG_ERROR, "SDL", "%s is unsupported in this compilation mode, please recompile with stripped audio flag off", __FUNCTION__);
    SDL_SetError("Unsupported in this compilation mode");
    return -1;
}

int Android_JNI_FileClose(SDL_RWops* ctxl)
{
    __android_log_print(ANDROID_LOG_ERROR, "SDL", "%s is unsupported in this compilation mode, please recompile with stripped audio flag off", __FUNCTION__);
    SDL_SetError("Unsupported in this compilation mode");
    return -1;
}

/* Lock / Unlock Mutex */
void Android_ActivityMutex_Lock()
{
    __android_log_print(ANDROID_LOG_ERROR, "SDL", "%s is unsupported in this compilation mode, please recompile with stripped audio flag off", __FUNCTION__);
    SDL_SetError("Unsupported in this compilation mode");
}

void Android_ActivityMutex_Unlock()
{
    __android_log_print(ANDROID_LOG_ERROR, "SDL", "%s is unsupported in this compilation mode, please recompile with stripped audio flag off", __FUNCTION__);
    SDL_SetError("Unsupported in this compilation mode");
}

/* Lock the Mutex when the Activity is in its 'Running' state */
void Android_ActivityMutex_Lock_Running()
{
    __android_log_print(ANDROID_LOG_ERROR, "SDL", "%s is unsupported in this compilation mode, please recompile with stripped audio flag off", __FUNCTION__);
    SDL_SetError("Unsupported in this compilation mode");
}

SDL_bool SDL_IsAndroidTablet(void)
{
    __android_log_print(ANDROID_LOG_ERROR, "SDL", "%s is unsupported in this compilation mode, please recompile with stripped audio flag off", __FUNCTION__);
    SDL_SetError("Unsupported in this compilation mode");
    return SDL_FALSE;
}

void Android_JNI_GetManifestEnvironmentVariables(void)
{
    __android_log_print(ANDROID_LOG_ERROR, "SDL", "%s is unsupported in this compilation mode, please recompile with stripped audio flag off", __FUNCTION__);
    SDL_SetError("Unsupported in this compilation mode");
    return;
}

SDL_bool Android_JNI_RequestPermission(const char *permission)
{
    JNIEnv *env = Android_JNI_GetEnv();
    jstring jpermission;
    const int requestCode = 1;

    /* Wait for any pending request on another thread */
    while (SDL_AtomicGet(&bPermissionRequestPending) == SDL_TRUE) {
        SDL_Delay(10);
    }
    SDL_AtomicSet(&bPermissionRequestPending, SDL_TRUE);

    jpermission = (*env)->NewStringUTF(env, permission);
    (*env)->CallStaticVoidMethod(env, mAudioManagerClass, midRequestPermission, jpermission, requestCode);
    (*env)->DeleteLocalRef(env, jpermission);

    /* Wait for the request to complete */
    while (SDL_AtomicGet(&bPermissionRequestPending) == SDL_TRUE) {
        SDL_Delay(10);
    }
    return bPermissionRequestResult;
}

const char *SDL_AndroidGetInternalStoragePath(void)
{
    __android_log_print(ANDROID_LOG_ERROR, "SDL", "%s is unsupported in this compilation mode, please recompile with stripped audio flag off", __FUNCTION__);
    SDL_SetError("Unsupported in this compilation mode");
    return "";
}

SDL_bool Android_JNI_ShouldMinimizeOnFocusLoss()
{
    __android_log_print(ANDROID_LOG_ERROR, "SDL", "%s is unsupported in this compilation mode, please recompile with stripped audio flag off", __FUNCTION__);
    SDL_SetError("Unsupported in this compilation mode");
    return SDL_FALSE;
}

#endif

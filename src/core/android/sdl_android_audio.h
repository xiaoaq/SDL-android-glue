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
#ifndef __SDL_ANDROID_AUDIO_H_
#define __SDL_ANDROID_AUDIO_H_

#include <jni.h>

#include "SDL_audio.h"

extern jclass mAudioManagerClass;

void register_audio_methods(JNIEnv *env, JavaVM *vm, void *reserved);

/* Audio support */
extern void Android_DetectDevices(void);
extern int Android_JNI_OpenAudioDevice(int iscapture, int device_id, SDL_AudioSpec *spec);
extern void *Android_JNI_GetAudioBuffer(void);
extern void Android_JNI_WriteAudioBuffer(void);
extern int Android_JNI_CaptureAudioBuffer(void *buffer, int buflen);
extern void Android_JNI_FlushCapturedAudio(void);
extern void Android_JNI_CloseAudioDevice(const int iscapture);
extern void Android_JNI_AudioSetThreadPriority(int iscapture, int device_id);

#endif // __SDL_ANDROID_AUDIO_H_

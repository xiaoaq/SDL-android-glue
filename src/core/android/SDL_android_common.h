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
#ifndef __SDL_ANDROID_COMMON_H_
#define __SDL_ANDROID_COMMON_H_

#include <pthread.h>
#include <jni.h>

extern pthread_key_t mThreadKey;
extern pthread_once_t key_once;
extern JavaVM *mJavaVM;


void checkJNIReady(void);

int Android_JNI_SetEnv(JNIEnv *env);
void Android_JNI_CreateKey_once(void);


#endif // __SDL_ANDROID_COMMON_H_

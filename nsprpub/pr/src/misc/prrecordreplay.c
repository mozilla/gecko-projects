/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "primpl.h"
#include "prrecordreplay.h"

#include <stdlib.h>

#if defined(XP_UNIX) && defined(USE_DLFCN)
#include <dlfcn.h>
#define HAVE_DLFCN
#endif

#ifdef WIN32
#include <windows.h>
#endif

PR_IMPLEMENT_DATA(PRBool) gPRIsRecordingOrReplaying = PR_FALSE;

void* PR_RecordReplayLoadInterface(const char* aName)
{
  void* rv = NULL;
#if defined(HAVE_DLFCN)
  rv = dlsym(RTLD_DEFAULT, aName);
#elif defined(WIN32)
  rv = GetProcAddress(LoadLibraryA("xul.dll"), aName);
#endif
  return rv;
}

static PRBool
TestEnv(const char* env)
{
  const char* value = getenv(env);
  return value && value[0];
}

void
PR_RecordReplayInitialize()
{
  PR_ASSERT(!gPRIsRecordingOrReplaying);
  if (TestEnv("RECORD") || TestEnv("REPLAY") || TestEnv("MIDDLEMAN_RECORD") || TestEnv("MIDDLEMAN_REPLAY")) {
    // If we aren't able to find the record/replay initialization function,
    // silently ignore the environment variable.
    void* initialize = PR_RecordReplayLoadInterface("RecordReplayInterface_Initialize");
    if (initialize) {
      ((void (*)())initialize)();
    }
  }
}

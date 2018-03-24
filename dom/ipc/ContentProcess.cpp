/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ipc/IOThreadChild.h"

#include "ContentProcess.h"
#include "base/shared_memory.h"
#include "mozilla/Preferences.h"
#include "mozilla/Scheduler.h"
#include "mozilla/recordreplay/ParentIPC.h"

#if defined(XP_MACOSX) && defined(MOZ_CONTENT_SANDBOX)
#include <stdlib.h>
#endif

#if (defined(XP_WIN) || defined(XP_MACOSX)) && defined(MOZ_CONTENT_SANDBOX)
#include "mozilla/SandboxSettings.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsDirectoryService.h"
#include "nsDirectoryServiceDefs.h"
#endif

using mozilla::ipc::IOThreadChild;

namespace mozilla {
namespace dom {

#if defined(XP_WIN) && defined(MOZ_CONTENT_SANDBOX)
static void
SetTmpEnvironmentVariable(nsIFile* aValue)
{
  // Save the TMP environment variable so that is is picked up by GetTempPath().
  // Note that we specifically write to the TMP variable, as that is the first
  // variable that is checked by GetTempPath() to determine its output.
  nsAutoString fullTmpPath;
  nsresult rv = aValue->GetPath(fullTmpPath);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }
  Unused << NS_WARN_IF(!SetEnvironmentVariableW(L"TMP", fullTmpPath.get()));
  // We also set TEMP in case there is naughty third-party code that is
  // referencing the environment variable directly.
  Unused << NS_WARN_IF(!SetEnvironmentVariableW(L"TEMP", fullTmpPath.get()));
}
#endif


#if defined(XP_WIN) && defined(MOZ_CONTENT_SANDBOX)
static void
SetUpSandboxEnvironment()
{
  MOZ_ASSERT(nsDirectoryService::gService,
    "SetUpSandboxEnvironment relies on nsDirectoryService being initialized");

  // On Windows, a sandbox-writable temp directory is used whenever the sandbox
  // is enabled.
  if (!IsContentSandboxEnabled()) {
    return;
  }

  nsCOMPtr<nsIFile> sandboxedContentTemp;
  nsresult rv =
    nsDirectoryService::gService->Get(NS_APP_CONTENT_PROCESS_TEMP_DIR,
                                      NS_GET_IID(nsIFile),
                                      getter_AddRefs(sandboxedContentTemp));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  // Change the gecko defined temp directory to our sandbox-writable one.
  // Undefine returns a failure if the property is not already set.
  Unused << nsDirectoryService::gService->Undefine(NS_OS_TEMP_DIR);
  rv = nsDirectoryService::gService->Set(NS_OS_TEMP_DIR, sandboxedContentTemp);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  SetTmpEnvironmentVariable(sandboxedContentTemp);
}
#endif

#ifdef ANDROID
static int gPrefsFd = -1;

void
SetPrefsFd(int aFd)
{
  gPrefsFd = aFd;
}
#endif

bool
ContentProcess::Init(int aArgc, char* aArgv[])
{
  // If passed in grab the application path for xpcom init
  bool foundAppdir = false;
  bool foundChildID = false;
  bool foundIsForBrowser = false;
#ifdef XP_WIN
  bool foundPrefsHandle = false;
#endif
  bool foundPrefsLen = false;
  bool foundSchedulerPrefs = false;

  uint64_t childID;
  bool isForBrowser;

#if defined(XP_MACOSX) && defined(MOZ_CONTENT_SANDBOX)
  // If passed in grab the profile path for sandboxing
  bool foundProfile = false;
  nsCOMPtr<nsIFile> profileDir;
#endif

  char* schedulerPrefs = nullptr;
  base::SharedMemoryHandle prefsHandle = base::SharedMemory::NULLHandle();
  size_t prefsLen = 0;
  for (int idx = aArgc; idx > 0; idx--) {
    if (!aArgv[idx]) {
      continue;
    }

    if (!strcmp(aArgv[idx], "-appdir")) {
      MOZ_ASSERT(!foundAppdir);
      if (foundAppdir) {
        continue;
      }
      nsCString appDir;
      appDir.Assign(nsDependentCString(aArgv[idx+1]));
      mXREEmbed.SetAppDir(appDir);
      foundAppdir = true;
    } else if (!strcmp(aArgv[idx], "-childID")) {
      MOZ_ASSERT(!foundChildID);
      if (foundChildID) {
        continue;
      }
      if (idx + 1 < aArgc) {
        childID = strtoull(aArgv[idx + 1], nullptr, 10);
        foundChildID = true;
      }
    } else if (!strcmp(aArgv[idx], "-isForBrowser") || !strcmp(aArgv[idx], "-notForBrowser")) {
      MOZ_ASSERT(!foundIsForBrowser);
      if (foundIsForBrowser) {
        continue;
      }
      isForBrowser = strcmp(aArgv[idx], "-notForBrowser");
      foundIsForBrowser = true;
#ifdef XP_WIN
    } else if (!strcmp(aArgv[idx], "-prefsHandle")) {
      char* str = aArgv[idx + 1];
      MOZ_ASSERT(str[0] != '\0');
      // ContentParent uses %zu to print a word-sized unsigned integer. So even
      // though strtoull() returns a long long int, it will fit in a uintptr_t.
      prefsHandle = reinterpret_cast<HANDLE>(strtoull(str, &str, 10));
      MOZ_ASSERT(str[0] == '\0');
      foundPrefsHandle = true;
#endif
    } else if (!strcmp(aArgv[idx], "-prefsLen")) {
      char* str = aArgv[idx + 1];
      MOZ_ASSERT(str[0] != '\0');
      // ContentParent uses %zu to print a word-sized unsigned integer. So even
      // though strtoull() returns a long long int, it will fit in a uintptr_t.
      prefsLen = strtoull(str, &str, 10);
      MOZ_ASSERT(str[0] == '\0');
      foundPrefsLen = true;
    } else if (!strcmp(aArgv[idx], "-schedulerPrefs")) {
      schedulerPrefs = aArgv[idx + 1];
      foundSchedulerPrefs = true;
    } else if (!strcmp(aArgv[idx], "-safeMode")) {
      gSafeMode = true;
    }

#if defined(XP_MACOSX) && defined(MOZ_CONTENT_SANDBOX)
    else if (!strcmp(aArgv[idx], "-profile")) {
      MOZ_ASSERT(!foundProfile);
      if (foundProfile) {
        continue;
      }
      bool flag;
      nsresult rv = XRE_GetFileFromPath(aArgv[idx+1], getter_AddRefs(profileDir));
      if (NS_FAILED(rv) ||
          NS_FAILED(profileDir->Exists(&flag)) || !flag) {
        NS_WARNING("Invalid profile directory passed to content process.");
        profileDir = nullptr;
      }
      foundProfile = true;
    }
#endif /* XP_MACOSX && MOZ_CONTENT_SANDBOX */

    bool allFound = foundAppdir
                 && foundChildID
                 && foundIsForBrowser
                 && foundPrefsLen
                 && foundSchedulerPrefs
#ifdef XP_WIN
                 && foundPrefsHandle
#endif
#if defined(XP_MACOSX) && defined(MOZ_CONTENT_SANDBOX)
                 && foundProfile
#endif
                 && true;

    if (allFound) {
      break;
    }
  }

#ifdef ANDROID
  // Android is different; get the FD via gPrefsFd instead of a fixed fd.
  MOZ_RELEASE_ASSERT(gPrefsFd != -1);
  prefsHandle = base::FileDescriptor(gPrefsFd, /* auto_close */ true);
#elif XP_UNIX
  prefsHandle = base::FileDescriptor(kPrefsFileDescriptor,
                                     /* auto_close */ true);
#endif

  if (recordreplay::IsRecordingOrReplaying()) {
    // Set up early prefs from shmem contents passed to us by the middleman.
    Preferences::DeserializePreferences(recordreplay::child::PrefsShmemContents(prefsLen),
                                        prefsLen);
  } else {
    // Set up early prefs from the shared memory.
    base::SharedMemory shm;
    if (!shm.SetHandle(prefsHandle, /* read_only */ true)) {
      NS_ERROR("failed to open shared memory in the child");
      return false;
    }
    if (!shm.Map(prefsLen)) {
      NS_ERROR("failed to map shared memory in the child");
      return false;
    }
    Preferences::DeserializePreferences(static_cast<char*>(shm.memory()),
                                        prefsLen);
    if (recordreplay::IsMiddleman()) {
      recordreplay::parent::NotePrefsShmemContents(static_cast<char*>(shm.memory()),
                                                   prefsLen);
    }
  }

  Scheduler::SetPrefs(schedulerPrefs);

  if (recordreplay::IsMiddleman()) {
    recordreplay::parent::Initialize(aArgc, aArgv, ParentPid(), childID, &mContent);
  } else {
    mContent.Init(IOThreadChild::message_loop(),
                  ParentPid(),
                  IOThreadChild::channel(),
                  childID,
                  isForBrowser);
  }

  mXREEmbed.Start();
#if (defined(XP_MACOSX)) && defined(MOZ_CONTENT_SANDBOX)
  mContent.SetProfileDir(profileDir);
#endif

#if defined(XP_WIN) && defined(MOZ_CONTENT_SANDBOX)
  SetUpSandboxEnvironment();
#endif

  return true;
}

// Note: CleanUp() never gets called in non-debug builds because we exit early
// in ContentChild::ActorDestroy().
void
ContentProcess::CleanUp()
{
  mXREEmbed.Stop();
}

} // namespace dom
} // namespace mozilla

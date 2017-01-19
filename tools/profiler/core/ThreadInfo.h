/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZ_THREAD_INFO_H
#define MOZ_THREAD_INFO_H

#include "mozilla/UniquePtrExtensions.h"

#include "platform.h"

class ThreadInfo {
 public:
  ThreadInfo(const char* aName, int aThreadId, bool aIsMainThread, PseudoStack* aPseudoStack, void* aStackTop);

  virtual ~ThreadInfo();

  const char* Name() const { return mName.get(); }
  int ThreadId() const { return mThreadId; }

  bool IsMainThread() const { return mIsMainThread; }
  PseudoStack* Stack() const { return mPseudoStack; }

  void SetProfile(mozilla::UniquePtr<ThreadProfile> aProfile)
  {
    mProfile = mozilla::Move(aProfile);
  }
  ThreadProfile* Profile() const { return mProfile.get(); }

  PlatformData* GetPlatformData() const { return mPlatformData.get(); }
  void* StackTop() const { return mStackTop; }

  virtual void SetPendingDelete();
  bool IsPendingDelete() const { return mPendingDelete; }

  /**
   * May be null for the main thread if the profiler was started during startup
   */
  nsIThread* GetThread() const { return mThread.get(); }

  bool CanInvokeJS() const;

 private:
  mozilla::UniqueFreePtr<char> mName;
  int mThreadId;
  const bool mIsMainThread;
  PseudoStack* mPseudoStack;
  Sampler::UniquePlatformData mPlatformData;
  mozilla::UniquePtr<ThreadProfile> mProfile;
  void* mStackTop;
  nsCOMPtr<nsIThread> mThread;
  bool mPendingDelete;
};

// Just like ThreadInfo, but owns a reference to the PseudoStack.
class StackOwningThreadInfo : public ThreadInfo {
 public:
  StackOwningThreadInfo(const char* aName, int aThreadId, bool aIsMainThread, PseudoStack* aPseudoStack, void* aStackTop);
  virtual ~StackOwningThreadInfo();

  virtual void SetPendingDelete();
};

#endif

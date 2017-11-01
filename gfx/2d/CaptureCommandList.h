/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_gfx_2d_CaptureCommandList_h
#define mozilla_gfx_2d_CaptureCommandList_h

#include "mozilla/Move.h"
#include "mozilla/PodOperations.h"
#include <vector>

namespace mozilla {
namespace gfx {

class DrawingCommand;

class CaptureCommandList
{
public:
  CaptureCommandList()
  {}
  CaptureCommandList(CaptureCommandList&& aOther)
   : mStorage(Move(aOther.mStorage))
  {}
  ~CaptureCommandList();

  CaptureCommandList& operator =(CaptureCommandList&& aOther) {
    mStorage = Move(aOther.mStorage);
    return *this;
  }

  template <typename T>
  T* Append() {
    size_t oldSize = mStorage.size();
    mStorage.resize(mStorage.size() + sizeof(T) + sizeof(uint32_t));
    uint8_t* nextDrawLocation = &mStorage.front() + oldSize;
    *(uint32_t*)(nextDrawLocation) = sizeof(T) + sizeof(uint32_t);
    return reinterpret_cast<T*>(nextDrawLocation + sizeof(uint32_t));
  }

  class iterator
  {
  public:
    explicit iterator(CaptureCommandList& aParent)
     : mParent(aParent),
       mCurrent(nullptr),
       mEnd(nullptr)
    {
      if (!mParent.mStorage.empty()) {
        mCurrent = &mParent.mStorage.front();
        mEnd = mCurrent + mParent.mStorage.size();
      }
    }
    bool Done() const {
      return mCurrent >= mEnd;
    }
    void Next() {
      MOZ_ASSERT(!Done());
      mCurrent += *reinterpret_cast<uint32_t*>(mCurrent);
    }
    DrawingCommand* Get() {
      MOZ_ASSERT(!Done());
      return reinterpret_cast<DrawingCommand*>(mCurrent + sizeof(uint32_t));
    }

  private:
    CaptureCommandList& mParent;
    uint8_t* mCurrent;
    uint8_t* mEnd;
  };

private:
  CaptureCommandList(const CaptureCommandList& aOther) = delete;
  void operator =(const CaptureCommandList& aOther) = delete;

private:
  std::vector<uint8_t> mStorage;
};

} // namespace gfx
} // namespace mozilla

#endif // mozilla_gfx_2d_CaptureCommandList_h

/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "js/ArrayBuffer.h"
#include "js/Value.h"
#include "mozilla/dom/WebGPUBinding.h"
#include "mozilla/ipc/Shmem.h"
#include "mozilla/Logging.h"
#include "Device.h"

#include "Adapter.h"
#include "ipc/WebGPUChild.h"

namespace mozilla {
namespace webgpu {

mozilla::LazyLogModule gWebGPULog("WebGPU");

NS_IMPL_CYCLE_COLLECTION_INHERITED(Device, DOMEventTargetHelper, mBridge)
NS_IMPL_ISUPPORTS_CYCLE_COLLECTION_INHERITED_0(Device, DOMEventTargetHelper)
GPU_IMPL_JS_WRAP(Device)

static void mapFreeCallback(void* aContents, void* aUserData) {
  Unused << aContents;
  Unused << aUserData;
}

JSObject* Device::CreateExternalArrayBuffer(JSContext* aCx, size_t aSize,
                                            ipc::Shmem& aShmem) {
  MOZ_ASSERT(aShmem.Size<uint8_t>() == aSize);
  return JS::NewExternalArrayBuffer(aCx, aSize, aShmem.get<uint8_t>(),
                                    &mapFreeCallback, nullptr);
}

Device::Device(Adapter* const aParent, RawId aId)
    : DOMEventTargetHelper(aParent->GetParentObject()),
      mBridge(aParent->GetBridge()),
      mId(aId) {}

Device::~Device() {
  // could be `nullptr` if CC-ed
  if (mBridge && mBridge->IsOpen()) {
    mBridge->SendDeviceDestroy(mId);
  }
}

void Device::GetLabel(nsAString& aValue) const { aValue = mLabel; }
void Device::SetLabel(const nsAString& aLabel) { mLabel = aLabel; }

already_AddRefed<Buffer> Device::CreateBuffer(
    const dom::GPUBufferDescriptor& aDesc) {
  RawId id = mBridge->DeviceCreateBuffer(mId, aDesc);
  RefPtr<Buffer> buffer = new Buffer(this, id, aDesc.mSize);
  return buffer.forget();
}

void Device::CreateBufferMapped(JSContext* aCx,
                                const dom::GPUBufferDescriptor& aDesc,
                                nsTArray<JS::Value>& aSequence,
                                ErrorResult& aRv) {
  const auto checked = CheckedInt<size_t>(aDesc.mSize);
  if (!checked.isValid()) {
    aRv.ThrowRangeError(u"Mapped size is too large");
    return;
  }
  const auto& size = checked.value();

  // TODO: use `ShmemPool`
  ipc::Shmem shmem;
  if (!mBridge->AllocShmem(size, ipc::Shmem::SharedMemory::TYPE_BASIC,
                           &shmem)) {
    aRv.ThrowDOMException(
        NS_ERROR_DOM_ABORT_ERR,
        nsPrintfCString("Unable to allocate shmem of size %" PRIuPTR, size));
    return;
  }

  // zero out memory
  memset(shmem.get<uint8_t>(), 0, size);

  JS::Rooted<JSObject*> arrayBuffer(
      aCx, CreateExternalArrayBuffer(aCx, size, shmem));
  if (!arrayBuffer) {
    aRv.NoteJSContextException(aCx);
    return;
  }

  dom::GPUBufferDescriptor modifiedDesc(aDesc);
  modifiedDesc.mUsage |= dom::GPUBufferUsage_Binding::MAP_WRITE;
  RawId id = mBridge->DeviceCreateBuffer(mId, modifiedDesc);
  RefPtr<Buffer> buffer = new Buffer(this, id, aDesc.mSize);

  JS::Rooted<JS::Value> bufferValue(aCx);
  if (!dom::ToJSValue(aCx, buffer, &bufferValue)) {
    aRv.NoteJSContextException(aCx);
    return;
  }

  aSequence.AppendElement(bufferValue);
  aSequence.AppendElement(JS::ObjectValue(*arrayBuffer));

  buffer->InitMapping(std::move(shmem), arrayBuffer);
}

RefPtr<MappingPromise> Device::MapBufferForReadAsync(RawId aId, size_t aSize,
                                                     ErrorResult& aRv) {
  ipc::Shmem shmem;
  if (!mBridge->AllocShmem(aSize, ipc::Shmem::SharedMemory::TYPE_BASIC,
                           &shmem)) {
    aRv.ThrowDOMException(
        NS_ERROR_DOM_ABORT_ERR,
        nsPrintfCString("Unable to allocate shmem of size %" PRIuPTR, aSize));
    return nullptr;
  }

  return mBridge->SendDeviceMapBufferRead(mId, aId, std::move(shmem));
}

void Device::UnmapBuffer(RawId aId, UniquePtr<ipc::Shmem> aShmem) {
  mBridge->SendDeviceUnmapBuffer(mId, aId, std::move(*aShmem));
}

void Device::DestroyBuffer(RawId aId) {
  if (mBridge && mBridge->IsOpen()) {
    mBridge->SendBufferDestroy(aId);
  }
}

}  // namespace webgpu
}  // namespace mozilla

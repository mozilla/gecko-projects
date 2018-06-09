/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// This file has the logic which the middleman process uses to send messages to
// the UI process with painting data from the child process.

#include "ParentInternal.h"

#include "chrome/common/mach_ipc_mac.h"
#include "mozilla/dom/TabChild.h"
#include "mozilla/layers/CompositorBridgeChild.h"
#include "mozilla/layers/ImageDataSerializer.h"
#include "mozilla/layers/LayerTransactionChild.h"
#include "mozilla/layers/PTextureChild.h"

#include <mach/mach_vm.h>

namespace mozilla {
namespace recordreplay {
namespace parent {

void* gGraphicsMemory;

static mach_port_t gGraphicsPort;
static ReceivePort* gGraphicsReceiver;

void
InitializeGraphicsMemory()
{
  mach_vm_address_t address;
  kern_return_t kr = mach_vm_allocate(mach_task_self(), &address,
                                      GraphicsMemorySize, VM_FLAGS_ANYWHERE);
  MOZ_RELEASE_ASSERT(kr == KERN_SUCCESS);

  memory_object_size_t memoryObjectSize = GraphicsMemorySize;
  kr = mach_make_memory_entry_64(mach_task_self(),
                                 &memoryObjectSize,
                                 address,
                                 VM_PROT_DEFAULT,
                                 &gGraphicsPort,
                                 MACH_PORT_NULL);
  MOZ_RELEASE_ASSERT(kr == KERN_SUCCESS);
  MOZ_RELEASE_ASSERT(memoryObjectSize == GraphicsMemorySize);

  gGraphicsMemory = (void*) address;
  gGraphicsReceiver = new ReceivePort(nsPrintfCString("WebReplay.%d", getpid()).get());
}

void
SendGraphicsMemoryToChild()
{
  MachReceiveMessage handshakeMessage;
  kern_return_t kr = gGraphicsReceiver->WaitForMessage(&handshakeMessage, 0);
  MOZ_RELEASE_ASSERT(kr == KERN_SUCCESS);

  MOZ_RELEASE_ASSERT(handshakeMessage.GetMessageID() == GraphicsHandshakeMessageId);
  mach_port_t childPort = handshakeMessage.GetTranslatedPort(0);
  MOZ_RELEASE_ASSERT(childPort != MACH_PORT_NULL);

  MachSendMessage message(GraphicsMemoryMessageId);
  message.AddDescriptor(MachMsgPortDescriptor(gGraphicsPort, MACH_MSG_TYPE_COPY_SEND));

  MachPortSender sender(childPort);
  kr = sender.SendMessage(message, 1000);
  MOZ_RELEASE_ASSERT(kr == KERN_SUCCESS);
}

static void
UpdateBrowserTitle(dom::TabChild* aBrowser)
{
  AutoSafeJSContext cx;

  nsString message;
  message.Append(u"DOMTitleChanged");

  const char* title = ActiveChildIsRecording() ? "RECORDING" : "REPLAYING";
  JS::RootedString str(cx, JS_NewStringCopyZ(cx, title));
  if (!str) {
    return;
  }
  JS::RootedValue strValue(cx, JS::StringValue(str));

  JS::RootedObject jsonObject(cx, JS_NewObject(cx, nullptr));
  if (!jsonObject || !JS_DefineProperty(cx, jsonObject, "title", strValue, JSPROP_ENUMERATE)) {
    return;
  }

  JS::RootedValue jsonValue(cx, JS::ObjectValue(*jsonObject));
  JS::RootedValue transferValue(cx);

  dom::ipc::StructuredCloneData data;
  {
    ErrorResult rv;
    data.Write(cx, jsonValue, transferValue, rv);
    MOZ_RELEASE_ASSERT(!rv.Failed());
  }

  {
    nsresult rv = aBrowser->DoSendAsyncMessage(cx, message, data, nullptr, nullptr);
    MOZ_RELEASE_ASSERT(!NS_FAILED(rv));
  }
}

// Information about a layer tree we have retained for future paints.
struct LayerTreeInfo
{
  layers::LayersId mLayerTreeId;
  layers::PLayerTransactionChild* mLayerTransactionChild;

  uint64_t mLayerObserverEpoch;

  // Action to clean up the current paint, to be performed after the next paint.
  std::function<void()> mDestroyAction;
};
static StaticInfallibleVector<LayerTreeInfo> gLayerTrees;

static LayerTreeInfo*
GetLayerTreeInfo(dom::TabChild* aBrowser)
{
  for (LayerTreeInfo& existing : gLayerTrees) {
    if (existing.mLayerTreeId == aBrowser->GetLayersId()) {
      if (existing.mLayerObserverEpoch != aBrowser->LayerObserverEpoch()) {
        existing.mLayerObserverEpoch = aBrowser->LayerObserverEpoch();
        existing.mLayerTransactionChild->SendSetLayerObserverEpoch(existing.mLayerObserverEpoch);
      }
      return &existing;
    }
  }

  gLayerTrees.emplaceBack();
  LayerTreeInfo* layersInfo = &gLayerTrees.back();

  layersInfo->mLayerTreeId = aBrowser->GetLayersId();

  nsTArray<layers::LayersBackend> backends;
  backends.AppendElement(layers::LayersBackend::LAYERS_BASIC);

  layers::CompositorBridgeChild* CBC = layers::CompositorBridgeChild::Get();
  layersInfo->mLayerTransactionChild =
    CBC->SendPLayerTransactionConstructor(backends, aBrowser->GetLayersId());
  MOZ_RELEASE_ASSERT(layersInfo->mLayerTransactionChild);

  layersInfo->mLayerObserverEpoch = aBrowser->LayerObserverEpoch();
  layersInfo->mLayerTransactionChild->SendSetLayerObserverEpoch(layersInfo->mLayerObserverEpoch);

  return layersInfo;
}

#define TRY(op) do { if (!(op)) MOZ_CRASH(#op); } while (false)

static void
UpdateBrowserGraphics(dom::TabChild* aBrowser, const PaintMessage& aMsg)
{
  layers::CompositorBridgeChild* CBC = layers::CompositorBridgeChild::Get();

  LayerTreeInfo* layersInfo = GetLayerTreeInfo(aBrowser);
  layers::PLayerTransactionChild* LTC = layersInfo->mLayerTransactionChild;

  gfx::IntSize size(aMsg.mWidth, aMsg.mHeight);
  size_t bufferSize = layers::ImageDataSerializer::ComputeRGBBufferSize(size, gSurfaceFormat);

  ipc::Shmem shmem;
  TRY(CBC->AllocShmem(bufferSize, ipc::SharedMemory::TYPE_BASIC, &shmem));

  memcpy(shmem.get<char>(), gGraphicsMemory, bufferSize);

  size_t width = aMsg.mWidth;
  size_t height = aMsg.mHeight;

  layers::BufferDescriptor bufferDesc =
    layers::RGBDescriptor(gfx::IntSize(width, height), gSurfaceFormat,
                          /* hasIntermediateBuffer = */ false);
  layers::SurfaceDescriptor surfaceDesc =
    layers::SurfaceDescriptorBuffer(bufferDesc, layers::MemoryOrShmem(shmem));

  static uint64_t gTextureSerial = 0;

  wr::MaybeExternalImageId externalImageId;
  layers::PTextureChild* texture =
    CBC->CreateTexture(surfaceDesc,
                       layers::ReadLockDescriptor(null_t()),
                       layers::LayersBackend::LAYERS_BASIC,
                       layers::TextureFlags::DISALLOW_BIGIMAGE |
                       layers::TextureFlags::IMMEDIATE_UPLOAD,
                       ++gTextureSerial,
                       externalImageId,
                       nullptr);
  MOZ_RELEASE_ASSERT(texture);

  static uint64_t gCompositableId = 0;
  layers::CompositableHandle ContentCompositable(++gCompositableId);

  TRY(LTC->SendNewCompositable(ContentCompositable,
                               layers::TextureInfo(layers::CompositableType::CONTENT_TILED)));

  static uint64_t gLayerId = 0;
  layers::LayerHandle RootLayer(++gLayerId);
  layers::LayerHandle ContentLayer(++gLayerId);

  nsTArray<layers::Edit> cset;
  cset.AppendElement(layers::OpCreateContainerLayer(RootLayer));
  cset.AppendElement(layers::OpCreatePaintedLayer(ContentLayer));
  cset.AppendElement(layers::OpSetRoot(RootLayer));
  cset.AppendElement(layers::OpPrependChild(RootLayer, ContentLayer));
  cset.AppendElement(layers::OpAttachCompositable(ContentLayer, ContentCompositable));

  nsTArray<layers::OpSetLayerAttributes> setAttrs;
  setAttrs.AppendElement(layers::OpSetLayerAttributes(RootLayer,
                           layers::LayerAttributes(
                             layers::CommonLayerAttributes(
                               LayerIntRegion(LayerIntRect(0, 0, width, height)),
                               layers::EventRegions(),
                               /* useClipRect = */ false,
                               ParentLayerIntRect(),
                               layers::LayerHandle(0),
                               nsTArray<layers::LayerHandle>(),
                               layers::CompositorAnimations(
                                 nsTArray<layers::Animation>(),
                                 0),
                               nsIntRegion(),
                               nsTArray<layers::ScrollMetadata>(),
                               nsCString()),
                             layers::ContainerLayerAttributes(1, 1, 1, 1, 1, false))));
  setAttrs.AppendElement(layers::OpSetLayerAttributes(ContentLayer,
                           layers::LayerAttributes(
                             layers::CommonLayerAttributes(
                               LayerIntRegion(LayerIntRect(0, 0, width, height)),
                               layers::EventRegions(),
                               /* useClipRect = */ false,
                               ParentLayerIntRect(),
                               layers::LayerHandle(0),
                               nsTArray<layers::LayerHandle>(),
                               layers::CompositorAnimations(
                                 nsTArray<layers::Animation>(),
                                 0),
                               nsIntRegion(),
                               nsTArray<layers::ScrollMetadata>(),
                               nsCString()),
                             layers::PaintedLayerAttributes(nsIntRegion(gfx::IntRect(0, 0, width, height))))));

  nsTArray<layers::TileDescriptor> tiles;
  tiles.AppendElement(layers::TexturedTileDescriptor(nullptr, texture,
                                                     layers::MaybeTexture(null_t()),
                                                     gfx::IntRect(0, 0, width, height),
                                                     /* readLocked = */ false,
                                                     /* readLockedOnWhite = */ false,
                                                     /* wasPlaceholder = */ false));

  layers::SurfaceDescriptorTiles tileSurface(nsIntRegion(gfx::IntRect(0, 0, width, height)),
                                             tiles,
                                             gfx::IntPoint(0, 0),
                                             gfx::IntSize(width, height),
                                             /* firstTileX = */ 0,
                                             /* firstTileY = */ 0,
                                             /* retainedWidth = */ 1,
                                             /* retainedHeight = */ 1,
                                             /* resolution = */ 1,
                                             /* frameXResolution = */ 2,
                                             /* frameYResolution = */ 2,
                                             /* isProgressive = */ false);

  nsTArray<layers::CompositableOperation> paints;
  paints.AppendElement(layers::CompositableOperation(ContentCompositable,
                                                     layers::OpUseTiledLayerBuffer(tileSurface)));

  nsTArray<layers::OpDestroy> destroy;

  TimeStamp now = TimeStamp::Now();

  static uint64_t FwdTransactionId = 2;
  static layers::TransactionId TransactionId = { 1 };
  static uint32_t PaintSequenceNumber = 0;

  layers::TargetConfig targetConfig(gfx::IntRect(0, 0, width, height),
                                    ROTATION_0,
                                    dom::eScreenOrientation_None,
                                    gfx::IntRect(0, 0, width, height));

  layers::TransactionInfo txn(cset,
                              nsTArray<layers::OpSetSimpleLayerAttributes>(),
                              setAttrs,
                              paints,
                              destroy,
                              FwdTransactionId,
                              TransactionId,
                              targetConfig,
                              nsTArray<layers::PluginWindowData>(),
                              /* isFirstPaint = */ true,
                              layers::FocusTarget(),
                              /* scheduleComposite = */ true,
                              PaintSequenceNumber,
                              /* isRepeatTransaction = */ false,
                              now,
                              TimeStamp());
  TRY(LTC->SendUpdate(txn));
  TRY(aBrowser->SendForcePaintNoOp(aBrowser->LayerObserverEpoch()));

  if (layersInfo->mDestroyAction) {
    layersInfo->mDestroyAction();
  }

  layersInfo->mDestroyAction = [=]() {
    TRY(texture->SendDestroy());
    TRY(LTC->SendReleaseLayer(RootLayer));
    TRY(LTC->SendReleaseLayer(ContentLayer));
    TRY(LTC->SendReleaseCompositable(ContentCompositable));
  };

  FwdTransactionId++;
  TransactionId = TransactionId.Next();
  PaintSequenceNumber++;
}

static void
ClearBrowserGraphics(dom::TabChild* aBrowser)
{
  LayerTreeInfo* layersInfo = GetLayerTreeInfo(aBrowser);
  layers::PLayerTransactionChild* LTC = layersInfo->mLayerTransactionChild;

  LTC->SendClearCachedResources();
}

static Maybe<PaintMessage> gLastPaint;

void
UpdateGraphicsInUIProcess(const PaintMessage* aMsg)
{
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

  if (aMsg) {
    gLastPaint = Some(*aMsg);
  } else if (!gLastPaint.isSome()) {
    return;
  }

  nsTArray<dom::PBrowserChild*> browsers;
  dom::ContentChild::GetSingleton()->ManagedPBrowserChild(browsers);

  // There might be multiple visible browsers in existence, and there doesn't
  // seem to be an obvious way to determine which we are supposed to paint to
  // in order to update our tab in the UI process. Until a better approach
  // presents itself, just paint to all the visible browsers.
  for (size_t i = 0; i < browsers.Length(); i++) {
    dom::TabChild* browser = static_cast<dom::TabChild*>(browsers[i]);
    if (browser->WebWidget()->IsVisible()) {
      UpdateBrowserTitle(browser);
      UpdateBrowserGraphics(browser, gLastPaint.ref());
    } else {
      ClearBrowserGraphics(browser);
    }
  }
}

#undef TRY

} // namespace parent
} // namespace recordreplay
} // namespace mozilla

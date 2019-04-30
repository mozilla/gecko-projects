/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_tabs_BrowserParent_h
#define mozilla_tabs_BrowserParent_h

#include "js/TypeDecls.h"
#include "LiveResizeListener.h"
#include "mozilla/ContentCache.h"
#include "mozilla/dom/ipc/IdType.h"
#include "mozilla/dom/PBrowserParent.h"
#include "mozilla/dom/PContent.h"
#include "mozilla/dom/PFilePickerParent.h"
#include "mozilla/dom/TabContext.h"
#include "mozilla/EventForwards.h"
#include "mozilla/dom/File.h"
#include "mozilla/gfx/CrossProcessPaint.h"
#include "mozilla/layers/CompositorBridgeParent.h"
#include "mozilla/layout/RenderFrame.h"
#include "mozilla/RefPtr.h"
#include "mozilla/Move.h"
#include "nsCOMPtr.h"
#include "nsIAuthPromptProvider.h"
#include "nsIBrowserDOMWindow.h"
#include "nsIDOMEventListener.h"
#include "nsIKeyEventInPluginCallback.h"
#include "nsIRemoteTab.h"
#include "nsIXULBrowserWindow.h"
#include "nsRefreshDriver.h"
#include "nsWeakReference.h"
#include "Units.h"
#include "nsIWidget.h"

class nsFrameLoader;
class nsIContent;
class nsIPrincipal;
class nsIURI;
class nsILoadContext;
class nsIDocShell;
class nsIWebBrowserPersistDocumentReceiver;

namespace mozilla {

namespace a11y {
class DocAccessibleParent;
}

namespace jsipc {
class CpowHolder;
}  // namespace jsipc

namespace layers {
struct TextureFactoryIdentifier;
}  // namespace layers

namespace widget {
struct IMENotification;
}  // namespace widget

namespace gfx {
class SourceSurface;
class DataSourceSurface;
}  // namespace gfx

namespace dom {

class CanonicalBrowsingContext;
class ClonedMessageData;
class ContentParent;
class Element;
class DataTransfer;
class BrowserBridgeParent;

namespace ipc {
class StructuredCloneData;
}  // namespace ipc

class BrowserParent final : public PBrowserParent,
                            public nsIDOMEventListener,
                            public nsIRemoteTab,
                            public nsIAuthPromptProvider,
                            public nsIKeyEventInPluginCallback,
                            public nsSupportsWeakReference,
                            public TabContext,
                            public LiveResizeListener {
  typedef mozilla::dom::ClonedMessageData ClonedMessageData;

  friend class PBrowserParent;
  friend class BrowserBridgeParent;  // for clearing mBrowserBridgeParent

  virtual ~BrowserParent();

 public:
  // Helper class for ContentParent::RecvCreateWindow.
  struct AutoUseNewTab;

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_NSIAUTHPROMPTPROVIDER
  // nsIRemoteTab
  NS_DECL_NSIREMOTETAB
  // nsIDOMEventListener interfaces
  NS_DECL_NSIDOMEVENTLISTENER

  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(BrowserParent, nsIRemoteTab)

  BrowserParent(ContentParent* aManager, const TabId& aTabId,
                const TabContext& aContext,
                CanonicalBrowsingContext* aBrowsingContext,
                uint32_t aChromeFlags,
                BrowserBridgeParent* aBrowserBridgeParent = nullptr);

  Element* GetOwnerElement() const { return mFrameElement; }
  already_AddRefed<nsPIDOMWindowOuter> GetParentWindowOuter();

  void SetOwnerElement(Element* aElement);

  void CacheFrameLoader(nsFrameLoader* aFrameLoader);

  nsIBrowserDOMWindow* GetBrowserDOMWindow() const { return mBrowserDOMWindow; }

  void SetBrowserDOMWindow(nsIBrowserDOMWindow* aBrowserDOMWindow) {
    mBrowserDOMWindow = aBrowserDOMWindow;
  }

  void SetHasContentOpener(bool aHasContentOpener);

  void SwapFrameScriptsFrom(nsTArray<FrameScriptInfo>& aFrameScripts) {
    aFrameScripts.SwapElements(mDelayedFrameScripts);
  }

  already_AddRefed<nsILoadContext> GetLoadContext();

  already_AddRefed<nsIWidget> GetTopLevelWidget();

  nsIXULBrowserWindow* GetXULBrowserWindow();

  CanonicalBrowsingContext* GetBrowsingContext() { return mBrowsingContext; }

  void Destroy();

  void RemoveWindowListeners();

  void AddWindowListeners();

  mozilla::ipc::IPCResult RecvMoveFocus(const bool& aForward,
                                        const bool& aForDocumentNavigation);

  mozilla::ipc::IPCResult RecvSizeShellTo(const uint32_t& aFlags,
                                          const int32_t& aWidth,
                                          const int32_t& aHeight,
                                          const int32_t& aShellItemWidth,
                                          const int32_t& aShellItemHeight);

  mozilla::ipc::IPCResult RecvDropLinks(nsTArray<nsString>&& aLinks);

  mozilla::ipc::IPCResult RecvEvent(const RemoteDOMEvent& aEvent);

  mozilla::ipc::IPCResult RecvReplyKeyEvent(const WidgetKeyboardEvent& aEvent);

  mozilla::ipc::IPCResult RecvAccessKeyNotHandled(
      const WidgetKeyboardEvent& aEvent);

  mozilla::ipc::IPCResult RecvSetHasBeforeUnload(const bool& aHasBeforeUnload);

  mozilla::ipc::IPCResult RecvRegisterProtocolHandler(const nsString& aScheme,
                                                      nsIURI* aHandlerURI,
                                                      const nsString& aTitle,
                                                      nsIURI* aDocURI);

  mozilla::ipc::IPCResult RecvOnProgressChange(
      const Maybe<WebProgressData>& aWebProgressData,
      const RequestData& aRequestData, const int32_t aCurSelfProgress,
      const int32_t aMaxSelfProgress, const int32_t aCurTotalProgres,
      const int32_t aMaxTotalProgress);

  mozilla::ipc::IPCResult RecvOnStatusChange(
      const Maybe<WebProgressData>& aWebProgressData,
      const RequestData& aRequestData, const nsresult aStatus,
      const nsString& aMessage);

  mozilla::ipc::IPCResult RecvOnContentBlockingEvent(
      const Maybe<WebProgressData>& aWebProgressData,
      const RequestData& aRequestData, const uint32_t& aEvent);

  void ReconstructWebProgressAndRequest(
      nsIWebProgress* aManager, const Maybe<WebProgressData>& aWebProgressData,
      const RequestData& aRequestData,
      nsCOMPtr<nsIWebProgress>& aOutWebProgress,
      nsCOMPtr<nsIRequest>& aOutRequest);

  mozilla::ipc::IPCResult RecvBrowserFrameOpenWindow(
      PBrowserParent* aOpener, const nsString& aURL, const nsString& aName,
      bool aForceNoReferrer, const nsString& aFeatures,
      BrowserFrameOpenWindowResolver&& aResolve);

  mozilla::ipc::IPCResult RecvSyncMessage(
      const nsString& aMessage, const ClonedMessageData& aData,
      InfallibleTArray<CpowEntry>&& aCpows, nsIPrincipal* aPrincipal,
      nsTArray<ipc::StructuredCloneData>* aRetVal);

  mozilla::ipc::IPCResult RecvRpcMessage(
      const nsString& aMessage, const ClonedMessageData& aData,
      InfallibleTArray<CpowEntry>&& aCpows, nsIPrincipal* aPrincipal,
      nsTArray<ipc::StructuredCloneData>* aRetVal);

  mozilla::ipc::IPCResult RecvAsyncMessage(const nsString& aMessage,
                                           InfallibleTArray<CpowEntry>&& aCpows,
                                           nsIPrincipal* aPrincipal,
                                           const ClonedMessageData& aData);

  mozilla::ipc::IPCResult RecvNotifyIMEFocus(
      const ContentCache& aContentCache,
      const widget::IMENotification& aEventMessage,
      NotifyIMEFocusResolver&& aResolve);

  mozilla::ipc::IPCResult RecvNotifyIMETextChange(
      const ContentCache& aContentCache,
      const widget::IMENotification& aEventMessage);

  mozilla::ipc::IPCResult RecvNotifyIMECompositionUpdate(
      const ContentCache& aContentCache,
      const widget::IMENotification& aEventMessage);

  mozilla::ipc::IPCResult RecvNotifyIMESelection(
      const ContentCache& aContentCache,
      const widget::IMENotification& aEventMessage);

  mozilla::ipc::IPCResult RecvUpdateContentCache(
      const ContentCache& aContentCache);

  mozilla::ipc::IPCResult RecvNotifyIMEMouseButtonEvent(
      const widget::IMENotification& aEventMessage, bool* aConsumedByIME);

  mozilla::ipc::IPCResult RecvNotifyIMEPositionChange(
      const ContentCache& aContentCache,
      const widget::IMENotification& aEventMessage);

  mozilla::ipc::IPCResult RecvOnEventNeedingAckHandled(
      const EventMessage& aMessage);

  mozilla::ipc::IPCResult RecvRequestIMEToCommitComposition(
      const bool& aCancel, bool* aIsCommitted, nsString* aCommittedString);

  mozilla::ipc::IPCResult RecvStartPluginIME(
      const WidgetKeyboardEvent& aKeyboardEvent, const int32_t& aPanelX,
      const int32_t& aPanelY, nsString* aCommitted);

  mozilla::ipc::IPCResult RecvSetPluginFocused(const bool& aFocused);

  mozilla::ipc::IPCResult RecvSetCandidateWindowForPlugin(
      const widget::CandidateWindowPosition& aPosition);
  mozilla::ipc::IPCResult RecvEnableIMEForPlugin(const bool& aEnable);

  mozilla::ipc::IPCResult RecvDefaultProcOfPluginEvent(
      const WidgetPluginEvent& aEvent);

  mozilla::ipc::IPCResult RecvGetInputContext(widget::IMEState* aIMEState);

  mozilla::ipc::IPCResult RecvSetInputContext(
      const widget::InputContext& aContext,
      const widget::InputContextAction& aAction);

  // See nsIKeyEventInPluginCallback
  virtual void HandledWindowedPluginKeyEvent(
      const NativeEventData& aKeyEventData, bool aIsConsumed) override;

  mozilla::ipc::IPCResult RecvOnWindowedPluginKeyEvent(
      const NativeEventData& aKeyEventData);

  mozilla::ipc::IPCResult RecvRequestFocus(const bool& aCanRaise);

  mozilla::ipc::IPCResult RecvLookUpDictionary(
      const nsString& aText, nsTArray<mozilla::FontRange>&& aFontRangeArray,
      const bool& aIsVertical, const LayoutDeviceIntPoint& aPoint);

  mozilla::ipc::IPCResult RecvEnableDisableCommands(
      const nsString& aAction, nsTArray<nsCString>&& aEnabledCommands,
      nsTArray<nsCString>&& aDisabledCommands);

  mozilla::ipc::IPCResult RecvSetCursor(
      const nsCursor& aValue, const bool& aHasCustomCursor,
      const nsCString& aUri, const uint32_t& aWidth, const uint32_t& aHeight,
      const uint32_t& aStride, const gfx::SurfaceFormat& aFormat,
      const uint32_t& aHotspotX, const uint32_t& aHotspotY, const bool& aForce);

  mozilla::ipc::IPCResult RecvSetStatus(const uint32_t& aType,
                                        const nsString& aStatus);

  mozilla::ipc::IPCResult RecvShowTooltip(const uint32_t& aX,
                                          const uint32_t& aY,
                                          const nsString& aTooltip,
                                          const nsString& aDirection);

  mozilla::ipc::IPCResult RecvHideTooltip();

  mozilla::ipc::IPCResult RecvSetNativeChildOfShareableWindow(
      const uintptr_t& childWindow);

  mozilla::ipc::IPCResult RecvDispatchFocusToTopLevelWindow();

  mozilla::ipc::IPCResult RecvRespondStartSwipeEvent(
      const uint64_t& aInputBlockId, const bool& aStartSwipe);

  mozilla::ipc::IPCResult RecvDispatchWheelEvent(
      const mozilla::WidgetWheelEvent& aEvent);

  mozilla::ipc::IPCResult RecvDispatchMouseEvent(
      const mozilla::WidgetMouseEvent& aEvent);

  mozilla::ipc::IPCResult RecvDispatchKeyboardEvent(
      const mozilla::WidgetKeyboardEvent& aEvent);

  PColorPickerParent* AllocPColorPickerParent(const nsString& aTitle,
                                              const nsString& aInitialColor);

  bool DeallocPColorPickerParent(PColorPickerParent* aColorPicker);

  PDocAccessibleParent* AllocPDocAccessibleParent(PDocAccessibleParent*,
                                                  const uint64_t&,
                                                  const uint32_t&,
                                                  const IAccessibleHolder&);

  bool DeallocPDocAccessibleParent(PDocAccessibleParent*);

  virtual mozilla::ipc::IPCResult RecvPDocAccessibleConstructor(
      PDocAccessibleParent* aDoc, PDocAccessibleParent* aParentDoc,
      const uint64_t& aParentID, const uint32_t& aMsaaID,
      const IAccessibleHolder& aDocCOMProxy) override;

  /**
   * Return the top level doc accessible parent for this tab.
   */
  a11y::DocAccessibleParent* GetTopLevelDocAccessible() const;

  PWindowGlobalParent* AllocPWindowGlobalParent(const WindowGlobalInit& aInit);

  bool DeallocPWindowGlobalParent(PWindowGlobalParent* aActor);

  virtual mozilla::ipc::IPCResult RecvPWindowGlobalConstructor(
      PWindowGlobalParent* aActor, const WindowGlobalInit& aInit) override;

  PBrowserBridgeParent* AllocPBrowserBridgeParent(
      const nsString& aPresentationURL, const nsString& aRemoteType,
      BrowsingContext* aBrowsingContext, const uint32_t& aChromeFlags);

  bool DeallocPBrowserBridgeParent(PBrowserBridgeParent* aActor);

  virtual mozilla::ipc::IPCResult RecvPBrowserBridgeConstructor(
      PBrowserBridgeParent* aActor, const nsString& aPresentationURL,
      const nsString& aRemoteType, BrowsingContext* aBrowsingContext,
      const uint32_t& aChromeFlags) override;

  void LoadURL(nsIURI* aURI);

  void ResumeLoad(uint64_t aPendingSwitchID);

  void InitRendering();
  void MaybeShowFrame();

  // XXX/cjones: it's not clear what we gain by hiding these
  // message-sending functions under a layer of indirection and
  // eating the return values
  void Show(const ScreenIntSize& aSize, bool aParentIsActive);

  void UpdateDimensions(const nsIntRect& aRect, const ScreenIntSize& aSize);

  DimensionInfo GetDimensionInfo();

  nsresult UpdatePosition();

  void SizeModeChanged(const nsSizeMode& aSizeMode);

  void UIResolutionChanged();

  void ThemeChanged();

  void HandleAccessKey(const WidgetKeyboardEvent& aEvent,
                       nsTArray<uint32_t>& aCharCodes);

  void Activate();

  void Deactivate();

  bool MapEventCoordinatesForChildProcess(mozilla::WidgetEvent* aEvent);

  void MapEventCoordinatesForChildProcess(const LayoutDeviceIntPoint& aOffset,
                                          mozilla::WidgetEvent* aEvent);

  LayoutDeviceToCSSScale GetLayoutDeviceToCSSScale();

  mozilla::ipc::IPCResult RecvRequestNativeKeyBindings(
      const uint32_t& aType, const mozilla::WidgetKeyboardEvent& aEvent,
      nsTArray<mozilla::CommandInt>* aCommands);

  mozilla::ipc::IPCResult RecvSynthesizeNativeKeyEvent(
      const int32_t& aNativeKeyboardLayout, const int32_t& aNativeKeyCode,
      const uint32_t& aModifierFlags, const nsString& aCharacters,
      const nsString& aUnmodifiedCharacters, const uint64_t& aObserverId);

  mozilla::ipc::IPCResult RecvSynthesizeNativeMouseEvent(
      const LayoutDeviceIntPoint& aPoint, const uint32_t& aNativeMessage,
      const uint32_t& aModifierFlags, const uint64_t& aObserverId);

  mozilla::ipc::IPCResult RecvSynthesizeNativeMouseMove(
      const LayoutDeviceIntPoint& aPoint, const uint64_t& aObserverId);

  mozilla::ipc::IPCResult RecvSynthesizeNativeMouseScrollEvent(
      const LayoutDeviceIntPoint& aPoint, const uint32_t& aNativeMessage,
      const double& aDeltaX, const double& aDeltaY, const double& aDeltaZ,
      const uint32_t& aModifierFlags, const uint32_t& aAdditionalFlags,
      const uint64_t& aObserverId);

  mozilla::ipc::IPCResult RecvSynthesizeNativeTouchPoint(
      const uint32_t& aPointerId, const TouchPointerState& aPointerState,
      const LayoutDeviceIntPoint& aPoint, const double& aPointerPressure,
      const uint32_t& aPointerOrientation, const uint64_t& aObserverId);

  mozilla::ipc::IPCResult RecvSynthesizeNativeTouchTap(
      const LayoutDeviceIntPoint& aPoint, const bool& aLongTap,
      const uint64_t& aObserverId);

  mozilla::ipc::IPCResult RecvClearNativeTouchSequence(
      const uint64_t& aObserverId);

  mozilla::ipc::IPCResult RecvSetPrefersReducedMotionOverrideForTest(
      const bool& aValue);
  mozilla::ipc::IPCResult RecvResetPrefersReducedMotionOverrideForTest();

  void SendMouseEvent(const nsAString& aType, float aX, float aY,
                      int32_t aButton, int32_t aClickCount, int32_t aModifiers,
                      bool aIgnoreRootScrollFrame);

  /**
   * The following Send*Event() marks aEvent as posted to remote process if
   * it succeeded.  So, you can check the result with
   * aEvent.HasBeenPostedToRemoteProcess().
   */
  void SendRealMouseEvent(WidgetMouseEvent& aEvent);

  void SendRealDragEvent(WidgetDragEvent& aEvent, uint32_t aDragAction,
                         uint32_t aDropEffect, nsIPrincipal* aPrincipal);

  void SendMouseWheelEvent(WidgetWheelEvent& aEvent);

  void SendRealKeyEvent(WidgetKeyboardEvent& aEvent);

  void SendRealTouchEvent(WidgetTouchEvent& aEvent);

  void SendPluginEvent(WidgetPluginEvent& aEvent);

  /**
   * Different from above Send*Event(), these methods return true if the
   * event has been posted to the remote process or failed to do that but
   * shouldn't be handled by following event listeners.
   * If you need to check if it's actually posted to the remote process,
   * you can refer aEvent.HasBeenPostedToRemoteProcess().
   */
  bool SendCompositionEvent(mozilla::WidgetCompositionEvent& aEvent);

  bool SendSelectionEvent(mozilla::WidgetSelectionEvent& aEvent);

  bool SendHandleTap(TapType aType, const LayoutDevicePoint& aPoint,
                     Modifiers aModifiers, const ScrollableLayerGuid& aGuid,
                     uint64_t aInputBlockId);

  PFilePickerParent* AllocPFilePickerParent(const nsString& aTitle,
                                            const int16_t& aMode);

  bool DeallocPFilePickerParent(PFilePickerParent* actor);

  mozilla::ipc::IPCResult RecvIndexedDBPermissionRequest(
      nsIPrincipal* aPrincipal, IndexedDBPermissionRequestResolver&& aResolve);

  bool GetGlobalJSObject(JSContext* cx, JSObject** globalp);

  void StartPersistence(uint64_t aOuterWindowID,
                        nsIWebBrowserPersistDocumentReceiver* aRecv,
                        ErrorResult& aRv);

  bool HandleQueryContentEvent(mozilla::WidgetQueryContentEvent& aEvent);

  bool SendPasteTransferable(const IPCDataTransfer& aDataTransfer,
                             const bool& aIsPrivateData,
                             nsIPrincipal* aRequestingPrincipal,
                             const uint32_t& aContentPolicyType);

  // Call from LayoutStatics only
  static void InitializeStatics();

  /**
   * Returns the focused BrowserParent or nullptr if chrome or another app
   * is focused.
   */
  static BrowserParent* GetFocused();

  static BrowserParent* GetFrom(nsFrameLoader* aFrameLoader);

  static BrowserParent* GetFrom(nsIRemoteTab* aBrowserParent);

  static BrowserParent* GetFrom(PBrowserParent* aBrowserParent);

  static BrowserParent* GetFrom(nsIContent* aContent);

  static TabId GetTabIdFrom(nsIDocShell* docshell);

  ContentParent* Manager() const { return mManager; }

  /**
   * Let managees query if Destroy() is already called so they don't send out
   * messages when the PBrowser actor is being destroyed.
   */
  bool IsDestroyed() const { return mIsDestroyed; }

  // Returns the closest widget for our frameloader's content.
  already_AddRefed<nsIWidget> GetWidget() const;

  // Returns the top-level widget for our frameloader's document.
  already_AddRefed<nsIWidget> GetDocWidget() const;

  const TabId GetTabId() const { return mTabId; }

  // Helper for transforming a point
  LayoutDeviceIntPoint TransformPoint(
      const LayoutDeviceIntPoint& aPoint,
      const LayoutDeviceToLayoutDeviceMatrix4x4& aMatrix);
  LayoutDevicePoint TransformPoint(
      const LayoutDevicePoint& aPoint,
      const LayoutDeviceToLayoutDeviceMatrix4x4& aMatrix);

  // Transform a coordinate from the parent process coordinate space to the
  // child process coordinate space.
  LayoutDeviceIntPoint TransformParentToChild(
      const LayoutDeviceIntPoint& aPoint);
  LayoutDevicePoint TransformParentToChild(const LayoutDevicePoint& aPoint);

  // Transform a coordinate from the child process coordinate space to the
  // parent process coordinate space.
  LayoutDeviceIntPoint TransformChildToParent(
      const LayoutDeviceIntPoint& aPoint);
  LayoutDevicePoint TransformChildToParent(const LayoutDevicePoint& aPoint);
  LayoutDeviceIntRect TransformChildToParent(const LayoutDeviceIntRect& aRect);

  // Returns the matrix that transforms event coordinates from the coordinate
  // space of the child process to the coordinate space of the parent process.
  LayoutDeviceToLayoutDeviceMatrix4x4 GetChildToParentConversionMatrix();

  void SetChildToParentConversionMatrix(
      const LayoutDeviceToLayoutDeviceMatrix4x4& aMatrix);

  // Returns the offset from the origin of our frameloader's nearest widget to
  // the origin of its layout frame. This offset is used to translate event
  // coordinates relative to the PuppetWidget origin in the child process.
  //
  // GOING AWAY. PLEASE AVOID ADDING CALLERS. Use the above tranformation
  // methods instead.
  LayoutDeviceIntPoint GetChildProcessOffset();

  // Returns the offset from the on-screen origin of our top-level window's
  // widget (including window decorations) to the origin of our frameloader's
  // nearest widget. This offset is used to translate coordinates from the
  // PuppetWidget's origin to absolute screen coordinates in the child.
  LayoutDeviceIntPoint GetClientOffset();

  /**
   * Native widget remoting protocol for use with windowed plugins with e10s.
   */
  PPluginWidgetParent* AllocPPluginWidgetParent();

  bool DeallocPPluginWidgetParent(PPluginWidgetParent* aActor);

  PPaymentRequestParent* AllocPPaymentRequestParent();

  bool DeallocPPaymentRequestParent(PPaymentRequestParent* aActor);

  bool SendLoadRemoteScript(const nsString& aURL,
                            const bool& aRunInGlobalScope);

  void LayerTreeUpdate(const LayersObserverEpoch& aEpoch, bool aActive);

  void RequestRootPaint(gfx::CrossProcessPaint* aPaint, IntRect aRect,
                        float aScale, nscolor aBackgroundColor);
  void RequestSubPaint(gfx::CrossProcessPaint* aPaint, float aScale,
                       nscolor aBackgroundColor);

  mozilla::ipc::IPCResult RecvInvokeDragSession(
      nsTArray<IPCDataTransfer>&& aTransfers, const uint32_t& aAction,
      Maybe<Shmem>&& aVisualDnDData, const uint32_t& aStride,
      const gfx::SurfaceFormat& aFormat, const LayoutDeviceIntRect& aDragRect,
      nsIPrincipal* aPrincipal);

  void AddInitialDnDDataTo(DataTransfer* aDataTransfer,
                           nsIPrincipal** aPrincipal);

  bool TakeDragVisualization(RefPtr<mozilla::gfx::SourceSurface>& aSurface,
                             LayoutDeviceIntRect* aDragRect);

  layout::RenderFrame* GetRenderFrame();

  // Returns the BrowserBridgeParent if this BrowserParent is for an
  // out-of-process iframe and nullptr otherwise.
  BrowserBridgeParent* GetBrowserBridgeParent() const;

  mozilla::ipc::IPCResult RecvEnsureLayersConnected(
      CompositorOptions* aCompositorOptions);

  // LiveResizeListener implementation
  void LiveResizeStarted() override;
  void LiveResizeStopped() override;

  void SetReadyToHandleInputEvents() { mIsReadyToHandleInputEvents = true; }
  bool IsReadyToHandleInputEvents() { return mIsReadyToHandleInputEvents; }

  static bool AreRecordReplayTabsActive() {
    return gNumActiveRecordReplayTabs != 0;
  }

  void NavigateByKey(bool aForward, bool aForDocumentNavigation);

  ShowInfo GetShowInfo();

 protected:
  bool ReceiveMessage(
      const nsString& aMessage, bool aSync, ipc::StructuredCloneData* aData,
      mozilla::jsipc::CpowHolder* aCpows, nsIPrincipal* aPrincipal,
      nsTArray<ipc::StructuredCloneData>* aJSONRetVal = nullptr);

  mozilla::ipc::IPCResult RecvAsyncAuthPrompt(const nsCString& aUri,
                                              const nsString& aRealm,
                                              const uint64_t& aCallbackId);

  virtual mozilla::ipc::IPCResult Recv__delete__() override;

  virtual void ActorDestroy(ActorDestroyReason why) override;

  nsCOMPtr<Element> mFrameElement;
  nsCOMPtr<nsIBrowserDOMWindow> mBrowserDOMWindow;

  mozilla::ipc::IPCResult RecvRemotePaintIsReady();

  mozilla::ipc::IPCResult RecvNotifyCompositorTransaction();

  mozilla::ipc::IPCResult RecvRemoteIsReadyToHandleInputEvents();

  mozilla::ipc::IPCResult RecvPaintWhileInterruptingJSNoOp(
      const LayersObserverEpoch& aEpoch);

  mozilla::ipc::IPCResult RecvSetDimensions(const uint32_t& aFlags,
                                            const int32_t& aX,
                                            const int32_t& aY,
                                            const int32_t& aCx,
                                            const int32_t& aCy);

  mozilla::ipc::IPCResult RecvShowCanvasPermissionPrompt(
      const nsCString& aFirstPartyURI, const bool& aHideDoorHanger);

  mozilla::ipc::IPCResult RecvSetSystemFont(const nsCString& aFontName);
  mozilla::ipc::IPCResult RecvGetSystemFont(nsCString* aFontName);

  mozilla::ipc::IPCResult RecvVisitURI(const URIParams& aURI,
                                       const Maybe<URIParams>& aLastVisitedURI,
                                       const uint32_t& aFlags);

  mozilla::ipc::IPCResult RecvQueryVisitedState(
      InfallibleTArray<URIParams>&& aURIs);

  ContentCacheInParent mContentCache;

  nsIntRect mRect;
  ScreenIntSize mDimensions;
  hal::ScreenOrientation mOrientation;
  float mDPI;
  int32_t mRounding;
  CSSToLayoutDeviceScale mDefaultScale;
  bool mUpdatedDimensions;
  nsSizeMode mSizeMode;
  LayoutDeviceIntPoint mClientOffset;
  LayoutDeviceIntPoint mChromeOffset;

 private:
  void SuppressDisplayport(bool aEnabled);

  void DestroyInternal();

  void SetRenderLayersInternal(bool aEnabled, bool aForceRepaint);

  already_AddRefed<nsFrameLoader> GetFrameLoader(
      bool aUseCachedFrameLoaderAfterDestroy = false) const;

  RefPtr<ContentParent> mManager;
  void TryCacheDPIAndScale();

  bool AsyncPanZoomEnabled() const;

  // Cached value indicating the docshell active state of the remote browser.
  bool mDocShellIsActive;

  // Update state prior to routing an APZ-aware event to the child process.
  // |aOutTargetGuid| will contain the identifier
  // of the APZC instance that handled the event. aOutTargetGuid may be null.
  // |aOutInputBlockId| will contain the identifier of the input block
  // that this event was added to, if there was one. aOutInputBlockId may be
  // null. |aOutApzResponse| will contain the response that the APZ gave when
  // processing the input block; this is used for generating appropriate
  // pointercancel events.
  void ApzAwareEventRoutingToChild(ScrollableLayerGuid* aOutTargetGuid,
                                   uint64_t* aOutInputBlockId,
                                   nsEventStatus* aOutApzResponse);

  // When true, we've initiated normal shutdown and notified our managing
  // PContent.
  bool mMarkedDestroying;
  // When true, the BrowserParent is invalid and we should not send IPC messages
  // anymore.
  bool mIsDestroyed;

  uint32_t mChromeFlags;

  nsTArray<nsTArray<IPCDataTransferItem>> mInitialDataTransferItems;

  RefPtr<gfx::DataSourceSurface> mDnDVisualization;
  bool mDragValid;
  LayoutDeviceIntRect mDragRect;
  nsCOMPtr<nsIPrincipal> mDragPrincipal;

  nsCOMPtr<nsILoadContext> mLoadContext;

  // We keep a strong reference to the frameloader after we've sent the
  // Destroy message and before we've received __delete__. This allows us to
  // dispatch message manager messages during this time.
  RefPtr<nsFrameLoader> mFrameLoader;

  // The root browsing context loaded in this BrowserParent.
  RefPtr<CanonicalBrowsingContext> mBrowsingContext;

  // Pointer back to BrowserBridgeParent if there is one associated with
  // this BrowserParent. This is non-owning to avoid cycles and is managed
  // by the BrowserBridgeParent instance, which has the strong reference
  // to this BrowserParent.
  BrowserBridgeParent* mBrowserBridgeParent;

  TabId mTabId;

  // When loading a new tab or window via window.open, the child is
  // responsible for loading the URL it wants into the new BrowserChild. When
  // the parent receives the CreateWindow message, though, it sends a LoadURL
  // message, usually for about:blank. It's important for the about:blank load
  // to get processed because the Firefox frontend expects every new window to
  // immediately start loading something (see bug 1123090). However, we want
  // the child to process the LoadURL message before it returns from
  // ProvideWindow so that the URL sent from the parent doesn't override the
  // child's URL. This is not possible using our IPC mechanisms. To solve the
  // problem, we skip sending the LoadURL message in the parent and instead
  // return the URL as a result from CreateWindow. The child simulates
  // receiving a LoadURL message before returning from ProvideWindow.
  //
  // The mCreatingWindow flag is set while dispatching CreateWindow. During
  // that time, any LoadURL calls are skipped and the URL is stored in
  // mSkippedURL.
  bool mCreatingWindow;
  nsCString mDelayedURL;

  // When loading a new tab or window via window.open, we want to ensure that
  // frame scripts for that tab are loaded before any scripts start to run in
  // the window. We can't load the frame scripts the normal way, using
  // separate IPC messages, since they won't be processed by the child until
  // returning to the event loop, which is too late. Instead, we queue up
  // frame scripts that we intend to load and send them as part of the
  // CreateWindow response. Then BrowserChild loads them immediately.
  nsTArray<FrameScriptInfo> mDelayedFrameScripts;

  // Cached cursor setting from BrowserChild.  When the cursor is over the tab,
  // it should take this appearance.
  nsCursor mCursor;
  nsCOMPtr<imgIContainer> mCustomCursor;
  uint32_t mCustomCursorHotspotX, mCustomCursorHotspotY;

  // True if the cursor changes from the BrowserChild should change the widget
  // cursor.  This happens whenever the cursor is in the tab's region.
  bool mTabSetsCursor;

  bool mHasContentOpener;

  // When dropping links we perform a roundtrip from
  // Parent (SendRealDragEvent) -> Child -> Parent (RecvDropLinks)
  // and have to ensure that the child did not modify links to be loaded.
  bool QueryDropLinksForVerification();
  nsTArray<nsString> mVerifyDropLinks;

#ifdef DEBUG
  int32_t mActiveSupressDisplayportCount;
#endif

 private:
  // This is used when APZ needs to find the BrowserParent associated with a
  // layer to dispatch events.
  typedef nsDataHashtable<nsUint64HashKey, BrowserParent*>
      LayerToBrowserParentTable;
  static LayerToBrowserParentTable* sLayerToBrowserParentTable;

  static void AddBrowserParentToTable(layers::LayersId aLayersId,
                                      BrowserParent* aBrowserParent);

  static void RemoveBrowserParentFromTable(layers::LayersId aLayersId);

  // Keeps track of which BrowserParent has keyboard focus
  static StaticAutoPtr<nsTArray<BrowserParent*>> sFocusStack;

  static void PushFocus(BrowserParent* aBrowserParent);

  static void PopFocus(BrowserParent* aBrowserParent);

  layout::RenderFrame mRenderFrame;
  LayersObserverEpoch mLayerTreeEpoch;

  Maybe<LayoutDeviceToLayoutDeviceMatrix4x4> mChildToParentConversionMatrix;

  // If this flag is set, then the tab's layers will be preserved even when
  // the tab's docshell is inactive.
  bool mPreserveLayers;

  // Holds the most recent value passed to the RenderLayers function. This
  // does not necessarily mean that the layers have finished rendering
  // and have uploaded - for that, use mHasLayers.
  bool mRenderLayers;

  // Whether this is active for the ProcessPriorityManager or not.
  bool mActiveInPriorityManager;

  // True if the compositor has reported that the BrowserChild has uploaded
  // layers.
  bool mHasLayers;

  // True if this BrowserParent has had its layer tree sent to the compositor
  // at least once.
  bool mHasPresented;

  // True if at least one window hosted in the BrowserChild has added a
  // beforeunload event listener.
  bool mHasBeforeUnload;

  // True when the remote browser is created and ready to handle input events.
  bool mIsReadyToHandleInputEvents;

  // True if we suppress the eMouseEnterIntoWidget event due to the BrowserChild
  // was not ready to handle it. We will resend it when the next time we fire a
  // mouse event and the BrowserChild is ready.
  bool mIsMouseEnterIntoWidgetEventSuppressed;

  // How many record/replay tabs have active docshells in this process.
  static size_t gNumActiveRecordReplayTabs;

  // Whether this tab is contributing to gNumActiveRecordReplayTabs.
  bool mIsActiveRecordReplayTab;

  // Update whether this is an active record/replay tab.
  void SetIsActiveRecordReplayTab(bool aIsActive);

 public:
  static BrowserParent* GetBrowserParentFromLayersId(
      layers::LayersId aLayersId);
};

struct MOZ_STACK_CLASS BrowserParent::AutoUseNewTab final {
 public:
  AutoUseNewTab(BrowserParent* aNewTab, nsCString* aURLToLoad)
      : mNewTab(aNewTab), mURLToLoad(aURLToLoad) {
    MOZ_ASSERT(!aNewTab->mCreatingWindow);

    aNewTab->mCreatingWindow = true;
    aNewTab->mDelayedURL.Truncate();
  }

  ~AutoUseNewTab() {
    mNewTab->mCreatingWindow = false;
    *mURLToLoad = mNewTab->mDelayedURL;
  }

 private:
  BrowserParent* mNewTab;
  nsCString* mURLToLoad;
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_tabs_BrowserParent_h

/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_NotifyPaintEvent_h_
#define mozilla_dom_NotifyPaintEvent_h_

#include "mozilla/Attributes.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/NotifyPaintEventBinding.h"
#include "nsIDOMNotifyPaintEvent.h"
#include "nsPresContext.h"

namespace mozilla {
namespace dom {

class DOMRect;
class DOMRectList;
class PaintRequestList;

class NotifyPaintEvent : public Event,
                         public nsIDOMNotifyPaintEvent
{

public:
  NotifyPaintEvent(EventTarget* aOwner,
                   nsPresContext* aPresContext,
                   WidgetEvent* aEvent,
                   EventMessage aEventMessage,
                   nsTArray<nsRect>* aInvalidateRequests,
                   uint64_t aTransactionId,
                   DOMHighResTimeStamp aTimeStamp);

  NS_DECL_ISUPPORTS_INHERITED

  NS_DECL_NSIDOMNOTIFYPAINTEVENT

  // Forward to base class
  NS_FORWARD_TO_EVENT_NO_SERIALIZATION_NO_DUPLICATION
  NS_IMETHOD DuplicatePrivateData() override
  {
    return Event::DuplicatePrivateData();
  }
  NS_IMETHOD_(void) Serialize(IPC::Message* aMsg, bool aSerializeInterfaceType) override;
  NS_IMETHOD_(bool) Deserialize(const IPC::Message* aMsg, PickleIterator* aIter) override;

  virtual JSObject* WrapObjectInternal(JSContext* aCx, JS::Handle<JSObject*> aGivenProto) override
  {
    return NotifyPaintEventBinding::Wrap(aCx, this, aGivenProto);
  }

  already_AddRefed<DOMRectList> ClientRects(SystemCallerGuarantee aGuarantee);

  already_AddRefed<DOMRect> BoundingClientRect(SystemCallerGuarantee aGuarantee);

  already_AddRefed<PaintRequestList> PaintRequests(SystemCallerGuarantee);

  uint64_t TransactionId(SystemCallerGuarantee);

  DOMHighResTimeStamp PaintTimeStamp(SystemCallerGuarantee);

protected:
  ~NotifyPaintEvent() {}

private:
  nsRegion GetRegion(SystemCallerGuarantee);

  nsTArray<nsRect> mInvalidateRequests;
  uint64_t mTransactionId;
  DOMHighResTimeStamp mTimeStamp;
};

} // namespace dom
} // namespace mozilla

// This empties aInvalidateRequests.
already_AddRefed<mozilla::dom::NotifyPaintEvent>
NS_NewDOMNotifyPaintEvent(mozilla::dom::EventTarget* aOwner,
                          nsPresContext* aPresContext,
                          mozilla::WidgetEvent* aEvent,
                          mozilla::EventMessage aEventMessage =
                            mozilla::eVoidEvent,
                          nsTArray<nsRect>* aInvalidateRequests = nullptr,
                          uint64_t aTransactionId = 0,
                          DOMHighResTimeStamp aTimeStamp = 0);

#endif // mozilla_dom_NotifyPaintEvent_h_

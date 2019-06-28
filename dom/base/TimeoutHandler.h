/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_timeout_handler_h
#define mozilla_dom_timeout_handler_h

#include "nsCOMPtr.h"
#include "nsIGlobalObject.h"
#include "nsISupports.h"
#include "nsCycleCollectionParticipant.h"
#include "nsString.h"
#include "mozilla/Attributes.h"
#include "mozilla/dom/FunctionBinding.h"

namespace mozilla {
namespace dom {

/**
 * Utility class for implementing nsITimeoutHandlers, designed to be subclassed.
 */
class TimeoutHandler : public nsISupports {
 public:
  // TimeoutHandler doesn't actually contain cycles, but subclasses
  // probably will.
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS(TimeoutHandler)

  MOZ_CAN_RUN_SCRIPT virtual bool Call(const char* /* unused */);
  // Get the location of the script.
  // Note: The memory pointed to by aFileName is owned by the
  // nsITimeoutHandler and should not be freed by the caller.
  virtual void GetLocation(const char** aFileName, uint32_t* aLineNo,
                           uint32_t* aColumn);
  virtual void MarkForCC() {}

 protected:
  TimeoutHandler() : mFileName(""), mLineNo(0), mColumn(0) {}
  explicit TimeoutHandler(JSContext* aCx);

  virtual ~TimeoutHandler() {}

  // filename, line number and JS language version string of the
  // caller of setTimeout()
  nsCString mFileName;
  uint32_t mLineNo;
  uint32_t mColumn;

 private:
  TimeoutHandler(const TimeoutHandler&) = delete;
  TimeoutHandler& operator=(const TimeoutHandler&) = delete;
  TimeoutHandler& operator=(const TimeoutHandler&&) = delete;
};

class ScriptTimeoutHandler : public TimeoutHandler {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(ScriptTimeoutHandler, TimeoutHandler)

  ScriptTimeoutHandler(JSContext* aCx, nsIGlobalObject* aGlobal,
                       const nsAString& aExpression);

  MOZ_CAN_RUN_SCRIPT virtual bool Call(const char* /* unused */) override {
    return false;
  };

 protected:
  virtual ~ScriptTimeoutHandler() {}

  nsCOMPtr<nsIGlobalObject> mGlobal;
  // The expression to evaluate or function to call. If mFunction is non-null
  // it should be used, else use mExpr.
  nsString mExpr;
};

class CallbackTimeoutHandler final : public TimeoutHandler {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS_INHERITED(CallbackTimeoutHandler,
                                                         TimeoutHandler)

  CallbackTimeoutHandler(JSContext* aCx, nsIGlobalObject* aGlobal,
                         Function* aFunction,
                         nsTArray<JS::Heap<JS::Value>>&& aArguments);

  MOZ_CAN_RUN_SCRIPT virtual bool Call(const char* aExecutionReason) override;
  virtual void MarkForCC() override;

  void ReleaseJSObjects();

 private:
  virtual ~CallbackTimeoutHandler() { ReleaseJSObjects(); }

  nsCOMPtr<nsIGlobalObject> mGlobal;
  RefPtr<Function> mFunction;
  nsTArray<JS::Heap<JS::Value>> mArgs;
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_timeout_handler_h

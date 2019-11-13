/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "IDBTransaction.h"

#include "BackgroundChildImpl.h"
#include "IDBDatabase.h"
#include "IDBEvents.h"
#include "IDBObjectStore.h"
#include "IDBRequest.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/HoldDropJSObjects.h"
#include "mozilla/dom/DOMException.h"
#include "mozilla/dom/DOMStringList.h"
#include "mozilla/dom/WorkerRef.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ScopeExit.h"
#include "nsAutoPtr.h"
#include "nsPIDOMWindow.h"
#include "nsQueryObject.h"
#include "nsServiceManagerUtils.h"
#include "nsTHashtable.h"
#include "ProfilerHelpers.h"
#include "ReportInternalError.h"

// Include this last to avoid path problems on Windows.
#include "ActorsChild.h"

namespace {
using namespace mozilla::dom::indexedDB;
using namespace mozilla::ipc;

// TODO: Move this to xpcom/ds.
template <typename T, typename Range, typename Transformation>
nsTHashtable<T> TransformToHashtable(const Range& aRange,
                                     const Transformation& aTransformation) {
  // TODO: Determining the size of the range is not syntactically necessary (and
  // requires random access iterators if expressed this way). It is a
  // performance optimization. We could resort to std::distance to support any
  // iterator category, but this would lead to a double iteration of the range
  // in case of non-random-access iterators. It is hard to determine in general
  // if double iteration or reallocation is worse.
  auto res = nsTHashtable<T>(aRange.cend() - aRange.cbegin());
  // TOOD: std::transform could be used if nsTHashtable had an insert_iterator,
  // and this would also allow a more generic version not depending on
  // nsTHashtable at all.
  for (const auto& item : aRange) {
    res.PutEntry(aTransformation(item));
  }
  return res;
}

ThreadLocal* GetIndexedDBThreadLocal() {
  BackgroundChildImpl::ThreadLocal* const threadLocal =
      BackgroundChildImpl::GetThreadLocalForCurrentThread();
  MOZ_ASSERT(threadLocal);

  ThreadLocal* idbThreadLocal = threadLocal->mIndexedDBThreadLocal;
  MOZ_ASSERT(idbThreadLocal);

  return idbThreadLocal;
}
}  // namespace

namespace mozilla {
namespace dom {

using namespace mozilla::dom::indexedDB;
using namespace mozilla::ipc;

bool IDBTransaction::HasTransactionChild() const {
  return (mMode == VERSION_CHANGE
              ? static_cast<void*>(
                    mBackgroundActor.mVersionChangeBackgroundActor)
              : mBackgroundActor.mNormalBackgroundActor) != nullptr;
}

template <typename Func>
auto IDBTransaction::DoWithTransactionChild(const Func& aFunc) const {
  MOZ_ASSERT(HasTransactionChild());
  return mMode == VERSION_CHANGE
             ? aFunc(*mBackgroundActor.mVersionChangeBackgroundActor)
             : aFunc(*mBackgroundActor.mNormalBackgroundActor);
}

IDBTransaction::IDBTransaction(IDBDatabase* const aDatabase,
                               const nsTArray<nsString>& aObjectStoreNames,
                               const Mode aMode, nsString aFilename,
                               const uint32_t aLineNo, const uint32_t aColumn)
    : DOMEventTargetHelper(aDatabase),
      mDatabase(aDatabase),
      mObjectStoreNames(aObjectStoreNames),
      mLoggingSerialNumber(GetIndexedDBThreadLocal()->NextTransactionSN(aMode)),
      mNextObjectStoreId(0),
      mNextIndexId(0),
      mAbortCode(NS_OK),
      mPendingRequestCount(0),
      mFilename(std::move(aFilename)),
      mLineNo(aLineNo),
      mColumn(aColumn),
      mReadyState(IDBTransaction::INITIAL),
      mMode(aMode),
      mCreating(false),
      mRegistered(false),
      mAbortedByScript(false),
      mNotedActiveTransaction(false)
#ifdef DEBUG
      ,
      mSentCommitOrAbort(false),
      mFiredCompleteOrAbort(false)
#endif
{
  MOZ_ASSERT(aDatabase);
  aDatabase->AssertIsOnOwningThread();

  // This also nulls mBackgroundActor.mVersionChangeBackgroundActor, so this is
  // valid also for mMode == VERSION_CHANGE.
  mBackgroundActor.mNormalBackgroundActor = nullptr;

#ifdef DEBUG
  if (!aObjectStoreNames.IsEmpty()) {
    // Make sure the array is properly sorted.
    MOZ_ASSERT(
        std::is_sorted(aObjectStoreNames.cbegin(), aObjectStoreNames.cend()));

    // Make sure there are no duplicates in our objectStore names.
    MOZ_ASSERT(aObjectStoreNames.cend() ==
               std::adjacent_find(aObjectStoreNames.cbegin(),
                                  aObjectStoreNames.cend()));
  }
#endif

  mozilla::HoldJSObjects(this);
}

IDBTransaction::~IDBTransaction() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(!mPendingRequestCount);
  MOZ_ASSERT(!mCreating);
  MOZ_ASSERT(!mNotedActiveTransaction);
  MOZ_ASSERT(mSentCommitOrAbort);
  MOZ_ASSERT_IF(HasTransactionChild(), mFiredCompleteOrAbort);

  if (mRegistered) {
    mDatabase->UnregisterTransaction(this);
#ifdef DEBUG
    mRegistered = false;
#endif
  }

  if (HasTransactionChild()) {
    if (mMode == VERSION_CHANGE) {
      mBackgroundActor.mVersionChangeBackgroundActor->SendDeleteMeInternal(
          /* aFailedConstructor */ false);
    } else {
      mBackgroundActor.mNormalBackgroundActor->SendDeleteMeInternal();
    }
  }
  MOZ_ASSERT(!HasTransactionChild(),
             "SendDeleteMeInternal should have cleared!");

  ReleaseWrapper(this);
  mozilla::DropJSObjects(this);
}

// static
already_AddRefed<IDBTransaction> IDBTransaction::CreateVersionChange(
    IDBDatabase* const aDatabase,
    BackgroundVersionChangeTransactionChild* const aActor,
    IDBOpenDBRequest* const aOpenRequest, const int64_t aNextObjectStoreId,
    const int64_t aNextIndexId) {
  MOZ_ASSERT(aDatabase);
  aDatabase->AssertIsOnOwningThread();
  MOZ_ASSERT(aActor);
  MOZ_ASSERT(aOpenRequest);
  MOZ_ASSERT(aNextObjectStoreId > 0);
  MOZ_ASSERT(aNextIndexId > 0);

  nsTArray<nsString> emptyObjectStoreNames;

  nsString filename;
  uint32_t lineNo, column;
  aOpenRequest->GetCallerLocation(filename, &lineNo, &column);
  RefPtr<IDBTransaction> transaction =
      new IDBTransaction(aDatabase, emptyObjectStoreNames, VERSION_CHANGE,
                         std::move(filename), lineNo, column);

  transaction->NoteActiveTransaction();

  transaction->mBackgroundActor.mVersionChangeBackgroundActor = aActor;
  transaction->mNextObjectStoreId = aNextObjectStoreId;
  transaction->mNextIndexId = aNextIndexId;

  aDatabase->RegisterTransaction(transaction);
  transaction->mRegistered = true;

  return transaction.forget();
}

// static
already_AddRefed<IDBTransaction> IDBTransaction::Create(
    JSContext* const aCx, IDBDatabase* const aDatabase,
    const nsTArray<nsString>& aObjectStoreNames, const Mode aMode) {
  MOZ_ASSERT(aDatabase);
  aDatabase->AssertIsOnOwningThread();
  MOZ_ASSERT(!aObjectStoreNames.IsEmpty());
  MOZ_ASSERT(aMode == READ_ONLY || aMode == READ_WRITE ||
             aMode == READ_WRITE_FLUSH || aMode == CLEANUP);

  nsString filename;
  uint32_t lineNo, column;
  IDBRequest::CaptureCaller(aCx, filename, &lineNo, &column);
  RefPtr<IDBTransaction> transaction = new IDBTransaction(
      aDatabase, aObjectStoreNames, aMode, std::move(filename), lineNo, column);

  if (!NS_IsMainThread()) {
    WorkerPrivate* const workerPrivate = GetCurrentThreadWorkerPrivate();
    MOZ_ASSERT(workerPrivate);

    workerPrivate->AssertIsOnWorkerThread();

    RefPtr<StrongWorkerRef> workerRef = StrongWorkerRef::Create(
        workerPrivate, "IDBTransaction", [transaction]() {
          transaction->AssertIsOnOwningThread();
          if (!transaction->IsCommittingOrDone()) {
            IDB_REPORT_INTERNAL_ERR();
            transaction->AbortInternal(NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR,
                                       nullptr);
          }
        });
    if (NS_WARN_IF(!workerRef)) {
      // Silence the destructor assertion if we never made this object live.
#ifdef DEBUG
      MOZ_ASSERT(!transaction->mSentCommitOrAbort);
      transaction->mSentCommitOrAbort = true;
#endif
      return nullptr;
    }

    transaction->mWorkerRef = std::move(workerRef);
  }

  nsCOMPtr<nsIRunnable> runnable = do_QueryObject(transaction);
  nsContentUtils::AddPendingIDBTransaction(runnable.forget());

  transaction->mCreating = true;

  aDatabase->RegisterTransaction(transaction);
  transaction->mRegistered = true;

  return transaction.forget();
}

// static
IDBTransaction* IDBTransaction::GetCurrent() {
  using namespace mozilla::ipc;

  MOZ_ASSERT(BackgroundChild::GetForCurrentThread());

  return GetIndexedDBThreadLocal()->GetCurrentTransaction();
}

#ifdef DEBUG

void IDBTransaction::AssertIsOnOwningThread() const {
  MOZ_ASSERT(mDatabase);
  mDatabase->AssertIsOnOwningThread();
}

#endif  // DEBUG

void IDBTransaction::SetBackgroundActor(
    indexedDB::BackgroundTransactionChild* const aBackgroundActor) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aBackgroundActor);
  MOZ_ASSERT(!mBackgroundActor.mNormalBackgroundActor);
  MOZ_ASSERT(mMode != VERSION_CHANGE);

  NoteActiveTransaction();

  mBackgroundActor.mNormalBackgroundActor = aBackgroundActor;
}

BackgroundRequestChild* IDBTransaction::StartRequest(
    IDBRequest* const aRequest, const RequestParams& aParams) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aRequest);
  MOZ_ASSERT(aParams.type() != RequestParams::T__None);

  BackgroundRequestChild* const actor = new BackgroundRequestChild(aRequest);

  DoWithTransactionChild([actor, &aParams](auto& transactionChild) {
    transactionChild.SendPBackgroundIDBRequestConstructor(actor, aParams);
  });

  MOZ_ASSERT(actor->GetActorEventTarget(),
             "The event target shall be inherited from its manager actor.");

  // Balanced in BackgroundRequestChild::Recv__delete__().
  OnNewRequest();

  return actor;
}

void IDBTransaction::OpenCursor(BackgroundCursorChild* const aBackgroundActor,
                                const OpenCursorParams& aParams) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aBackgroundActor);
  MOZ_ASSERT(aParams.type() != OpenCursorParams::T__None);

  DoWithTransactionChild([aBackgroundActor, &aParams](auto& actor) {
    actor.SendPBackgroundIDBCursorConstructor(aBackgroundActor, aParams);
  });

  MOZ_ASSERT(aBackgroundActor->GetActorEventTarget(),
             "The event target shall be inherited from its manager actor.");

  // Balanced in BackgroundCursorChild::RecvResponse().
  OnNewRequest();
}

void IDBTransaction::RefreshSpec(const bool aMayDelete) {
  AssertIsOnOwningThread();

  for (auto& objectStore : mObjectStores) {
    objectStore->RefreshSpec(aMayDelete);
  }

  for (auto& objectStore : mDeletedObjectStores) {
    objectStore->RefreshSpec(false);
  }
}

void IDBTransaction::OnNewRequest() {
  AssertIsOnOwningThread();

  if (!mPendingRequestCount) {
    MOZ_ASSERT(INITIAL == mReadyState);
    mReadyState = LOADING;
  }

  ++mPendingRequestCount;
}

void IDBTransaction::OnRequestFinished(
    const bool aRequestCompletedSuccessfully) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mPendingRequestCount);

  --mPendingRequestCount;

  if (!mPendingRequestCount) {
    mReadyState = COMMITTING;

    if (aRequestCompletedSuccessfully) {
      if (NS_SUCCEEDED(mAbortCode)) {
        SendCommit();
      } else {
        SendAbort(mAbortCode);
      }
    } else {
      // Don't try to send any more messages to the parent if the request actor
      // was killed.
#ifdef DEBUG
      MOZ_ASSERT(!mSentCommitOrAbort);
      mSentCommitOrAbort = true;
#endif
      IDB_LOG_MARK_CHILD_TRANSACTION(
          "Request actor was killed, transaction will be aborted",
          "IDBTransaction abort", LoggingSerialNumber());
    }
  }
}

void IDBTransaction::SendCommit() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(NS_SUCCEEDED(mAbortCode));
  MOZ_ASSERT(IsCommittingOrDone());
  MOZ_ASSERT(!mSentCommitOrAbort);
  MOZ_ASSERT(!mPendingRequestCount);

  // Don't do this in the macro because we always need to increment the serial
  // number to keep in sync with the parent.
  const uint64_t requestSerialNumber = IDBRequest::NextSerialNumber();

  IDB_LOG_MARK_CHILD_TRANSACTION_REQUEST(
      "All requests complete, committing transaction", "IDBTransaction commit",
      LoggingSerialNumber(), requestSerialNumber);

  DoWithTransactionChild([](auto& actor) { actor.SendCommit(); });

#ifdef DEBUG
  mSentCommitOrAbort = true;
#endif
}

void IDBTransaction::SendAbort(const nsresult aResultCode) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(NS_FAILED(aResultCode));
  MOZ_ASSERT(IsCommittingOrDone());
  MOZ_ASSERT(!mSentCommitOrAbort);

  // Don't do this in the macro because we always need to increment the serial
  // number to keep in sync with the parent.
  const uint64_t requestSerialNumber = IDBRequest::NextSerialNumber();

  IDB_LOG_MARK_CHILD_TRANSACTION_REQUEST(
      "Aborting transaction with result 0x%x", "IDBTransaction abort (0x%x)",
      LoggingSerialNumber(), requestSerialNumber, aResultCode);

  DoWithTransactionChild(
      [aResultCode](auto& actor) { actor.SendAbort(aResultCode); });

#ifdef DEBUG
  mSentCommitOrAbort = true;
#endif
}

void IDBTransaction::NoteActiveTransaction() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(!mNotedActiveTransaction);

  mDatabase->NoteActiveTransaction();
  mNotedActiveTransaction = true;
}

void IDBTransaction::MaybeNoteInactiveTransaction() {
  AssertIsOnOwningThread();

  if (mNotedActiveTransaction) {
    mDatabase->NoteInactiveTransaction();
    mNotedActiveTransaction = false;
  }
}

bool IDBTransaction::IsOpen() const {
  AssertIsOnOwningThread();

  // If we haven't started anything then we're open.
  if (mReadyState == IDBTransaction::INITIAL) {
    return true;
  }

  // If we've already started then we need to check to see if we still have the
  // mCreating flag set. If we do (i.e. we haven't returned to the event loop
  // from the time we were created) then we are open. Otherwise check the
  // currently running transaction to see if it's the same. We only allow other
  // requests to be made if this transaction is currently running.
  if (mReadyState == IDBTransaction::LOADING &&
      (mCreating || GetCurrent() == this)) {
    return true;
  }

  return false;
}

void IDBTransaction::GetCallerLocation(nsAString& aFilename,
                                       uint32_t* const aLineNo,
                                       uint32_t* const aColumn) const {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aLineNo);
  MOZ_ASSERT(aColumn);

  aFilename = mFilename;
  *aLineNo = mLineNo;
  *aColumn = mColumn;
}

already_AddRefed<IDBObjectStore> IDBTransaction::CreateObjectStore(
    const ObjectStoreSpec& aSpec) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aSpec.metadata().id());
  MOZ_ASSERT(VERSION_CHANGE == mMode);
  MOZ_ASSERT(mBackgroundActor.mVersionChangeBackgroundActor);
  MOZ_ASSERT(IsOpen());

#ifdef DEBUG
  {
    // TODO: Bind name outside of lambda capture as a workaround for GCC 7 bug
    // https://gcc.gnu.org/bugzilla/show_bug.cgi?id=66735.
    const auto& name = aSpec.metadata().name();
    // TODO: Use #ifdef and local variable as a workaround for Bug 1583449.
    const bool objectStoreNameDoesNotYetExist =
        std::all_of(mObjectStores.cbegin(), mObjectStores.cend(),
                    [&name](const auto& objectStore) {
                      return objectStore->Name() != name;
                    });
    MOZ_ASSERT(objectStoreNameDoesNotYetExist);
  }
#endif

  MOZ_ALWAYS_TRUE(
      mBackgroundActor.mVersionChangeBackgroundActor->SendCreateObjectStore(
          aSpec.metadata()));

  RefPtr<IDBObjectStore> objectStore = IDBObjectStore::Create(this, aSpec);
  MOZ_ASSERT(objectStore);

  mObjectStores.AppendElement(objectStore);

  return objectStore.forget();
}

void IDBTransaction::DeleteObjectStore(const int64_t aObjectStoreId) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aObjectStoreId);
  MOZ_ASSERT(VERSION_CHANGE == mMode);
  MOZ_ASSERT(mBackgroundActor.mVersionChangeBackgroundActor);
  MOZ_ASSERT(IsOpen());

  MOZ_ALWAYS_TRUE(
      mBackgroundActor.mVersionChangeBackgroundActor->SendDeleteObjectStore(
          aObjectStoreId));

  const auto foundIt =
      std::find_if(mObjectStores.begin(), mObjectStores.end(),
                   [aObjectStoreId](const auto& objectStore) {
                     return objectStore->Id() == aObjectStoreId;
                   });
  if (foundIt != mObjectStores.end()) {
    auto& objectStore = *foundIt;
    objectStore->NoteDeletion();

    RefPtr<IDBObjectStore>* deletedObjectStore =
        mDeletedObjectStores.AppendElement();
    deletedObjectStore->swap(objectStore);

    mObjectStores.RemoveElementAt(foundIt);
  }
}

void IDBTransaction::RenameObjectStore(const int64_t aObjectStoreId,
                                       const nsAString& aName) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aObjectStoreId);
  MOZ_ASSERT(VERSION_CHANGE == mMode);
  MOZ_ASSERT(mBackgroundActor.mVersionChangeBackgroundActor);
  MOZ_ASSERT(IsOpen());

  MOZ_ALWAYS_TRUE(
      mBackgroundActor.mVersionChangeBackgroundActor->SendRenameObjectStore(
          aObjectStoreId, nsString(aName)));
}

void IDBTransaction::CreateIndex(IDBObjectStore* const aObjectStore,
                                 const indexedDB::IndexMetadata& aMetadata) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aObjectStore);
  MOZ_ASSERT(aMetadata.id());
  MOZ_ASSERT(VERSION_CHANGE == mMode);
  MOZ_ASSERT(mBackgroundActor.mVersionChangeBackgroundActor);
  MOZ_ASSERT(IsOpen());

  MOZ_ALWAYS_TRUE(
      mBackgroundActor.mVersionChangeBackgroundActor->SendCreateIndex(
          aObjectStore->Id(), aMetadata));
}

void IDBTransaction::DeleteIndex(IDBObjectStore* const aObjectStore,
                                 const int64_t aIndexId) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aObjectStore);
  MOZ_ASSERT(aIndexId);
  MOZ_ASSERT(VERSION_CHANGE == mMode);
  MOZ_ASSERT(mBackgroundActor.mVersionChangeBackgroundActor);
  MOZ_ASSERT(IsOpen());

  MOZ_ALWAYS_TRUE(
      mBackgroundActor.mVersionChangeBackgroundActor->SendDeleteIndex(
          aObjectStore->Id(), aIndexId));
}

void IDBTransaction::RenameIndex(IDBObjectStore* const aObjectStore,
                                 const int64_t aIndexId,
                                 const nsAString& aName) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aObjectStore);
  MOZ_ASSERT(aIndexId);
  MOZ_ASSERT(VERSION_CHANGE == mMode);
  MOZ_ASSERT(mBackgroundActor.mVersionChangeBackgroundActor);
  MOZ_ASSERT(IsOpen());

  MOZ_ALWAYS_TRUE(
      mBackgroundActor.mVersionChangeBackgroundActor->SendRenameIndex(
          aObjectStore->Id(), aIndexId, nsString(aName)));
}

void IDBTransaction::AbortInternal(const nsresult aAbortCode,
                                   already_AddRefed<DOMException> aError) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(NS_FAILED(aAbortCode));
  MOZ_ASSERT(!IsCommittingOrDone());

  RefPtr<DOMException> error = aError;

  const bool isVersionChange = mMode == VERSION_CHANGE;
  const bool needToSendAbort = mReadyState == INITIAL;

  mAbortCode = aAbortCode;
  mReadyState = DONE;
  mError = error.forget();

  if (isVersionChange) {
    // If a version change transaction is aborted, we must revert the world
    // back to its previous state unless we're being invalidated after the
    // transaction already completed.
    if (!mDatabase->IsInvalidated()) {
      mDatabase->RevertToPreviousState();
    }

    // We do the reversion only for the mObjectStores/mDeletedObjectStores but
    // not for the mIndexes/mDeletedIndexes of each IDBObjectStore because it's
    // time-consuming(O(m*n)) and mIndexes/mDeletedIndexes won't be used anymore
    // in IDBObjectStore::(Create|Delete)Index() and IDBObjectStore::Index() in
    // which all the executions are returned earlier by !transaction->IsOpen().

    const nsTArray<ObjectStoreSpec>& specArray =
        mDatabase->Spec()->objectStores();

    if (specArray.IsEmpty()) {
      // This case is specially handled as a performance optimization, it is
      // equivalent to the else block.
      mObjectStores.Clear();
    } else {
      const auto validIds = TransformToHashtable<nsUint64HashKey>(
          specArray, [](const auto& spec) {
            const int64_t objectStoreId = spec.metadata().id();
            MOZ_ASSERT(objectStoreId);
            return static_cast<uint64_t>(objectStoreId);
          });

      mObjectStores.RemoveElementsAt(
          std::remove_if(mObjectStores.begin(), mObjectStores.end(),
                         [&validIds](const auto& objectStore) {
                           return !validIds.Contains(
                               uint64_t(objectStore->Id()));
                         }),
          mObjectStores.end());

      // TODO: if nsTArray had an insert iterator, we could use the following
      // instead:
      // std::copy_if(std::make_move_iterator(mDeletedObjectStores.begin()),
      //              std::make_move_iterator(mDeletedObjectStores.end()),
      //              std::back_inserter(mObjectStores),
      //              [&validIds](const auto& deletedObjectStore) {
      //                const int64_t objectStoreId = deletedObjectStore->Id();
      //                MOZ_ASSERT(objectStoreId);
      //                return validIds.Contains(uint64_t(objectStoreId));
      //              });
      for (auto& deletedObjectStore : mDeletedObjectStores) {
        const int64_t objectStoreId = deletedObjectStore->Id();
        MOZ_ASSERT(objectStoreId);

        if (validIds.Contains(uint64_t(objectStoreId))) {
          RefPtr<IDBObjectStore>* objectStore = mObjectStores.AppendElement();
          objectStore->swap(deletedObjectStore);
        }
      }
    }
    mDeletedObjectStores.Clear();
  }

  // Fire the abort event if there are no outstanding requests. Otherwise the
  // abort event will be fired when all outstanding requests finish.
  if (needToSendAbort) {
    SendAbort(aAbortCode);
  }

  if (isVersionChange) {
    mDatabase->Close();
  }
}

void IDBTransaction::Abort(IDBRequest* const aRequest) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aRequest);

  if (IsCommittingOrDone()) {
    // Already started (and maybe finished) the commit or abort so there is
    // nothing to do here.
    return;
  }

  ErrorResult rv;
  RefPtr<DOMException> error = aRequest->GetError(rv);

  AbortInternal(aRequest->GetErrorCode(), error.forget());
}

void IDBTransaction::Abort(const nsresult aErrorCode) {
  AssertIsOnOwningThread();

  if (IsCommittingOrDone()) {
    // Already started (and maybe finished) the commit or abort so there is
    // nothing to do here.
    return;
  }

  RefPtr<DOMException> error = DOMException::Create(aErrorCode);
  AbortInternal(aErrorCode, error.forget());
}

void IDBTransaction::Abort(ErrorResult& aRv) {
  AssertIsOnOwningThread();

  if (IsCommittingOrDone()) {
    aRv = NS_ERROR_DOM_INDEXEDDB_NOT_ALLOWED_ERR;
    return;
  }

  AbortInternal(NS_ERROR_DOM_INDEXEDDB_ABORT_ERR, nullptr);

  MOZ_ASSERT(!mAbortedByScript);
  mAbortedByScript = true;
}

void IDBTransaction::FireCompleteOrAbortEvents(const nsresult aResult) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(!mFiredCompleteOrAbort);

  mReadyState = DONE;

#ifdef DEBUG
  mFiredCompleteOrAbort = true;
#endif

  // Make sure we drop the WorkerRef when this function completes.
  const auto scopeExit = MakeScopeExit([&] { mWorkerRef = nullptr; });

  RefPtr<Event> event;
  if (NS_SUCCEEDED(aResult)) {
    event = CreateGenericEvent(this, nsDependentString(kCompleteEventType),
                               eDoesNotBubble, eNotCancelable);
    MOZ_ASSERT(event);

    // If we hit this assertion, it probably means transaction object on the
    // parent process doesn't propagate error properly.
    MOZ_ASSERT(NS_SUCCEEDED(mAbortCode));
  } else {
    if (aResult == NS_ERROR_DOM_INDEXEDDB_QUOTA_ERR) {
      mDatabase->SetQuotaExceeded();
    }

    if (!mError && !mAbortedByScript) {
      mError = DOMException::Create(aResult);
    }

    event = CreateGenericEvent(this, nsDependentString(kAbortEventType),
                               eDoesBubble, eNotCancelable);
    MOZ_ASSERT(event);

    if (NS_SUCCEEDED(mAbortCode)) {
      mAbortCode = aResult;
    }
  }

  if (NS_SUCCEEDED(mAbortCode)) {
    IDB_LOG_MARK_CHILD_TRANSACTION("Firing 'complete' event",
                                   "IDBTransaction 'complete' event",
                                   mLoggingSerialNumber);
  } else {
    IDB_LOG_MARK_CHILD_TRANSACTION("Firing 'abort' event with error 0x%x",
                                   "IDBTransaction 'abort' event (0x%x)",
                                   mLoggingSerialNumber, mAbortCode);
  }

  IgnoredErrorResult rv;
  DispatchEvent(*event, rv);
  if (rv.Failed()) {
    NS_WARNING("DispatchEvent failed!");
  }

  // Normally, we note inactive transaction here instead of
  // IDBTransaction::ClearBackgroundActor() because here is the earliest place
  // to know that it becomes non-blocking to allow the scheduler to start the
  // preemption as soon as it can.
  // Note: If the IDBTransaction object is held by the script,
  // ClearBackgroundActor() will be done in ~IDBTransaction() until garbage
  // collected after its window is closed which prevents us to preempt its
  // window immediately after committed.
  MaybeNoteInactiveTransaction();
}

int64_t IDBTransaction::NextObjectStoreId() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(VERSION_CHANGE == mMode);

  return mNextObjectStoreId++;
}

int64_t IDBTransaction::NextIndexId() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(VERSION_CHANGE == mMode);

  return mNextIndexId++;
}

void IDBTransaction::InvalidateCursorCaches() {
  AssertIsOnOwningThread();

  for (auto* const cursor : mCursors) {
    cursor->InvalidateCachedResponses();
  }
}

void IDBTransaction::RegisterCursor(IDBCursor* const aCursor) {
  AssertIsOnOwningThread();

  mCursors.AppendElement(aCursor);
}

void IDBTransaction::UnregisterCursor(IDBCursor* const aCursor) {
  AssertIsOnOwningThread();

  DebugOnly<bool> removed = mCursors.RemoveElement(aCursor);
  MOZ_ASSERT(removed);
}

nsIGlobalObject* IDBTransaction::GetParentObject() const {
  AssertIsOnOwningThread();

  return mDatabase->GetParentObject();
}

IDBTransactionMode IDBTransaction::GetMode(ErrorResult& aRv) const {
  AssertIsOnOwningThread();

  switch (mMode) {
    case READ_ONLY:
      return IDBTransactionMode::Readonly;

    case READ_WRITE:
      return IDBTransactionMode::Readwrite;

    case READ_WRITE_FLUSH:
      return IDBTransactionMode::Readwriteflush;

    case CLEANUP:
      return IDBTransactionMode::Cleanup;

    case VERSION_CHANGE:
      return IDBTransactionMode::Versionchange;

    case MODE_INVALID:
    default:
      MOZ_CRASH("Bad mode!");
  }
}

DOMException* IDBTransaction::GetError() const {
  AssertIsOnOwningThread();

  return mError;
}

already_AddRefed<DOMStringList> IDBTransaction::ObjectStoreNames() const {
  AssertIsOnOwningThread();

  if (mMode == VERSION_CHANGE) {
    return mDatabase->ObjectStoreNames();
  }

  RefPtr<DOMStringList> list = new DOMStringList();
  list->StringArray() = mObjectStoreNames;
  return list.forget();
}

already_AddRefed<IDBObjectStore> IDBTransaction::ObjectStore(
    const nsAString& aName, ErrorResult& aRv) {
  AssertIsOnOwningThread();

  if (IsCommittingOrDone()) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return nullptr;
  }

  const ObjectStoreSpec* spec = nullptr;

  if (IDBTransaction::VERSION_CHANGE == mMode ||
      mObjectStoreNames.Contains(aName)) {
    const nsTArray<ObjectStoreSpec>& objectStores =
        mDatabase->Spec()->objectStores();

    const auto foundIt =
        std::find_if(objectStores.cbegin(), objectStores.cend(),
                     [&aName](const auto& objectStore) {
                       return objectStore.metadata().name() == aName;
                     });
    if (foundIt != objectStores.cend()) {
      spec = &*foundIt;
    }
  }

  if (!spec) {
    aRv.Throw(NS_ERROR_DOM_INDEXEDDB_NOT_FOUND_ERR);
    return nullptr;
  }

  RefPtr<IDBObjectStore> objectStore;

  const auto foundIt = std::find_if(
      mObjectStores.cbegin(), mObjectStores.cend(),
      [desiredId = spec->metadata().id()](const auto& existingObjectStore) {
        return existingObjectStore->Id() == desiredId;
      });
  if (foundIt != mObjectStores.cend()) {
    objectStore = *foundIt;
  } else {
    objectStore = IDBObjectStore::Create(this, *spec);
    MOZ_ASSERT(objectStore);

    mObjectStores.AppendElement(objectStore);
  }

  return objectStore.forget();
}

NS_IMPL_ADDREF_INHERITED(IDBTransaction, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(IDBTransaction, DOMEventTargetHelper)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(IDBTransaction)
  NS_INTERFACE_MAP_ENTRY(nsIRunnable)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

NS_IMPL_CYCLE_COLLECTION_CLASS(IDBTransaction)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(IDBTransaction,
                                                  DOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDatabase)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mError)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mObjectStores)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDeletedObjectStores)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(IDBTransaction,
                                                DOMEventTargetHelper)
  // Don't unlink mDatabase!
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mError)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mObjectStores)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDeletedObjectStores)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

JSObject* IDBTransaction::WrapObject(JSContext* const aCx,
                                     JS::Handle<JSObject*> aGivenProto) {
  AssertIsOnOwningThread();

  return IDBTransaction_Binding::Wrap(aCx, this, std::move(aGivenProto));
}

void IDBTransaction::GetEventTargetParent(EventChainPreVisitor& aVisitor) {
  AssertIsOnOwningThread();

  aVisitor.mCanHandle = true;
  aVisitor.SetParentTarget(mDatabase, false);
}

NS_IMETHODIMP
IDBTransaction::Run() {
  AssertIsOnOwningThread();

  // We're back at the event loop, no longer newborn.
  mCreating = false;

  // Maybe commit if there were no requests generated.
  if (mReadyState == IDBTransaction::INITIAL) {
    mReadyState = DONE;

    SendCommit();
  }

  return NS_OK;
}

}  // namespace dom
}  // namespace mozilla

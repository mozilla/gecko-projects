/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#if !defined(MozPromise_h_)
#define MozPromise_h_

#include "mozilla/AbstractThread.h"
#include "mozilla/IndexSequence.h"
#include "mozilla/Logging.h"
#include "mozilla/Maybe.h"
#include "mozilla/Mutex.h"
#include "mozilla/Monitor.h"
#include "mozilla/Tuple.h"
#include "mozilla/TypeTraits.h"
#include "mozilla/Variant.h"

#include "nsTArray.h"
#include "nsThreadUtils.h"

#if defined(DEBUG) || !defined(RELEASE_OR_BETA)
#define PROMISE_DEBUG
#endif

#ifdef PROMISE_DEBUG
#define PROMISE_ASSERT MOZ_RELEASE_ASSERT
#else
#define PROMISE_ASSERT(...) do { } while (0)
#endif

namespace mozilla {

extern LazyLogModule gMozPromiseLog;

#define PROMISE_LOG(x, ...) \
  MOZ_LOG(gMozPromiseLog, mozilla::LogLevel::Debug, (x, ##__VA_ARGS__))

namespace detail {
template<typename ThisType, typename Ret, typename ArgType>
static TrueType TakesArgumentHelper(Ret (ThisType::*)(ArgType));
template<typename ThisType, typename Ret, typename ArgType>
static TrueType TakesArgumentHelper(Ret (ThisType::*)(ArgType) const);
template<typename ThisType, typename Ret>
static FalseType TakesArgumentHelper(Ret (ThisType::*)());
template<typename ThisType, typename Ret>
static FalseType TakesArgumentHelper(Ret (ThisType::*)() const);

template<typename ThisType, typename Ret, typename ArgType>
static Ret ReturnTypeHelper(Ret (ThisType::*)(ArgType));
template<typename ThisType, typename Ret, typename ArgType>
static Ret ReturnTypeHelper(Ret (ThisType::*)(ArgType) const);
template<typename ThisType, typename Ret>
static Ret ReturnTypeHelper(Ret (ThisType::*)());
template<typename ThisType, typename Ret>
static Ret ReturnTypeHelper(Ret (ThisType::*)() const);

template<typename MethodType>
struct ReturnType {
  typedef decltype(detail::ReturnTypeHelper(DeclVal<MethodType>())) Type;
};

} // namespace detail

template<typename MethodType>
struct TakesArgument {
  static const bool value = decltype(detail::TakesArgumentHelper(DeclVal<MethodType>()))::value;
};

template<typename MethodType, typename TargetType>
struct ReturnTypeIs {
  static const bool value = IsConvertible<typename detail::ReturnType<MethodType>::Type, TargetType>::value;
};

/*
 * A promise manages an asynchronous request that may or may not be able to be
 * fulfilled immediately. When an API returns a promise, the consumer may attach
 * callbacks to be invoked (asynchronously, on a specified thread) when the
 * request is either completed (resolved) or cannot be completed (rejected).
 * Whereas JS promise callbacks are dispatched from Microtask checkpoints,
 * MozPromises resolution/rejection make a normal round-trip through the event
 * loop, which simplifies their ordering semantics relative to other native code.
 *
 * MozPromises attempt to mirror the spirit of JS Promises to the extent that
 * is possible (and desirable) in C++. While the intent is that MozPromises
 * feel familiar to programmers who are accustomed to their JS-implemented cousin,
 * we don't shy away from imposing restrictions and adding features that make
 * sense for the use cases we encounter.
 *
 * A MozPromise is ThreadSafe, and may be ->Then()ed on any thread. The Then()
 * call accepts resolve and reject callbacks, and returns a magic object which
 * will be implicitly converted to a MozPromise::Request or a MozPromise object
 * depending on how the return value is used. The magic object serves several
 * purposes for the consumer.
 *
 *   (1) When converting to a MozPromise::Request, it allows the caller to
 *       cancel the delivery of the resolve/reject value if it has not already
 *       occurred, via Disconnect() (this must be done on the target thread to
 *       avoid racing).
 *
 *   (2) When converting to a MozPromise (which is called a completion promise),
 *       it allows promise chaining so ->Then() can be called again to attach
 *       more resolve and reject callbacks. If the resolve/reject callback
 *       returns a new MozPromise, that promise is chained to the completion
 *       promise, such that its resolve/reject value will be forwarded along
 *       when it arrives. If the resolve/reject callback returns void, the
 *       completion promise is resolved/rejected with the same value that was
 *       passed to the callback.
 *
 * The MozPromise APIs skirt traditional XPCOM convention by returning nsRefPtrs
 * (rather than already_AddRefed) from various methods. This is done to allow elegant
 * chaining of calls without cluttering up the code with intermediate variables, and
 * without introducing separate API variants for callers that want a return value
 * (from, say, ->Then()) from those that don't.
 *
 * When IsExclusive is true, the MozPromise does a release-mode assertion that
 * there is at most one call to either Then(...) or ChainTo(...).
 */

class MozPromiseRefcountable
{
public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MozPromiseRefcountable)
protected:
  virtual ~MozPromiseRefcountable() {}
};

template<typename T> class MozPromiseHolder;
template<typename T> class MozPromiseRequestHolder;
template<typename ResolveValueT, typename RejectValueT, bool IsExclusive>
class MozPromise : public MozPromiseRefcountable
{
  static const uint32_t sMagic = 0xcecace11;

  // Return a |T&&| to enable move when IsExclusive is true or
  // a |const T&| to enforce copy otherwise.
  template <typename T,
    typename R = typename Conditional<IsExclusive, T&&, const T&>::Type>
  static R MaybeMove(T& aX)
  {
    return static_cast<R>(aX);
  }

public:
  typedef ResolveValueT ResolveValueType;
  typedef RejectValueT RejectValueType;
  class ResolveOrRejectValue
  {
    template <int, typename T>
    struct Holder
    {
      template <typename... Args>
      explicit Holder(Args&&... aArgs) : mData(Forward<Args>(aArgs)...) { }
      T mData;
    };

    // Ensure Holder<0, T1> and Holder<1, T2> are different types
    // which is required by Variant.
    using ResolveValueHolder = Holder<0, ResolveValueType>;
    using RejectValueHolder = Holder<1, RejectValueType>;

  public:
    template<typename ResolveValueType_>
    void SetResolve(ResolveValueType_&& aResolveValue)
    {
      MOZ_ASSERT(IsNothing());
      mValue = AsVariant(ResolveValueHolder(Forward<ResolveValueType_>(aResolveValue)));
    }

    template<typename RejectValueType_>
    void SetReject(RejectValueType_&& aRejectValue)
    {
      MOZ_ASSERT(IsNothing());
      mValue = AsVariant(RejectValueHolder(Forward<RejectValueType_>(aRejectValue)));
    }

    template<typename ResolveValueType_>
    static ResolveOrRejectValue MakeResolve(ResolveValueType_&& aResolveValue)
    {
      ResolveOrRejectValue val;
      val.SetResolve(Forward<ResolveValueType_>(aResolveValue));
      return val;
    }

    template<typename RejectValueType_>
    static ResolveOrRejectValue MakeReject(RejectValueType_&& aRejectValue)
    {
      ResolveOrRejectValue val;
      val.SetReject(Forward<RejectValueType_>(aRejectValue));
      return val;
    }

    bool IsResolve() const { return mValue.template is<ResolveValueHolder>(); }
    bool IsReject() const { return mValue.template is<RejectValueHolder>(); }
    bool IsNothing() const { return mValue.template is<Nothing>(); }

    const ResolveValueType& ResolveValue() const
    {
      return mValue.template as<ResolveValueHolder>().mData;
    }
    ResolveValueType& ResolveValue()
    {
      return mValue.template as<ResolveValueHolder>().mData;
    }
    const RejectValueType& RejectValue() const
    {
      return mValue.template as<RejectValueHolder>().mData;
    }
    RejectValueType& RejectValue()
    {
      return mValue.template as<RejectValueHolder>().mData;
    }

  private:
    Variant<Nothing, ResolveValueHolder, RejectValueHolder> mValue = AsVariant(Nothing{});
  };

protected:
  // MozPromise is the public type, and never constructed directly. Construct
  // a MozPromise::Private, defined below.
  MozPromise(const char* aCreationSite, bool aIsCompletionPromise)
    : mCreationSite(aCreationSite)
    , mMutex("MozPromise Mutex")
    , mHaveRequest(false)
    , mIsCompletionPromise(aIsCompletionPromise)
#ifdef PROMISE_DEBUG
    , mMagic4(&mMutex)
#endif
  {
    PROMISE_LOG("%s creating MozPromise (%p)", mCreationSite, this);
  }

public:
  // MozPromise::Private allows us to separate the public interface (upon which
  // consumers of the promise may invoke methods like Then()) from the private
  // interface (upon which the creator of the promise may invoke Resolve() or
  // Reject()). APIs should create and store a MozPromise::Private (usually
  // via a MozPromiseHolder), and return a MozPromise to consumers.
  //
  // NB: We can include the definition of this class inline once B2G ICS is gone.
  class Private;

  template<typename ResolveValueType_>
  static RefPtr<MozPromise>
  CreateAndResolve(ResolveValueType_&& aResolveValue, const char* aResolveSite)
  {
    RefPtr<typename MozPromise::Private> p = new MozPromise::Private(aResolveSite);
    p->Resolve(Forward<ResolveValueType_>(aResolveValue), aResolveSite);
    return p.forget();
  }

  template<typename RejectValueType_>
  static RefPtr<MozPromise>
  CreateAndReject(RejectValueType_&& aRejectValue, const char* aRejectSite)
  {
    RefPtr<typename MozPromise::Private> p = new MozPromise::Private(aRejectSite);
    p->Reject(Forward<RejectValueType_>(aRejectValue), aRejectSite);
    return p.forget();
  }

  typedef MozPromise<nsTArray<ResolveValueType>, RejectValueType, IsExclusive> AllPromiseType;
private:
  class AllPromiseHolder : public MozPromiseRefcountable
  {
  public:
    explicit AllPromiseHolder(size_t aDependentPromises)
      : mPromise(new typename AllPromiseType::Private(__func__))
      , mOutstandingPromises(aDependentPromises)
    {
      MOZ_ASSERT(aDependentPromises > 0);
      mResolveValues.SetLength(aDependentPromises);
    }

    void Resolve(size_t aIndex, ResolveValueType&& aResolveValue)
    {
      if (!mPromise) {
        // Already rejected.
        return;
      }

      mResolveValues[aIndex].emplace(Move(aResolveValue));
      if (--mOutstandingPromises == 0) {
        nsTArray<ResolveValueType> resolveValues;
        resolveValues.SetCapacity(mResolveValues.Length());
        for (size_t i = 0; i < mResolveValues.Length(); ++i) {
          resolveValues.AppendElement(Move(mResolveValues[i].ref()));
        }

        mPromise->Resolve(Move(resolveValues), __func__);
        mPromise = nullptr;
        mResolveValues.Clear();
      }
    }

    void Reject(RejectValueType&& aRejectValue)
    {
      if (!mPromise) {
        // Already rejected.
        return;
      }

      mPromise->Reject(Move(aRejectValue), __func__);
      mPromise = nullptr;
      mResolveValues.Clear();
    }

    AllPromiseType* Promise() { return mPromise; }

  private:
    nsTArray<Maybe<ResolveValueType>> mResolveValues;
    RefPtr<typename AllPromiseType::Private> mPromise;
    size_t mOutstandingPromises;
  };
public:

  static RefPtr<AllPromiseType> All(AbstractThread* aProcessingThread, nsTArray<RefPtr<MozPromise>>& aPromises)
  {
    if (aPromises.Length() == 0) {
      return AllPromiseType::CreateAndResolve(nsTArray<ResolveValueType>(), __func__);
    }

    RefPtr<AllPromiseHolder> holder = new AllPromiseHolder(aPromises.Length());
    for (size_t i = 0; i < aPromises.Length(); ++i) {
      aPromises[i]->Then(aProcessingThread, __func__,
        [holder, i] (ResolveValueType aResolveValue) -> void { holder->Resolve(i, Move(aResolveValue)); },
        [holder] (RejectValueType aRejectValue) -> void { holder->Reject(Move(aRejectValue)); }
      );
    }
    return holder->Promise();
  }

  class Request : public MozPromiseRefcountable
  {
  public:
    virtual void Disconnect() = 0;

  protected:
    Request() : mComplete(false), mDisconnected(false) {}
    virtual ~Request() {}

    bool mComplete;
    bool mDisconnected;
  };

protected:

  /*
   * A ThenValue tracks a single consumer waiting on the promise. When a consumer
   * invokes promise->Then(...), a ThenValue is created. Once the Promise is
   * resolved or rejected, a {Resolve,Reject}Runnable is dispatched, which
   * invokes the resolve/reject method and then deletes the ThenValue.
   */
  class ThenValueBase : public Request
  {
    friend class MozPromise;
    static const uint32_t sMagic = 0xfadece11;

  public:
    class ResolveOrRejectRunnable : public CancelableRunnable
    {
    public:
      ResolveOrRejectRunnable(ThenValueBase* aThenValue, MozPromise* aPromise)
        : mThenValue(aThenValue)
        , mPromise(aPromise)
      {
        MOZ_DIAGNOSTIC_ASSERT(!mPromise->IsPending());
      }

      ~ResolveOrRejectRunnable()
      {
        if (mThenValue) {
          mThenValue->AssertIsDead();
        }
      }

      NS_IMETHOD Run() override
      {
        PROMISE_LOG("ResolveOrRejectRunnable::Run() [this=%p]", this);
        mThenValue->DoResolveOrReject(mPromise->Value());
        mThenValue = nullptr;
        mPromise = nullptr;
        return NS_OK;
      }

      nsresult Cancel() override
      {
        return Run();
      }

    private:
      RefPtr<ThenValueBase> mThenValue;
      RefPtr<MozPromise> mPromise;
    };

    ThenValueBase(AbstractThread* aResponseTarget,
                  const char* aCallSite)
      : mResponseTarget(aResponseTarget)
      , mCallSite(aCallSite)
    {
      MOZ_ASSERT(aResponseTarget);
    }

#ifdef PROMISE_DEBUG
    ~ThenValueBase()
    {
      mMagic1 = 0;
      mMagic2 = 0;
    }
#endif

    void AssertIsDead()
    {
      PROMISE_ASSERT(mMagic1 == sMagic && mMagic2 == sMagic);
      // We want to assert that this ThenValues is dead - that is to say, that
      // there are no consumers waiting for the result. In the case of a normal
      // ThenValue, we check that it has been disconnected, which is the way
      // that the consumer signals that it no longer wishes to hear about the
      // result. If this ThenValue has a completion promise (which is mutually
      // exclusive with being disconnectable), we recursively assert that every
      // ThenValue associated with the completion promise is dead.
      if (mCompletionPromise) {
        mCompletionPromise->AssertIsDead();
      } else {
        MOZ_DIAGNOSTIC_ASSERT(Request::mDisconnected);
      }
    }

    void Dispatch(MozPromise *aPromise)
    {
      PROMISE_ASSERT(mMagic1 == sMagic && mMagic2 == sMagic);
      aPromise->mMutex.AssertCurrentThreadOwns();
      MOZ_ASSERT(!aPromise->IsPending());

      nsCOMPtr<nsIRunnable> r = new ResolveOrRejectRunnable(this, aPromise);
      PROMISE_LOG("%s Then() call made from %s [Runnable=%p, Promise=%p, ThenValue=%p]",
                  aPromise->mValue.IsResolve() ? "Resolving" : "Rejecting", mCallSite,
                  r.get(), aPromise, this);

      // Promise consumers are allowed to disconnect the Request object and
      // then shut down the thread or task queue that the promise result would
      // be dispatched on. So we unfortunately can't assert that promise
      // dispatch succeeds. :-(
      mResponseTarget->Dispatch(r.forget(), AbstractThread::DontAssertDispatchSuccess);
    }

    void Disconnect() override
    {
      MOZ_DIAGNOSTIC_ASSERT(mResponseTarget->IsCurrentThreadIn());
      MOZ_DIAGNOSTIC_ASSERT(!Request::mComplete);
      Request::mDisconnected = true;

      // We could support rejecting the completion promise on disconnection, but
      // then we'd need to have some sort of default reject value. The use cases
      // of disconnection and completion promise chaining seem pretty orthogonal,
      // so let's use assert against it.
      MOZ_DIAGNOSTIC_ASSERT(!mCompletionPromise);
    }

  protected:
    virtual already_AddRefed<MozPromise> DoResolveOrRejectInternal(ResolveOrRejectValue& aValue) = 0;

    void DoResolveOrReject(ResolveOrRejectValue& aValue)
    {
      PROMISE_ASSERT(mMagic1 == sMagic && mMagic2 == sMagic);
      MOZ_DIAGNOSTIC_ASSERT(mResponseTarget->IsCurrentThreadIn());
      Request::mComplete = true;
      if (Request::mDisconnected) {
        PROMISE_LOG("ThenValue::DoResolveOrReject disconnected - bailing out [this=%p]", this);
        return;
      }

      // Invoke the resolve or reject method.
      RefPtr<MozPromise> result = DoResolveOrRejectInternal(aValue);

      MOZ_DIAGNOSTIC_ASSERT(!mCompletionPromise || result,
        "Can't do promise chaining for a non-promise-returning method.");

      if (mCompletionPromise && result) {
        result->ChainTo(mCompletionPromise.forget(), "<chained completion promise>");
      }
    }

    RefPtr<AbstractThread> mResponseTarget; // May be released on any thread.
#ifdef PROMISE_DEBUG
    uint32_t mMagic1 = sMagic;
#endif
    RefPtr<Private> mCompletionPromise;
#ifdef PROMISE_DEBUG
    uint32_t mMagic2 = sMagic;
#endif
    const char* mCallSite;
  };

  /*
   * We create two overloads for invoking Resolve/Reject Methods so as to
   * make the resolve/reject value argument "optional".
   */

  template<typename ThisType, typename MethodType, typename ValueType>
  static typename EnableIf<ReturnTypeIs<MethodType, RefPtr<MozPromise>>::value &&
                           TakesArgument<MethodType>::value,
                           already_AddRefed<MozPromise>>::Type
  InvokeCallbackMethod(ThisType* aThisVal, MethodType aMethod, ValueType&& aValue)
  {
    return ((*aThisVal).*aMethod)(Forward<ValueType>(aValue)).forget();
  }

  template<typename ThisType, typename MethodType, typename ValueType>
  static typename EnableIf<ReturnTypeIs<MethodType, void>::value &&
                           TakesArgument<MethodType>::value,
                           already_AddRefed<MozPromise>>::Type
  InvokeCallbackMethod(ThisType* aThisVal, MethodType aMethod, ValueType&& aValue)
  {
    ((*aThisVal).*aMethod)(Forward<ValueType>(aValue));
    return nullptr;
  }

  template<typename ThisType, typename MethodType, typename ValueType>
  static typename EnableIf<ReturnTypeIs<MethodType, RefPtr<MozPromise>>::value &&
                           !TakesArgument<MethodType>::value,
                           already_AddRefed<MozPromise>>::Type
  InvokeCallbackMethod(ThisType* aThisVal, MethodType aMethod, ValueType&& aValue)
  {
    return ((*aThisVal).*aMethod)().forget();
  }

  template<typename ThisType, typename MethodType, typename ValueType>
  static typename EnableIf<ReturnTypeIs<MethodType, void>::value &&
                           !TakesArgument<MethodType>::value,
                           already_AddRefed<MozPromise>>::Type
  InvokeCallbackMethod(ThisType* aThisVal, MethodType aMethod, ValueType&& aValue)
  {
    ((*aThisVal).*aMethod)();
    return nullptr;
  }

  template<typename ThisType, typename ResolveMethodType, typename RejectMethodType>
  class MethodThenValue : public ThenValueBase
  {
  public:
    MethodThenValue(AbstractThread* aResponseTarget, ThisType* aThisVal,
                    ResolveMethodType aResolveMethod, RejectMethodType aRejectMethod,
                    const char* aCallSite)
      : ThenValueBase(aResponseTarget, aCallSite)
      , mThisVal(aThisVal)
      , mResolveMethod(aResolveMethod)
      , mRejectMethod(aRejectMethod) {}

    void Disconnect() override
    {
      ThenValueBase::Disconnect();

      // If a Request has been disconnected, we don't guarantee that the
      // resolve/reject runnable will be dispatched. Null out our refcounted
      // this-value now so that it's released predictably on the dispatch thread.
      mThisVal = nullptr;
    }

  protected:
    already_AddRefed<MozPromise> DoResolveOrRejectInternal(ResolveOrRejectValue& aValue) override
    {
      RefPtr<MozPromise> completion;
      if (aValue.IsResolve()) {
        completion = InvokeCallbackMethod(
          mThisVal.get(), mResolveMethod, MaybeMove(aValue.ResolveValue()));
      } else {
        completion = InvokeCallbackMethod(
          mThisVal.get(), mRejectMethod, MaybeMove(aValue.RejectValue()));
      }

      // Null out mThisVal after invoking the callback so that any references are
      // released predictably on the dispatch thread. Otherwise, it would be
      // released on whatever thread last drops its reference to the ThenValue,
      // which may or may not be ok.
      mThisVal = nullptr;

      return completion.forget();
    }

  private:
    RefPtr<ThisType> mThisVal; // Only accessed and refcounted on dispatch thread.
    ResolveMethodType mResolveMethod;
    RejectMethodType mRejectMethod;
  };

  // Specialization of MethodThenValue (with 3rd template arg being 'void')
  // that only takes one method, to be called with a ResolveOrRejectValue.
  template<typename ThisType, typename ResolveRejectMethodType>
  class MethodThenValue<ThisType, ResolveRejectMethodType, void> : public ThenValueBase
  {
  public:
    MethodThenValue(AbstractThread* aResponseTarget, ThisType* aThisVal,
                    ResolveRejectMethodType aResolveRejectMethod,
                    const char* aCallSite)
      : ThenValueBase(aResponseTarget, aCallSite)
      , mThisVal(aThisVal)
      , mResolveRejectMethod(aResolveRejectMethod)
    {}

    void Disconnect() override
    {
      ThenValueBase::Disconnect();

      // If a Request has been disconnected, we don't guarantee that the
      // resolve/reject runnable will be dispatched. Null out our refcounted
      // this-value now so that it's released predictably on the dispatch thread.
      mThisVal = nullptr;
    }

  protected:
    already_AddRefed<MozPromise> DoResolveOrRejectInternal(ResolveOrRejectValue& aValue) override
    {
      RefPtr<MozPromise> completion = InvokeCallbackMethod(
        mThisVal.get(), mResolveRejectMethod, MaybeMove(aValue));

      // Null out mThisVal after invoking the callback so that any references are
      // released predictably on the dispatch thread. Otherwise, it would be
      // released on whatever thread last drops its reference to the ThenValue,
      // which may or may not be ok.
      mThisVal = nullptr;

      return completion.forget();
    }

  private:
    RefPtr<ThisType> mThisVal; // Only accessed and refcounted on dispatch thread.
    ResolveRejectMethodType mResolveRejectMethod;
  };

  // NB: We could use std::function here instead of a template if it were supported. :-(
  template<typename ResolveFunction, typename RejectFunction>
  class FunctionThenValue : public ThenValueBase
  {
  public:
    FunctionThenValue(AbstractThread* aResponseTarget,
                      ResolveFunction&& aResolveFunction,
                      RejectFunction&& aRejectFunction,
                      const char* aCallSite)
      : ThenValueBase(aResponseTarget, aCallSite)
    {
      mResolveFunction.emplace(Move(aResolveFunction));
      mRejectFunction.emplace(Move(aRejectFunction));
    }

    void Disconnect() override
    {
      ThenValueBase::Disconnect();

      // If a Request has been disconnected, we don't guarantee that the
      // resolve/reject runnable will be dispatched. Destroy our callbacks
      // now so that any references in closures are released predictable on
      // the dispatch thread.
      mResolveFunction.reset();
      mRejectFunction.reset();
    }

  protected:
    already_AddRefed<MozPromise> DoResolveOrRejectInternal(ResolveOrRejectValue& aValue) override
    {
      // Note: The usage of InvokeCallbackMethod here requires that
      // ResolveFunction/RejectFunction are capture-lambdas (i.e. anonymous
      // classes with ::operator()), since it allows us to share code more easily.
      // We could fix this if need be, though it's quite easy to work around by
      // just capturing something.
      RefPtr<MozPromise> completion;
      if (aValue.IsResolve()) {
        completion = InvokeCallbackMethod(mResolveFunction.ptr(),
          &ResolveFunction::operator(), MaybeMove(aValue.ResolveValue()));
      } else {
        completion = InvokeCallbackMethod(mRejectFunction.ptr(),
          &RejectFunction::operator(), MaybeMove(aValue.RejectValue()));
      }

      // Destroy callbacks after invocation so that any references in closures are
      // released predictably on the dispatch thread. Otherwise, they would be
      // released on whatever thread last drops its reference to the ThenValue,
      // which may or may not be ok.
      mResolveFunction.reset();
      mRejectFunction.reset();

      return completion.forget();
    }

  private:
    Maybe<ResolveFunction> mResolveFunction; // Only accessed and deleted on dispatch thread.
    Maybe<RejectFunction> mRejectFunction; // Only accessed and deleted on dispatch thread.
  };

  // Specialization of FunctionThenValue (with 2nd template arg being 'void')
  // that only takes one function, to be called with a ResolveOrRejectValue.
  template<typename ResolveRejectFunction>
  class FunctionThenValue<ResolveRejectFunction, void> : public ThenValueBase
  {
  public:
    FunctionThenValue(AbstractThread* aResponseTarget,
                      ResolveRejectFunction&& aResolveRejectFunction,
                      const char* aCallSite)
      : ThenValueBase(aResponseTarget, aCallSite)
    {
      mResolveRejectFunction.emplace(Move(aResolveRejectFunction));
    }

    void Disconnect() override
    {
      ThenValueBase::Disconnect();

      // If a Request has been disconnected, we don't guarantee that the
      // resolve/reject runnable will be dispatched. Destroy our callbacks
      // now so that any references in closures are released predictable on
      // the dispatch thread.
      mResolveRejectFunction.reset();
    }

  protected:
    already_AddRefed<MozPromise> DoResolveOrRejectInternal(ResolveOrRejectValue& aValue) override
    {
      // Note: The usage of InvokeCallbackMethod here requires that
      // ResolveRejectFunction is capture-lambdas (i.e. anonymous
      // classes with ::operator()), since it allows us to share code more easily.
      // We could fix this if need be, though it's quite easy to work around by
      // just capturing something.
      RefPtr<MozPromise> completion =
        InvokeCallbackMethod(mResolveRejectFunction.ptr(),
                             &ResolveRejectFunction::operator(),
                             MaybeMove(aValue));

      // Destroy callbacks after invocation so that any references in closures are
      // released predictably on the dispatch thread. Otherwise, they would be
      // released on whatever thread last drops its reference to the ThenValue,
      // which may or may not be ok.
      mResolveRejectFunction.reset();

      return completion.forget();
    }

  private:
    Maybe<ResolveRejectFunction> mResolveRejectFunction; // Only accessed and deleted on dispatch thread.
  };

public:
  void ThenInternal(AbstractThread* aResponseThread, ThenValueBase* aThenValue,
                    const char* aCallSite)
  {
    PROMISE_ASSERT(mMagic1 == sMagic && mMagic2 == sMagic && mMagic3 == sMagic && mMagic4 == &mMutex);
    MOZ_ASSERT(aResponseThread);
    MutexAutoLock lock(mMutex);
    MOZ_DIAGNOSTIC_ASSERT(!IsExclusive || !mHaveRequest);
    mHaveRequest = true;
    PROMISE_LOG("%s invoking Then() [this=%p, aThenValue=%p, isPending=%d]",
                aCallSite, this, aThenValue, (int) IsPending());
    if (!IsPending()) {
      aThenValue->Dispatch(this);
    } else {
      mThenValues.AppendElement(aThenValue);
    }
  }

private:
  /*
   * A command object to store all information needed to make a request to
   * the promise. This allows us to delay the request until further use is
   * known (whether it is ->Then() again for more promise chaining or ->Track()
   * to terminate chaining and issue the request).
   *
   * This allows a unified syntax for promise chaining and disconnection
   * and feels more like its JS counterpart.
   */
  template <bool SupportChaining>
  class ThenCommand
  {
    friend class MozPromise;

    ThenCommand(AbstractThread* aResponseThread,
                const char* aCallSite,
                already_AddRefed<ThenValueBase> aThenValue,
                MozPromise* aReceiver)
      : mResponseThread(aResponseThread)
      , mCallSite(aCallSite)
      , mThenValue(aThenValue)
      , mReceiver(aReceiver)
    {
      MOZ_ASSERT(aResponseThread);
    }

    ThenCommand(ThenCommand&& aOther) = default;

  public:
    ~ThenCommand()
    {
      // Issue the request now if the return value of Then() is not used.
      if (mThenValue) {
        mReceiver->ThenInternal(mResponseThread, mThenValue, mCallSite);
      }
    }

    // Allow RefPtr<MozPromise> p = somePromise->Then();
    //       p->Then(thread1, ...);
    //       p->Then(thread2, ...);
    template <typename...>
    operator RefPtr<MozPromise>()
    {
      static_assert(SupportChaining,
        "The resolve/reject callback needs to return a RefPtr<MozPromise> "
        "in order to do promise chaining.");

      RefPtr<ThenValueBase> thenValue = mThenValue.forget();
      // mCompletionPromise must be created before ThenInternal() to avoid race.
      RefPtr<MozPromise::Private> p = new MozPromise::Private(
        "<completion promise>", true /* aIsCompletionPromise */);
      thenValue->mCompletionPromise = p;
      // Note ThenInternal() might nullify mCompletionPromise before return.
      // So we need to return p instead of mCompletionPromise.
      mReceiver->ThenInternal(mResponseThread, thenValue, mCallSite);
      return p;
    }

    template <typename... Ts>
    auto Then(Ts&&... aArgs)
      -> decltype(DeclVal<MozPromise>().Then(Forward<Ts>(aArgs)...))
    {
      return static_cast<RefPtr<MozPromise>>(*this)->Then(Forward<Ts>(aArgs)...);
    }

    void Track(MozPromiseRequestHolder<MozPromise>& aRequestHolder)
    {
      RefPtr<ThenValueBase> thenValue = mThenValue.forget();
      mReceiver->ThenInternal(mResponseThread, thenValue, mCallSite);
      aRequestHolder.Track(thenValue.forget());
    }

    // Allow calling ->Then() again for more promise chaining or ->Track() to
    // end chaining and track the request for future disconnection.
    ThenCommand* operator->()
    {
      return this;
    }

  private:
    AbstractThread* mResponseThread;
    const char* mCallSite;
    RefPtr<ThenValueBase> mThenValue;
    MozPromise* mReceiver;
  };

  template<typename Method>
  using MethodReturnPromise =
    ReturnTypeIs<Method, RefPtr<MozPromise>>;

  template<typename Function>
  using FunctionReturnPromise =
    MethodReturnPromise<decltype(&Function::operator())>;

  template <typename M1, typename... Ms>
  struct MethodThenCommand
  {
    static const bool value =
      MethodThenCommand<M1>::value && MethodThenCommand<Ms...>::value;
    using type = ThenCommand<value>;
  };

  template <typename M1>
  struct MethodThenCommand<M1>
  {
    static const bool value = MethodReturnPromise<M1>::value;
    using type = ThenCommand<value>;
  };

  template <typename F1, typename... Fs>
  struct FunctionThenCommand
  {
    static const bool value =
      FunctionThenCommand<F1>::value && FunctionThenCommand<Fs...>::value;
    using type = ThenCommand<value>;
  };

  template <typename F1>
  struct FunctionThenCommand<F1>
  {
    static const bool value = FunctionReturnPromise<F1>::value;
    using type = ThenCommand<value>;
  };

public:
  template<typename ThisType, typename ResolveMethodType, typename RejectMethodType>
  typename MethodThenCommand<ResolveMethodType, RejectMethodType>::type
  Then(AbstractThread* aResponseThread, const char* aCallSite,
    ThisType* aThisVal, ResolveMethodType aResolveMethod, RejectMethodType aRejectMethod)
  {
    using ThenType = MethodThenValue<ThisType, ResolveMethodType, RejectMethodType>;
    RefPtr<ThenValueBase> thenValue = new ThenType(aResponseThread,
       aThisVal, aResolveMethod, aRejectMethod, aCallSite);
    return typename MethodThenCommand<ResolveMethodType, RejectMethodType>::type(
      aResponseThread, aCallSite, thenValue.forget(), this);
  }

  template<typename ThisType, typename ResolveRejectMethodType>
  typename MethodThenCommand<ResolveRejectMethodType>::type
  Then(AbstractThread* aResponseThread, const char* aCallSite,
    ThisType* aThisVal, ResolveRejectMethodType aResolveRejectMethod)
  {
    using ThenType = MethodThenValue<ThisType, ResolveRejectMethodType, void>;
    RefPtr<ThenValueBase> thenValue = new ThenType(aResponseThread,
       aThisVal, aResolveRejectMethod, aCallSite);
    return typename MethodThenCommand<ResolveRejectMethodType>::type(
      aResponseThread, aCallSite, thenValue.forget(), this);
  }

  template<typename ResolveFunction, typename RejectFunction>
  typename FunctionThenCommand<ResolveFunction, RejectFunction>::type
  Then(AbstractThread* aResponseThread, const char* aCallSite,
    ResolveFunction&& aResolveFunction, RejectFunction&& aRejectFunction)
  {
    using ThenType = FunctionThenValue<ResolveFunction, RejectFunction>;
    RefPtr<ThenValueBase> thenValue = new ThenType(aResponseThread,
      Move(aResolveFunction), Move(aRejectFunction), aCallSite);
    return typename FunctionThenCommand<ResolveFunction, RejectFunction>::type(
      aResponseThread, aCallSite, thenValue.forget(), this);
  }

  template<typename ResolveRejectFunction>
  typename FunctionThenCommand<ResolveRejectFunction>::type
  Then(AbstractThread* aResponseThread, const char* aCallSite,
                   ResolveRejectFunction&& aResolveRejectFunction)
  {
    using ThenType = FunctionThenValue<ResolveRejectFunction, void>;
    RefPtr<ThenValueBase> thenValue = new ThenType(aResponseThread,
      Move(aResolveRejectFunction), aCallSite);
    return typename FunctionThenCommand<ResolveRejectFunction>::type(
      aResponseThread, aCallSite, thenValue.forget(), this);
  }

  void ChainTo(already_AddRefed<Private> aChainedPromise, const char* aCallSite)
  {
    MutexAutoLock lock(mMutex);
    MOZ_DIAGNOSTIC_ASSERT(!IsExclusive || !mHaveRequest);
    mHaveRequest = true;
    RefPtr<Private> chainedPromise = aChainedPromise;
    PROMISE_LOG("%s invoking Chain() [this=%p, chainedPromise=%p, isPending=%d]",
                aCallSite, this, chainedPromise.get(), (int) IsPending());
    if (!IsPending()) {
      ForwardTo(chainedPromise);
    } else {
      mChainedPromises.AppendElement(chainedPromise);
    }
  }

  // Note we expose the function AssertIsDead() instead of IsDead() since
  // checking IsDead() is a data race in the situation where the request is not
  // dead. Therefore we enforce the form |Assert(IsDead())| by exposing
  // AssertIsDead() only.
  void AssertIsDead()
  {
    PROMISE_ASSERT(mMagic1 == sMagic && mMagic2 == sMagic && mMagic3 == sMagic && mMagic4 == &mMutex);
    MutexAutoLock lock(mMutex);
    for (auto&& then : mThenValues) {
      then->AssertIsDead();
    }
    for (auto&& chained : mChainedPromises) {
      chained->AssertIsDead();
    }
  }

protected:
  bool IsPending() const { return mValue.IsNothing(); }

  ResolveOrRejectValue& Value()
  {
    // This method should only be called once the value has stabilized. As
    // such, we don't need to acquire the lock here.
    MOZ_DIAGNOSTIC_ASSERT(!IsPending());
    return mValue;
  }

  void DispatchAll()
  {
    mMutex.AssertCurrentThreadOwns();
    for (size_t i = 0; i < mThenValues.Length(); ++i) {
      mThenValues[i]->Dispatch(this);
    }
    mThenValues.Clear();

    for (size_t i = 0; i < mChainedPromises.Length(); ++i) {
      ForwardTo(mChainedPromises[i]);
    }
    mChainedPromises.Clear();
  }

  void ForwardTo(Private* aOther)
  {
    MOZ_ASSERT(!IsPending());
    if (mValue.IsResolve()) {
      aOther->Resolve(MaybeMove(mValue.ResolveValue()), "<chained promise>");
    } else {
      aOther->Reject(MaybeMove(mValue.RejectValue()), "<chained promise>");
    }
  }

  virtual ~MozPromise()
  {
    PROMISE_LOG("MozPromise::~MozPromise [this=%p]", this);
    AssertIsDead();
    // We can't guarantee a completion promise will always be revolved or
    // rejected since ResolveOrRejectRunnable might not run when dispatch fails.
    if (!mIsCompletionPromise) {
      MOZ_ASSERT(!IsPending());
      MOZ_ASSERT(mThenValues.IsEmpty());
      MOZ_ASSERT(mChainedPromises.IsEmpty());
    }
#ifdef PROMISE_DEBUG
    mMagic1 = 0;
    mMagic2 = 0;
    mMagic3 = 0;
    mMagic4 = nullptr;
#endif
  };

  const char* mCreationSite; // For logging
  Mutex mMutex;
  ResolveOrRejectValue mValue;
#ifdef PROMISE_DEBUG
  uint32_t mMagic1 = sMagic;
#endif
  // Try shows we never have more than 3 elements when IsExclusive is false.
  // So '3' is a good value to avoid heap allocation in most cases.
  AutoTArray<RefPtr<ThenValueBase>, IsExclusive ? 1 : 3> mThenValues;
#ifdef PROMISE_DEBUG
  uint32_t mMagic2 = sMagic;
#endif
  nsTArray<RefPtr<Private>> mChainedPromises;
#ifdef PROMISE_DEBUG
  uint32_t mMagic3 = sMagic;
#endif
  bool mHaveRequest;
  const bool mIsCompletionPromise;
#ifdef PROMISE_DEBUG
  void* mMagic4;
#endif
};

template<typename ResolveValueT, typename RejectValueT, bool IsExclusive>
class MozPromise<ResolveValueT, RejectValueT, IsExclusive>::Private
  : public MozPromise<ResolveValueT, RejectValueT, IsExclusive>
{
public:
  explicit Private(const char* aCreationSite, bool aIsCompletionPromise = false)
    : MozPromise(aCreationSite, aIsCompletionPromise) {}

  template<typename ResolveValueT_>
  void Resolve(ResolveValueT_&& aResolveValue, const char* aResolveSite)
  {
    PROMISE_ASSERT(mMagic1 == sMagic && mMagic2 == sMagic && mMagic3 == sMagic && mMagic4 == &mMutex);
    MutexAutoLock lock(mMutex);
    PROMISE_LOG("%s resolving MozPromise (%p created at %s)", aResolveSite, this, mCreationSite);
    if (!IsPending()) {
      PROMISE_LOG("%s ignored already resolved or rejected MozPromise (%p created at %s)", aResolveSite, this, mCreationSite);
      return;
    }
    mValue.SetResolve(Forward<ResolveValueT_>(aResolveValue));
    DispatchAll();
  }

  template<typename RejectValueT_>
  void Reject(RejectValueT_&& aRejectValue, const char* aRejectSite)
  {
    PROMISE_ASSERT(mMagic1 == sMagic && mMagic2 == sMagic && mMagic3 == sMagic && mMagic4 == &mMutex);
    MutexAutoLock lock(mMutex);
    PROMISE_LOG("%s rejecting MozPromise (%p created at %s)", aRejectSite, this, mCreationSite);
    if (!IsPending()) {
      PROMISE_LOG("%s ignored already resolved or rejected MozPromise (%p created at %s)", aRejectSite, this, mCreationSite);
      return;
    }
    mValue.SetReject(Forward<RejectValueT_>(aRejectValue));
    DispatchAll();
  }

  template<typename ResolveOrRejectValue_>
  void ResolveOrReject(ResolveOrRejectValue_&& aValue, const char* aSite)
  {
    PROMISE_ASSERT(mMagic1 == sMagic && mMagic2 == sMagic && mMagic3 == sMagic && mMagic4 == &mMutex);
    MutexAutoLock lock(mMutex);
    PROMISE_LOG("%s resolveOrRejecting MozPromise (%p created at %s)", aSite, this, mCreationSite);
    if (!IsPending()) {
      PROMISE_LOG("%s ignored already resolved or rejected MozPromise (%p created at %s)", aSite, this, mCreationSite);
      return;
    }
    mValue = Forward<ResolveOrRejectValue_>(aValue);
    DispatchAll();
  }
};

// A generic promise type that does the trick for simple use cases.
typedef MozPromise<bool, nsresult, /* IsExclusive = */ false> GenericPromise;

/*
 * Class to encapsulate a promise for a particular role. Use this as the member
 * variable for a class whose method returns a promise.
 */
template<typename PromiseType>
class MozPromiseHolder
{
public:
  MozPromiseHolder()
    : mMonitor(nullptr) {}

  MozPromiseHolder(MozPromiseHolder&& aOther)
    : mMonitor(nullptr), mPromise(aOther.mPromise.forget()) {}

  // Move semantics.
  MozPromiseHolder& operator=(MozPromiseHolder&& aOther)
  {
    MOZ_ASSERT(!mMonitor && !aOther.mMonitor);
    MOZ_DIAGNOSTIC_ASSERT(!mPromise);
    mPromise = aOther.mPromise;
    aOther.mPromise = nullptr;
    return *this;
  }

  ~MozPromiseHolder() { MOZ_ASSERT(!mPromise); }

  already_AddRefed<PromiseType> Ensure(const char* aMethodName) {
    if (mMonitor) {
      mMonitor->AssertCurrentThreadOwns();
    }
    if (!mPromise) {
      mPromise = new (typename PromiseType::Private)(aMethodName);
    }
    RefPtr<PromiseType> p = mPromise.get();
    return p.forget();
  }

  // Provide a Monitor that should always be held when accessing this instance.
  void SetMonitor(Monitor* aMonitor) { mMonitor = aMonitor; }

  bool IsEmpty() const
  {
    if (mMonitor) {
      mMonitor->AssertCurrentThreadOwns();
    }
    return !mPromise;
  }

  already_AddRefed<typename PromiseType::Private> Steal()
  {
    if (mMonitor) {
      mMonitor->AssertCurrentThreadOwns();
    }

    RefPtr<typename PromiseType::Private> p = mPromise;
    mPromise = nullptr;
    return p.forget();
  }

  void Resolve(const typename PromiseType::ResolveValueType& aResolveValue,
               const char* aMethodName)
  {
    if (mMonitor) {
      mMonitor->AssertCurrentThreadOwns();
    }
    MOZ_ASSERT(mPromise);
    mPromise->Resolve(aResolveValue, aMethodName);
    mPromise = nullptr;
  }
  void Resolve(typename PromiseType::ResolveValueType&& aResolveValue,
               const char* aMethodName)
  {
    if (mMonitor) {
      mMonitor->AssertCurrentThreadOwns();
    }
    MOZ_ASSERT(mPromise);
    mPromise->Resolve(Move(aResolveValue), aMethodName);
    mPromise = nullptr;
  }

  void ResolveIfExists(const typename PromiseType::ResolveValueType& aResolveValue,
                       const char* aMethodName)
  {
    if (!IsEmpty()) {
      Resolve(aResolveValue, aMethodName);
    }
  }
  void ResolveIfExists(typename PromiseType::ResolveValueType&& aResolveValue,
                       const char* aMethodName)
  {
    if (!IsEmpty()) {
      Resolve(Move(aResolveValue), aMethodName);
    }
  }

  void Reject(const typename PromiseType::RejectValueType& aRejectValue,
              const char* aMethodName)
  {
    if (mMonitor) {
      mMonitor->AssertCurrentThreadOwns();
    }
    MOZ_ASSERT(mPromise);
    mPromise->Reject(aRejectValue, aMethodName);
    mPromise = nullptr;
  }
  void Reject(typename PromiseType::RejectValueType&& aRejectValue,
              const char* aMethodName)
  {
    if (mMonitor) {
      mMonitor->AssertCurrentThreadOwns();
    }
    MOZ_ASSERT(mPromise);
    mPromise->Reject(Move(aRejectValue), aMethodName);
    mPromise = nullptr;
  }

  void RejectIfExists(const typename PromiseType::RejectValueType& aRejectValue,
                      const char* aMethodName)
  {
    if (!IsEmpty()) {
      Reject(aRejectValue, aMethodName);
    }
  }
  void RejectIfExists(typename PromiseType::RejectValueType&& aRejectValue,
                      const char* aMethodName)
  {
    if (!IsEmpty()) {
      Reject(Move(aRejectValue), aMethodName);
    }
  }

private:
  Monitor* mMonitor;
  RefPtr<typename PromiseType::Private> mPromise;
};

/*
 * Class to encapsulate a MozPromise::Request reference. Use this as the member
 * variable for a class waiting on a MozPromise.
 */
template<typename PromiseType>
class MozPromiseRequestHolder
{
public:
  MozPromiseRequestHolder() {}
  ~MozPromiseRequestHolder() { MOZ_ASSERT(!mRequest); }

  void Track(RefPtr<typename PromiseType::Request>&& aRequest)
  {
    MOZ_DIAGNOSTIC_ASSERT(!Exists());
    mRequest = Move(aRequest);
  }

  void Complete()
  {
    MOZ_DIAGNOSTIC_ASSERT(Exists());
    mRequest = nullptr;
  }

  // Disconnects and forgets an outstanding promise. The resolve/reject methods
  // will never be called.
  void Disconnect() {
    MOZ_ASSERT(Exists());
    mRequest->Disconnect();
    mRequest = nullptr;
  }

  void DisconnectIfExists() {
    if (Exists()) {
      Disconnect();
    }
  }

  bool Exists() const { return !!mRequest; }

private:
  RefPtr<typename PromiseType::Request> mRequest;
};

template <typename Return>
struct IsMozPromise
  : FalseType
{};

template<typename ResolveValueT, typename RejectValueT, bool IsExclusive>
struct IsMozPromise<MozPromise<ResolveValueT, RejectValueT, IsExclusive>>
  : TrueType
{};

// Asynchronous Potentially-Cross-Thread Method Calls.
//
// This machinery allows callers to schedule a promise-returning function
// (a method and object, or a function object like a lambda) to be invoked
// asynchronously on a given thread, while at the same time receiving a
// promise upon which to invoke Then() immediately. InvokeAsync dispatches a
// task to invoke the function on the proper thread and also chain the
// resulting promise to the one that the caller received, so that resolve/
// reject values are forwarded through.

namespace detail {

// Non-templated base class to allow us to use MOZ_COUNT_{C,D}TOR, which cause
// assertions when used on templated types.
class MethodCallBase
{
public:
  MethodCallBase() { MOZ_COUNT_CTOR(MethodCallBase); }
  virtual ~MethodCallBase() { MOZ_COUNT_DTOR(MethodCallBase); }
};

template<typename PromiseType, typename MethodType, typename ThisType,
         typename... Storages>
class MethodCall : public MethodCallBase
{
public:
  template<typename... Args>
  MethodCall(MethodType aMethod, ThisType* aThisVal, Args&&... aArgs)
    : mMethod(aMethod)
    , mThisVal(aThisVal)
    , mArgs(Forward<Args>(aArgs)...)
  {
    static_assert(sizeof...(Storages) == sizeof...(Args), "Storages and Args should have equal sizes");
  }

  RefPtr<PromiseType> Invoke()
  {
    return mArgs.apply(mThisVal.get(), mMethod);
  }

private:
  MethodType mMethod;
  RefPtr<ThisType> mThisVal;
  RunnableMethodArguments<Storages...> mArgs;
};

template<typename PromiseType, typename MethodType, typename ThisType,
         typename... Storages>
class ProxyRunnable : public CancelableRunnable
{
public:
  ProxyRunnable(typename PromiseType::Private* aProxyPromise,
                MethodCall<PromiseType, MethodType, ThisType, Storages...>* aMethodCall)
    : mProxyPromise(aProxyPromise), mMethodCall(aMethodCall) {}

  NS_IMETHOD Run() override
  {
    RefPtr<PromiseType> p = mMethodCall->Invoke();
    mMethodCall = nullptr;
    p->ChainTo(mProxyPromise.forget(), "<Proxy Promise>");
    return NS_OK;
  }

  nsresult Cancel() override
  {
    return Run();
  }

private:
  RefPtr<typename PromiseType::Private> mProxyPromise;
  nsAutoPtr<MethodCall<PromiseType, MethodType, ThisType, Storages...>> mMethodCall;
};

template<typename... Storages,
         typename PromiseType, typename ThisType, typename... ArgTypes,
         typename... ActualArgTypes>
static RefPtr<PromiseType>
InvokeAsyncImpl(AbstractThread* aTarget, ThisType* aThisVal,
                const char* aCallerName,
                RefPtr<PromiseType>(ThisType::*aMethod)(ArgTypes...),
                ActualArgTypes&&... aArgs)
{
  MOZ_ASSERT(aTarget);

  typedef RefPtr<PromiseType>(ThisType::*MethodType)(ArgTypes...);
  typedef detail::MethodCall<PromiseType, MethodType, ThisType, Storages...> MethodCallType;
  typedef detail::ProxyRunnable<PromiseType, MethodType, ThisType, Storages...> ProxyRunnableType;

  MethodCallType* methodCall =
    new MethodCallType(aMethod, aThisVal, Forward<ActualArgTypes>(aArgs)...);
  RefPtr<typename PromiseType::Private> p = new (typename PromiseType::Private)(aCallerName);
  RefPtr<ProxyRunnableType> r = new ProxyRunnableType(p, methodCall);
  aTarget->Dispatch(r.forget());
  return p.forget();
}

constexpr bool Any()
{
  return false;
}

template <typename T1>
constexpr bool Any(T1 a)
{
  return static_cast<bool>(a);
}

template <typename T1, typename... Ts>
constexpr bool Any(T1 a, Ts... aOthers)
{
  return a || Any(aOthers...);
}

} // namespace detail

// InvokeAsync with explicitly-specified storages.
// See ParameterStorage in nsThreadUtils.h for help.
template<typename... Storages,
         typename PromiseType, typename ThisType, typename... ArgTypes,
         typename... ActualArgTypes,
         typename EnableIf<sizeof...(Storages) != 0, int>::Type = 0>
static RefPtr<PromiseType>
InvokeAsync(AbstractThread* aTarget, ThisType* aThisVal, const char* aCallerName,
            RefPtr<PromiseType>(ThisType::*aMethod)(ArgTypes...),
            ActualArgTypes&&... aArgs)
{
  static_assert(sizeof...(Storages) == sizeof...(ArgTypes),
                "Provided Storages and method's ArgTypes should have equal sizes");
  static_assert(sizeof...(Storages) == sizeof...(ActualArgTypes),
                "Provided Storages and ActualArgTypes should have equal sizes");
  return detail::InvokeAsyncImpl<Storages...>(
           aTarget, aThisVal, aCallerName, aMethod,
           Forward<ActualArgTypes>(aArgs)...);
}

// InvokeAsync with no explicitly-specified storages, will copy arguments and
// then move them out of the runnable into the target method parameters.
template<typename... Storages,
         typename PromiseType, typename ThisType, typename... ArgTypes,
         typename... ActualArgTypes,
         typename EnableIf<sizeof...(Storages) == 0, int>::Type = 0>
static RefPtr<PromiseType>
InvokeAsync(AbstractThread* aTarget, ThisType* aThisVal, const char* aCallerName,
            RefPtr<PromiseType>(ThisType::*aMethod)(ArgTypes...),
            ActualArgTypes&&... aArgs)
{
  static_assert(!detail::Any(IsPointer<typename RemoveReference<ActualArgTypes>::Type>::value...),
                "Cannot pass pointer types through InvokeAsync, Storages must be provided");
  static_assert(sizeof...(ArgTypes) == sizeof...(ActualArgTypes),
                "Method's ArgTypes and ActualArgTypes should have equal sizes");
  return detail::InvokeAsyncImpl<StoreCopyPassByRRef<typename Decay<ActualArgTypes>::Type>...>(
           aTarget, aThisVal, aCallerName, aMethod,
           Forward<ActualArgTypes>(aArgs)...);
}

namespace detail {

template<typename Function, typename PromiseType>
class ProxyFunctionRunnable : public CancelableRunnable
{
  typedef typename Decay<Function>::Type FunctionStorage;
public:
  template <typename F>
  ProxyFunctionRunnable(typename PromiseType::Private* aProxyPromise,
                        F&& aFunction)
    : mProxyPromise(aProxyPromise)
    , mFunction(new FunctionStorage(Forward<F>(aFunction))) {}

  NS_IMETHOD Run() override
  {
    RefPtr<PromiseType> p = (*mFunction)();
    mFunction = nullptr;
    p->ChainTo(mProxyPromise.forget(), "<Proxy Promise>");
    return NS_OK;
  }

  nsresult Cancel() override
  {
    return Run();
  }

private:
  RefPtr<typename PromiseType::Private> mProxyPromise;
  UniquePtr<FunctionStorage> mFunction;
};

// Note: The following struct and function are not for public consumption (yet?)
// as we would prefer all calls to pass on-the-spot lambdas (or at least moved
// function objects). They could be moved outside of detail if really needed.

// We prefer getting function objects by non-lvalue-ref (to avoid copying them
// and their captures). This struct is a tag that allows the use of objects
// through lvalue-refs where necessary.
struct AllowInvokeAsyncFunctionLVRef {};

// Invoke a function object (e.g., lambda or std/mozilla::function)
// asynchronously; note that the object will be copied if provided by lvalue-ref.
// Return a promise that the function should eventually resolve or reject.
template<typename Function>
static auto
InvokeAsync(AbstractThread* aTarget, const char* aCallerName,
            AllowInvokeAsyncFunctionLVRef, Function&& aFunction)
  -> decltype(aFunction())
{
  static_assert(IsRefcountedSmartPointer<decltype(aFunction())>::value
                && IsMozPromise<typename RemoveSmartPointer<
                                           decltype(aFunction())>::Type>::value,
                "Function object must return RefPtr<MozPromise>");
  MOZ_ASSERT(aTarget);
  typedef typename RemoveSmartPointer<decltype(aFunction())>::Type PromiseType;
  typedef detail::ProxyFunctionRunnable<Function, PromiseType> ProxyRunnableType;

  RefPtr<typename PromiseType::Private> p =
    new (typename PromiseType::Private)(aCallerName);
  RefPtr<ProxyRunnableType> r =
    new ProxyRunnableType(p, Forward<Function>(aFunction));
  aTarget->Dispatch(r.forget());
  return p.forget();
}

} // namespace detail

// Invoke a function object (e.g., lambda) asynchronously.
// Return a promise that the function should eventually resolve or reject.
template<typename Function>
static auto
InvokeAsync(AbstractThread* aTarget, const char* aCallerName,
            Function&& aFunction)
  -> decltype(aFunction())
{
  static_assert(!IsLvalueReference<Function>::value,
                "Function object must not be passed by lvalue-ref (to avoid "
                "unplanned copies); Consider move()ing the object.");
  return detail::InvokeAsync(aTarget, aCallerName,
                             detail::AllowInvokeAsyncFunctionLVRef(),
                             Forward<Function>(aFunction));
}

#undef PROMISE_LOG
#undef PROMISE_ASSERT
#undef PROMISE_DEBUG

} // namespace mozilla

#endif

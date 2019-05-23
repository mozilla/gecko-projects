#ifndef mozilla_ipc_IPDLParamTraits_h
#define mozilla_ipc_IPDLParamTraits_h

#include "chrome/common/ipc_message_utils.h"
#include "mozilla/UniquePtr.h"

namespace mozilla {
namespace ipc {

class IProtocol;

//
// IPDLParamTraits are an extended version of ParamTraits. Unlike ParamTraits,
// IPDLParamTraits supports passing an additional IProtocol* argument to the
// write and read methods.
//
// This is important for serializing and deserializing types which require
// knowledge of which protocol they're being sent over, such as actors and
// nsIInputStreams.
//
// All types which already implement ParamTraits also support IPDLParamTraits.
//
template <typename P>
struct IPDLParamTraits {
  // This is the default impl which discards the actor parameter and calls into
  // ParamTraits. Types which want to use the actor parameter must specialize
  // IPDLParamTraits.
  template <typename R>
  static inline void Write(IPC::Message* aMsg, IProtocol*, R&& aParam) {
    IPC::ParamTraits<P>::Write(aMsg, std::forward<R>(aParam));
  }

  template <typename R>
  static inline bool Read(const IPC::Message* aMsg, PickleIterator* aIter,
                          IProtocol*, R* aResult) {
    return IPC::ParamTraits<P>::Read(aMsg, aIter, aResult);
  }
};

//
// WriteIPDLParam and ReadIPDLParam are like IPC::WriteParam and IPC::ReadParam,
// however, they also accept an extra actor argument, and use IPDLParamTraits
// rather than ParamTraits.
//
// NOTE: WriteIPDLParam takes a universal reference, so that it can support
// whatever reference type is supported by the underlying IPDLParamTraits::Write
// implementation. See the comment on IPDLParamTraits<nsTArray<T>>::Write for
// more information.
//
template <typename P>
static MOZ_NEVER_INLINE void WriteIPDLParam(IPC::Message* aMsg,
                                            IProtocol* aActor, P&& aParam) {
  IPDLParamTraits<typename Decay<P>::Type>::Write(aMsg, aActor,
                                                  std::forward<P>(aParam));
}

template <typename P>
static MOZ_NEVER_INLINE bool ReadIPDLParam(const IPC::Message* aMsg,
                                           PickleIterator* aIter,
                                           IProtocol* aActor, P* aResult) {
  return IPDLParamTraits<P>::Read(aMsg, aIter, aActor, aResult);
}

constexpr void WriteIPDLParamList(IPC::Message*, IProtocol*) {}

template <typename P, typename... Ps>
static void WriteIPDLParamList(IPC::Message* aMsg, IProtocol* aActor,
                               P&& aParam, Ps&&... aParams) {
  WriteIPDLParam(aMsg, aActor, std::forward<P>(aParam));
  WriteIPDLParamList(aMsg, aActor, std::forward<Ps>(aParams)...);
}

constexpr bool ReadIPDLParamList(const IPC::Message*, PickleIterator*,
                                 IProtocol*) {
  return true;
}

template <typename P, typename... Ps>
static bool ReadIPDLParamList(const IPC::Message* aMsg, PickleIterator* aIter,
                              IProtocol* aActor, P* aResult, Ps*... aResults) {
  return ReadIPDLParam(aMsg, aIter, aActor, aResult) &&
         ReadIPDLParamList(aMsg, aIter, aActor, aResults...);
}

// When being passed `RefPtr<T>` or `nsCOMPtr<T>`, forward to a specialization
// for the underlying target type. The parameter type will be passed as `T*`,
// and result as `RefPtr<T>*`.
//
// This is done explicitly to ensure that the deleted `&&` overload for
// `operator T*` is not selected in generic contexts, and to support
// deserializing into `nsCOMPtr<T>`.
template <typename T>
struct IPDLParamTraits<RefPtr<T>> {
  static void Write(IPC::Message* aMsg, IProtocol* aActor,
                    const RefPtr<T>& aParam) {
    IPDLParamTraits<T*>::Write(aMsg, aActor, aParam.get());
  }

  static bool Read(const IPC::Message* aMsg, PickleIterator* aIter,
                   IProtocol* aActor, RefPtr<T>* aResult) {
    return IPDLParamTraits<T*>::Read(aMsg, aIter, aActor, aResult);
  }
};

template <typename T>
struct IPDLParamTraits<nsCOMPtr<T>> {
  static void Write(IPC::Message* aMsg, IProtocol* aActor,
                    const nsCOMPtr<T>& aParam) {
    IPDLParamTraits<T*>::Write(aMsg, aActor, aParam.get());
  }

  static bool Read(const IPC::Message* aMsg, PickleIterator* aIter,
                   IProtocol* aActor, nsCOMPtr<T>* aResult) {
    RefPtr<T> refptr;
    if (!IPDLParamTraits<T*>::Read(aMsg, aIter, aActor, &refptr)) {
      return false;
    }
    *aResult = refptr.forget();
    return true;
  }
};

// nsTArray support for IPDLParamTraits
template <typename T>
struct IPDLParamTraits<nsTArray<T>> {
  // Some serializers need to take a mutable reference to their backing object,
  // such as Shmem segments and Byte Buffers. These serializers take the
  // backing data and move it into the IPC layer for efficiency. `Write` uses a
  // forwarding reference as occasionally these types appear inside of IPDL
  // arrays.
  template <typename U>
  static void Write(IPC::Message* aMsg, IProtocol* aActor, U&& aParam) {
    uint32_t length = aParam.Length();
    WriteIPDLParam(aMsg, aActor, length);

    if (sUseWriteBytes) {
      auto pickledLength = CheckedInt<int>(length) * sizeof(T);
      MOZ_RELEASE_ASSERT(pickledLength.isValid());
      aMsg->WriteBytes(aParam.Elements(), pickledLength.value());
    } else {
      WriteValues(aMsg, aActor, std::forward<U>(aParam));
    }
  }

  // This method uses infallible allocation so that an OOM failure will
  // show up as an OOM crash rather than an IPC FatalError.
  static bool Read(const IPC::Message* aMsg, PickleIterator* aIter,
                   IProtocol* aActor, nsTArray<T>* aResult) {
    uint32_t length;
    if (!ReadIPDLParam(aMsg, aIter, aActor, &length)) {
      return false;
    }

    if (sUseWriteBytes) {
      auto pickledLength = CheckedInt<int>(length) * sizeof(T);
      if (!pickledLength.isValid() ||
          !aMsg->HasBytesAvailable(aIter, pickledLength.value())) {
        return false;
      }

      // XXX(nika): This currently default-constructs the backing data before
      // passing it into ReadBytesInto, which is technically unnecessary here.
      // Perhaps we should consider using an API which doesn't initialize the
      // elements?
      T* elements = aResult->AppendElements(length);
      return aMsg->ReadBytesInto(aIter, elements, pickledLength.value());
    }

    // Each ReadIPDLParam<E> may read more than 1 byte each; this is an attempt
    // to minimally validate that the length isn't much larger than what's
    // actually available in aMsg. We cannot use |pickledLength|, like in the
    // codepath above, because ReadIPDLParam can read variable amounts of data
    // from aMsg.
    if (!aMsg->HasBytesAvailable(aIter, length)) {
      return false;
    }

    aResult->SetCapacity(length);

    for (uint32_t index = 0; index < length; index++) {
      T* element = aResult->AppendElement();
      if (!ReadIPDLParam(aMsg, aIter, aActor, element)) {
        return false;
      }
    }
    return true;
  }

 private:
  // Length has already been written. Const overload.
  static void WriteValues(IPC::Message* aMsg, IProtocol* aActor,
                          const nsTArray<T>& aParam) {
    for (auto& elt : aParam) {
      WriteIPDLParam(aMsg, aActor, elt);
    }
  }

  // Length has already been written. Rvalue overload.
  static void WriteValues(IPC::Message* aMsg, IProtocol* aActor,
                          nsTArray<T>&& aParam) {
    for (auto& elt : aParam) {
      WriteIPDLParam(aMsg, aActor, std::move(elt));
    }

    // As we just moved all of our values out, let's clean up after ourselves &
    // clear the input array. This means our move write method will act more
    // like a traditional move constructor.
    aParam.Clear();
  }

  // We write arrays of integer or floating-point data using a single pickling
  // call, rather than writing each element individually.  We deliberately do
  // not use mozilla::IsPod here because it is perfectly reasonable to have
  // a data structure T for which IsPod<T>::value is true, yet also have a
  // {IPDL,}ParamTraits<T> specialization.
  static const bool sUseWriteBytes =
      (mozilla::IsIntegral<T>::value || mozilla::IsFloatingPoint<T>::value);
};

// Maybe support for IPDLParamTraits
template <typename T>
struct IPDLParamTraits<Maybe<T>> {
  typedef Maybe<T> paramType;

  static void Write(IPC::Message* aMsg, IProtocol* aActor,
                    const Maybe<T>& aParam) {
    bool isSome = aParam.isSome();
    WriteIPDLParam(aMsg, aActor, isSome);

    if (isSome) {
      WriteIPDLParam(aMsg, aActor, aParam.ref());
    }
  }

  static void Write(IPC::Message* aMsg, IProtocol* aActor, Maybe<T>&& aParam) {
    bool isSome = aParam.isSome();
    WriteIPDLParam(aMsg, aActor, isSome);

    if (isSome) {
      WriteIPDLParam(aMsg, aActor, std::move(aParam.ref()));
    }
  }

  static bool Read(const IPC::Message* aMsg, PickleIterator* aIter,
                   IProtocol* aActor, Maybe<T>* aResult) {
    bool isSome;
    if (!ReadIPDLParam(aMsg, aIter, aActor, &isSome)) {
      return false;
    }

    if (isSome) {
      aResult->emplace();
      if (!ReadIPDLParam(aMsg, aIter, aActor, aResult->ptr())) {
        return false;
      }
    } else {
      aResult->reset();
    }
    return true;
  }
};

template <typename T>
struct IPDLParamTraits<UniquePtr<T>> {
  typedef UniquePtr<T> paramType;

  template <typename U>
  static void Write(IPC::Message* aMsg, IProtocol* aActor, U&& aParam) {
    bool isNull = aParam == nullptr;
    WriteIPDLParam(aMsg, aActor, isNull);

    if (!isNull) {
      WriteValue(aMsg, aActor, std::forward<U>(aParam));
    }
  }

  static bool Read(const IPC::Message* aMsg, PickleIterator* aIter,
                   IProtocol* aActor, UniquePtr<T>* aResult) {
    bool isNull = true;
    if (!ReadParam(aMsg, aIter, &isNull)) {
      return false;
    }

    if (isNull) {
      aResult->reset();
    } else {
      *aResult = MakeUnique<T>();
      if (!ReadIPDLParam(aMsg, aIter, aActor, aResult->get())) {
        return false;
      }
    }
    return true;
  }

 private:
  // If we have an rvalue, clear out our passed-in parameter.
  static void WriteValue(IPC::Message* aMsg, IProtocol* aActor,
                         UniquePtr<T>&& aParam) {
    WriteIPDLParam(aMsg, aActor, std::move(*aParam.get()));
    aParam = nullptr;
  }

  static void WriteValue(IPC::Message* aMsg, IProtocol* aActor,
                         const UniquePtr<T>& aParam) {
    WriteIPDLParam(aMsg, aActor, *aParam.get());
  }
};

template <typename... Ts>
struct IPDLParamTraits<Tuple<Ts...>> {
  typedef Tuple<Ts...> paramType;

  template <typename U>
  static void Write(IPC::Message* aMsg, IProtocol* aActor, U&& aParam) {
    WriteInternal(aMsg, aActor, std::forward<U>(aParam),
                  std::index_sequence_for<Ts...>{});
  }

  static bool Read(const IPC::Message* aMsg, PickleIterator* aIter,
                   IProtocol* aActor, Tuple<Ts...>* aResult) {
    return ReadInternal(aMsg, aIter, aActor, *aResult,
                        std::index_sequence_for<Ts...>{});
  }

 private:
  template <size_t... Is>
  static void WriteInternal(IPC::Message* aMsg, IProtocol* aActor,
                            const Tuple<Ts...>& aParam,
                            std::index_sequence<Is...>) {
    WriteIPDLParamList(aMsg, aActor, Get<Is>(aParam)...);
  }

  template <size_t... Is>
  static void WriteInternal(IPC::Message* aMsg, IProtocol* aActor,
                            Tuple<Ts...>&& aParam, std::index_sequence<Is...>) {
    WriteIPDLParamList(aMsg, aActor, std::move(Get<Is>(aParam))...);
  }

  template <size_t... Is>
  static bool ReadInternal(const IPC::Message* aMsg, PickleIterator* aIter,
                           IProtocol* aActor, Tuple<Ts...>& aResult,
                           std::index_sequence<Is...>) {
    return ReadIPDLParamList(aMsg, aIter, aActor, &Get<Is>(aResult)...);
  }
};

}  // namespace ipc
}  // namespace mozilla

#endif  // defined(mozilla_ipc_IPDLParamTraits_h)

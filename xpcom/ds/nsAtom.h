/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsAtom_h
#define nsAtom_h

#include "nsISupportsImpl.h"
#include "nsString.h"
#include "nsStringBuffer.h"

namespace mozilla {
struct AtomsSizes;
}

class nsAtom
{
public:
  void AddSizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf,
                              mozilla::AtomsSizes& aSizes) const;

  enum class AtomKind : uint8_t {
    DynamicAtom = 0,
    StaticAtom = 1,
    HTML5Atom = 2,
  };

  bool Equals(char16ptr_t aString, uint32_t aLength) const
  {
    return mLength == aLength &&
           memcmp(mString, aString, mLength * sizeof(char16_t)) == 0;
  }

  bool Equals(const nsAString& aString) const
  {
    return Equals(aString.BeginReading(), aString.Length());
  }

  AtomKind Kind() const { return static_cast<AtomKind>(mKind); }

  bool IsDynamic() const { return Kind() == AtomKind::DynamicAtom; }
  bool IsHTML5()   const { return Kind() == AtomKind::HTML5Atom; }
  bool IsStatic()  const { return Kind() == AtomKind::StaticAtom; }

  char16ptr_t GetUTF16String() const { return mString; }

  uint32_t GetLength() const { return mLength; }

  void ToString(nsAString& aString) const;
  void ToUTF8String(nsACString& aString) const;

  // This is not valid for static atoms. The caller must *not* mutate the
  // string buffer, otherwise all hell will break loose.
  nsStringBuffer* GetStringBuffer() const
  {
    // See the comment on |mString|'s declaration.
    MOZ_ASSERT(IsDynamic() || IsHTML5());
    return nsStringBuffer::FromData(const_cast<char16_t*>(mString));
  }

  // A hashcode that is better distributed than the actual atom pointer, for
  // use in situations that need a well-distributed hashcode. It's called hash()
  // rather than Hash() so we can use mozilla::BloomFilter<N, nsAtom>, because
  // BloomFilter requires elements to implement a function called hash().
  //
  uint32_t hash() const
  {
    MOZ_ASSERT(!IsHTML5());
    return mHash;
  }

  // We can't use NS_INLINE_DECL_THREADSAFE_REFCOUNTING because the refcounting
  // of this type is special.
  MozExternalRefCountType AddRef();
  MozExternalRefCountType Release();

  typedef mozilla::TrueType HasThreadSafeRefCnt;

private:
  friend class nsAtomTable;
  friend class nsAtomSubTable;
  friend class nsHtml5AtomEntry;

protected:
  // Used by nsDynamicAtom and directly (by nsHtml5AtomEntry) for HTML5 atoms.
  nsAtom(AtomKind aKind, const nsAString& aString, uint32_t aHash);

  // Used by nsStaticAtom.
  nsAtom(const char16_t* aString, uint32_t aLength, uint32_t aHash);

  ~nsAtom();

  const uint32_t mLength:30;
  const uint32_t mKind:2; // nsAtom::AtomKind
  const uint32_t mHash;
  // WARNING! For static atoms, this is a pointer to a static char buffer. For
  // non-static atoms it points to the chars in an nsStringBuffer. This means
  // that nsStringBuffer::FromData(mString) calls are only valid for non-static
  // atoms.
  const char16_t* const mString;
};

// A trivial subclass of nsAtom that can be used for known static atoms. The
// main advantage of this class is that it doesn't require refcounting, so you
// can use |nsStaticAtom*| in contrast with |RefPtr<nsAtom>|.
//
// This class would be |final| if it wasn't for nsICSSAnonBoxPseudo and
// nsICSSPseudoElement, which are trivial subclasses used to ensure only
// certain atoms are passed to certain functions.
class nsStaticAtom : public nsAtom
{
public:
  // These are deleted so it's impossible to RefPtr<nsStaticAtom>. Raw
  // nsStaticAtom pointers should be used instead.
  MozExternalRefCountType AddRef() = delete;
  MozExternalRefCountType Release() = delete;

  already_AddRefed<nsAtom> ToAddRefed() {
    return already_AddRefed<nsAtom>(static_cast<nsAtom*>(this));
  }

private:
  friend class nsAtomTable;

  // Construction is done entirely by |friend|s.
  nsStaticAtom(const char16_t* aString, uint32_t aLength, uint32_t aHash)
    : nsAtom(aString, aLength, aHash)
  {}
};

// The four forms of NS_Atomize (for use with |RefPtr<nsAtom>|) return the
// atom for the string given. At any given time there will always be one atom
// representing a given string. Atoms are intended to make string comparison
// cheaper by simplifying it to pointer equality. A pointer to the atom that
// does not own a reference is not guaranteed to be valid.

// Find an atom that matches the given UTF-8 string. The string is assumed to
// be zero terminated. Never returns null.
already_AddRefed<nsAtom> NS_Atomize(const char* aUTF8String);

// Find an atom that matches the given UTF-8 string. Never returns null.
already_AddRefed<nsAtom> NS_Atomize(const nsACString& aUTF8String);

// Find an atom that matches the given UTF-16 string. The string is assumed to
// be zero terminated. Never returns null.
already_AddRefed<nsAtom> NS_Atomize(const char16_t* aUTF16String);

// Find an atom that matches the given UTF-16 string. Never returns null.
already_AddRefed<nsAtom> NS_Atomize(const nsAString& aUTF16String);

// An optimized version of the method above for the main thread.
already_AddRefed<nsAtom> NS_AtomizeMainThread(const nsAString& aUTF16String);

// Return a count of the total number of atoms currently alive in the system.
//
// Note that the result is imprecise and racy if other threads are currently
// operating on atoms. It's also slow, since it triggers a GC before counting.
// Currently this function is only used in tests, which should probably remain
// the case.
nsrefcnt NS_GetNumberOfAtoms();

// Return a pointer for a static atom for the string or null if there's no
// static atom for this string.
nsStaticAtom* NS_GetStaticAtom(const nsAString& aUTF16String);

// Record that all static atoms have been inserted.
void NS_SetStaticAtomsDone();

class nsAtomString : public nsString
{
public:
  explicit nsAtomString(const nsAtom* aAtom) { aAtom->ToString(*this); }
};

class nsAtomCString : public nsCString
{
public:
  explicit nsAtomCString(nsAtom* aAtom) { aAtom->ToUTF8String(*this); }
};

class nsDependentAtomString : public nsDependentString
{
public:
  explicit nsDependentAtomString(const nsAtom* aAtom)
    : nsDependentString(aAtom->GetUTF16String(), aAtom->GetLength())
  {}
};

#endif  // nsAtom_h

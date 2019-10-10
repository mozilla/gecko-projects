/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef frontend_BinASTTokenReaderContext_h
#define frontend_BinASTTokenReaderContext_h

#include "mozilla/Array.h"         // mozilla::Array
#include "mozilla/Assertions.h"    // MOZ_ASSERT
#include "mozilla/Attributes.h"    // MOZ_MUST_USE, MOZ_STACK_CLASS
#include "mozilla/IntegerRange.h"  // mozilla::IntegerRange
#include "mozilla/Maybe.h"         // mozilla::Maybe
#include "mozilla/Variant.h"       // mozilla::Variant

#include <stddef.h>  // size_t
#include <stdint.h>  // uint8_t, uint32_t

#include "frontend/BinASTRuntimeSupport.h"  // CharSlice, BinASTVariant, BinASTKind, BinASTField, BinASTSourceMetadata
#include "frontend/BinASTToken.h"
#include "frontend/BinASTTokenReaderBase.h"  // BinASTTokenReaderBase, SkippableSubTree
#include "js/AllocPolicy.h"                  // SystemAllocPolicy
#include "js/HashTable.h"                    // HashMap, DefaultHasher
#include "js/Result.h"                       // JS::Result, Ok, Error
#include "js/Vector.h"                       // js::Vector

class JSAtom;
class JSTracer;
struct JSContext;

namespace js {

class ScriptSource;

namespace frontend {

class ErrorReporter;

// The format treats several distinct models as the same.
//
// We use `NormalizedInterfaceAndField` as a proxy for `BinASTInterfaceAndField`
// to ensure that we always normalize into the canonical model.
struct NormalizedInterfaceAndField {
  const BinASTInterfaceAndField identity;
  explicit NormalizedInterfaceAndField(BinASTInterfaceAndField identity)
      : identity(identity == BinASTInterfaceAndField::
                                 StaticMemberAssignmentTarget__Property
                     ? BinASTInterfaceAndField::StaticMemberExpression__Property
                     : identity) {}
};

template <typename T>
struct Split {
  T prefix;
  T suffix;
};

// A bunch of bits used to lookup a value in a Huffman table. In most cases,
// these are the 32 leading bits of the underlying bit stream.
//
// In a Huffman table, keys have variable bitlength. Consequently, we only know
// the bitlength of the key *after* we have performed the lookup. A
// `HuffmanLookup` is a data structure contained at least as many bits as
// needed to perform the lookup.
//
// Whenever a lookup is performed, the consumer MUST look at the `bitLength` of
// the returned `HuffmanKey` and consume as many bits from the bit stream.
struct HuffmanLookup {
  HuffmanLookup(const uint32_t bits, const uint8_t bitLength)
      // We zero out the highest `32 - bitLength` bits.
      : bits(bitLength == 0
                 ? 0  // >> 32 is UB
                 : (bits & (uint32_t(0xFFFFFFFF) >> (32 - bitLength)))),
        bitLength(bitLength) {
    MOZ_ASSERT(bitLength <= 32);
    MOZ_ASSERT_IF(bitLength != 32 /* >> 32 is UB */,
                  this->bits >> bitLength == 0);
  }

  // Return the `bitLength` leading bits of this superset, in the order
  // expected to compare to a `HuffmanKey`. The order of bits and bytes
  // is ensured by `BitBuffer`.
  //
  // Note: This only makes sense if `bitLength <= this.bitLength`.
  //
  // So, for instance, if `leadingBits(4)` returns
  // `0b_0000_0000__0000_0000__0000_0000__0000_0100`, this is
  // equal to Huffman Key `0100`.
  uint32_t leadingBits(const uint8_t bitLength) const;

  // Split a HuffmanLookup into a prefix and a suffix.
  //
  // If the value holds at least `prefixLength` bits, the
  // prefix consists in the first `prefixLength` bits and the
  // suffix in the remaining bits.
  //
  // If the value holds fewer bits, the prefix consists in
  // all the bits, with 0 padding at the end to ensure that
  // the prefix contains exactly `prefixLength` bits.
  Split<HuffmanLookup> split(const uint8_t prefixLength) const;

  // The buffer holding the bits. At this stage, bits are stored
  // in the same order as `HuffmanKey`. See the implementation of
  // `BitBuffer` methods for more details about how this order
  // is implemented.
  //
  // If `bitLength < 32`, the unused highest bits are guaranteed
  // to be 0.
  const uint32_t bits;

  // The actual length of buffer `bits`.
  //
  // MUST be within `[0, 32]`.
  //
  // If `bitLength < 32`, it means that some of the highest bits are unused.
  const uint8_t bitLength;

  // Return an iterable data structure representing all possible
  // suffixes of this `HuffmanLookup` with `expectedBitLength`
  // bits.
  //
  // If this `HuffmanLookup` is already at least `expectedBitLength`
  // bits long, we truncate the `HuffmanLookup` to `expectedBitLength`
  // bits and there is only one such suffix.
  mozilla::detail::IntegerRange<size_t> suffixes(
      uint8_t expectedBitLength) const;
};

// A Huffman Key.
struct HuffmanKey {
  // Construct the HuffmanKey.
  //
  // `bits` and `bitLength` define a buffer containing the standard Huffman
  // code for this key.
  //
  // For instance, if the Huffman code is `0100`,
  // - `bits = 0b0000_0000__0000_0000__0000_0000__0000_0100`;
  // - `bitLength = 4`.
  HuffmanKey(const uint32_t bits, const uint8_t bitLength);

  // The buffer holding the bits.
  //
  // For a Huffman code of `0100`
  // - `bits = 0b0000_0000__0000_0000__0000_0000__0000_0100`;
  //
  // If `bitLength < 32`, the unused highest bits are guaranteed
  // to be 0.
  const uint32_t bits;

  // The actual length of buffer `bits`.
  //
  // MUST be within `[0, 32]`.
  //
  // If `bitLength < 32`, it means that some of the highest bits are unused.
  const uint8_t bitLength;
};

// A Huffman key represented as a single `uint32_t`.
struct FlatHuffmanKey {
  explicit FlatHuffmanKey(HuffmanKey key);
  explicit FlatHuffmanKey(const HuffmanKey* key);

  // 0b0000000L_LLLLCCCC_CCCCCCCC_CCCCCCCC
  // Where:
  // - `LLLLL` store `key.bitLength`
  // - `CCCC_CCCCCCCC_CCCCCCCC` store `key.bits`
  //
  // While `key.bits` is nominally 32 bits, it is in fact
  // `MAX_CODE_BIT_LENGTH` bits, padded with 0s in the
  // highest bits.
  const uint32_t representation;

  // -- Implementing HashPolicy
  using Lookup = FlatHuffmanKey;
  using Key = Lookup;
  static HashNumber hash(const Lookup& lookup) {
    return mozilla::DefaultHasher<uint32_t>::hash(lookup.representation);
  }
  static bool match(const Key& key, const Lookup& lookup) {
    return mozilla::DefaultHasher<uint32_t>::match(key.representation,
                                                   lookup.representation);
  }
};

// An entry in a Huffman table.
template <typename T>
struct HuffmanEntry {
  HuffmanEntry(HuffmanKey key, T&& value) : key(key), value(value) {}
  HuffmanEntry(uint32_t bits, uint8_t bitLength, T&& value)
      : key(bits, bitLength), value(value) {}

  const HuffmanKey key;
  const T value;
};

// The default inline buffer length for instances of HuffmanTableValue.
// Specific type (e.g. booleans) will override this to provide something
// more suited to their type.
const size_t HUFFMAN_TABLE_DEFAULT_INLINE_BUFFER_LENGTH = 8;

// A flag that determines only whether a value is `null`.
// Used for optional interface.
enum class Nullable {
  Null,
  NonNull,
};

// An implementation of Huffman Tables as a vector, with `O(entries)`
// lookup. Performance-wise, this implementation only makes sense for
// very short tables.
template <typename T, int N = HUFFMAN_TABLE_DEFAULT_INLINE_BUFFER_LENGTH>
class NaiveHuffmanTable {
 public:
  explicit NaiveHuffmanTable(JSContext* cx) : values(cx) {}
  NaiveHuffmanTable(NaiveHuffmanTable&& other) noexcept
      : values(std::move(other.values)) {}

  // Initialize a Huffman table containing a single value.
  JS::Result<Ok> initWithSingleValue(JSContext* cx, T&& value);

  // Initialize a Huffman table containing `numberOfSymbols`.
  // Symbols must be added with `addSymbol`.
  // If you initialize with `initStart`, you MUST call `initComplete()`
  // at the end of initialization.
  JS::Result<Ok> initStart(JSContext* cx, size_t numberOfSymbols,
                           uint8_t maxBitLength);

  JS::Result<Ok> initComplete();

  // Add a symbol to a value.
  JS::Result<Ok> addSymbol(uint32_t bits, uint8_t bits_length, T&& value);

  NaiveHuffmanTable() = delete;
  NaiveHuffmanTable(NaiveHuffmanTable&) = delete;

  // Lookup a value in the table.
  //
  // The return of this method contains:
  //
  // - the resulting value (`nullptr` if the value is not in the table);
  // - the number of bits in the entry associated to this value.
  //
  // Note that entries inside a single table are typically associated to
  // distinct bit lengths. The caller is responsible for checking
  // the result of this method and advancing the bitstream by
  // `result.key.bitLength` bits.
  HuffmanEntry<const T*> lookup(HuffmanLookup lookup) const;

  // The number of values in the table.
  size_t length() const { return values.length(); }
  const HuffmanEntry<T>* begin() const { return values.begin(); }
  const HuffmanEntry<T>* end() const { return values.end(); }

 private:
  // The entries in this Huffman table.
  // Entries are always ranked by increasing bit_length, and within
  // a bitlength by increasing value of `bits`. This representation
  // is good for small tables, but in the future, we may adopt a
  // representation more optimized for larger tables.
  Vector<HuffmanEntry<T>, N> values;
  friend class HuffmanPreludeReader;
};

// An implementation of Huffman Tables as a hash map. Space-Efficient,
// faster than NaiveHuffmanTable for large tables but not terribly
// fast, either.
//
// Complexity:
//
// - We assume that hashing is sufficient to guarantee `O(1)` lookups
//   inside the hashmap.
// - On a well-formed file, all lookups are successful and a Huffman
//   lookup will take exactly `bitLen` Hashmap lookups. This makes it
//   `O(MAX_CODE_BIT_LENGTH)` worst case. This also makes it
//   `O(ln(N))` in the best case (perfectly balanced Huffman table)
//   and `O(N)` in the worst case (perfectly linear Huffman table),
//   where `N` is the number of entries.
// - On an invalid file, the number of lookups is also bounded by
//   `MAX_CODE_BIT_LENGTH`.
template <typename T>
class MapBasedHuffmanTable {
 public:
  explicit MapBasedHuffmanTable(JSContext* cx) : values(cx), keys(cx) {}
  MapBasedHuffmanTable(MapBasedHuffmanTable&& other) noexcept
      : values(std::move(other.values)), keys(std::move(other.keys)) {}

  // Initialize a Huffman table containing a single value.
  JS::Result<Ok> initWithSingleValue(JSContext* cx, T&& value);

  // Initialize a Huffman table containing `numberOfSymbols`.
  // Symbols must be added with `addSymbol`.
  // If you initialize with `initStart`, you MUST call `initComplete()`
  // at the end of initialization.
  JS::Result<Ok> initStart(JSContext* cx, size_t numberOfSymbols,
                           uint8_t maxBitLength);

  // Add a `(bit, bits_length) => value` mapping.
  JS::Result<Ok> addSymbol(uint32_t bits, uint8_t bits_length, T&& value);

  JS::Result<Ok> initComplete();

  MapBasedHuffmanTable() = delete;
  MapBasedHuffmanTable(MapBasedHuffmanTable&) = delete;

  // Lookup a value in the table.
  //
  // Return an entry with a value of `nullptr` if the value is not in the table.
  //
  // The lookup may advance `key` by `[0, key.bitLength]` bits. Typically, in a
  // table with a single instance, or if the value is not in the table, it
  // will advance by 0 bits. The caller is responsible for advancing its
  // bitstream by `result.key.bitLength` bits.
  HuffmanEntry<const T*> lookup(HuffmanLookup key) const;

  // The number of values in the table.
  size_t length() const { return values.length(); }

  // Iterating in the order of insertion.
  struct Iterator {
    Iterator(const js::HashMap<FlatHuffmanKey, T, FlatHuffmanKey>& values,
             const HuffmanKey* position)
        : values(values), position(position) {}
    void operator++() { ++position; }
    const T* operator*() const {
      const FlatHuffmanKey key(position);
      if (const auto ptr = values.lookup(key)) {
        return &ptr->value();
      }
      MOZ_CRASH();
    }
    bool operator==(const Iterator& other) const {
      MOZ_ASSERT(&values == &other.values);
      return position == other.position;
    }
    bool operator!=(const Iterator& other) const {
      MOZ_ASSERT(&values == &other.values);
      return position != other.position;
    }

   private:
    const js::HashMap<FlatHuffmanKey, T, FlatHuffmanKey>& values;
    const HuffmanKey* position;
  };
  Iterator begin() const { return Iterator(values, keys.begin()); }
  Iterator end() const { return Iterator(values, keys.end()); }

 private:
  // The entries in this Huffman table, prepared for lookup.
  js::HashMap<FlatHuffmanKey, T, FlatHuffmanKey> values;

  // The entries in this Huffman Table, sorted in the order of insertion.
  Vector<HuffmanKey> keys;

  friend class HuffmanPreludeReader;
};

// An implementation of Huffman Tables as a vector designed to allow
// constant-time lookups at the expense of high space complexity.
//
// # Time complexity
//
// Lookups take constant time, which essentially consists in two
// simple vector lookups.
//
// # Space complexity
//
// After initialization, a `SingleLookupHuffmanTable`
// requires O(2 ^ max bit length in the table) space:
//
// - A vector `values` containing one entry per symbol.
// - A vector `saturated` containing exactly 2 ^ (max bit length in the
//   table) entries, which we use to map any combination of `largestBitLength`
//   bits onto the only `HuffmanEntry` that may be reached by a prefix
//   of these `largestBitLength` bits. See below for more details.
//
// # Algorithm
//
// Consider the following Huffman table
//
// Symbol | Binary Code  | Int value of Code | Bit Length
// ------ | ------------ | ----------------- | ----------
// A      | 11000        | 24                | 5
// B      | 11001        | 25                | 5
// C      | 1101         | 13                | 4
// D      | 100          | 4                 | 3
// E      | 101          | 5                 | 3
// F      | 111          | 7                 | 3
// G      | 00           | 0                 | 2
// H      | 01           | 1                 | 2
//
// By definition of a Huffman Table, the Binary Codes represent
// paths in a Huffman Tree. Consequently, padding these codes
// to the end would not change the result.
//
// Symbol | Binary Code  | Int value of Code | Bit Length
// ------ | ------------ | ----------------- | ----------
// A      | 11000        | 24                | 5
// B      | 11001        | 25                | 5
// C      | 1101?        | [26...27]         | 4
// D      | 100??        | [16...19]         | 3
// E      | 101??        | [20..23]          | 3
// F      | 111??        | [28..31]          | 3
// G      | 00???        | [0...7]           | 2
// H      | 01???        | [8...15]          | 2
//
// Row "Int value of Code" now contains all possible values
// that may be expressed in 5 bits. By using these values
// as array indices, we may therefore represent the
// Huffman table as an array:
//
// Index     |   Symbol   |   Bit Length
// --------- | ---------- | -------------
// [0...7]   |  G         | 2
// [8...15]  |  H         | 2
// [16...19] |  D         | 3
// [20...23] |  E         | 3
// 24        |  A         | 5
// 25        |  B         | 5
// [26...27] |  C         | 4
// [28...31] |  F         | 3
//
// By using the next 5 bits in the bit buffer, we may, in
// a single lookup, determine the symbol and the bit length.
//
// In the current implementation, to save some space, we have
// two distinct arrays, one (`values`) with a single instance of each
// symbols bit length, and one (`saturated`) with indices into that
// array.
template <typename T>
class SingleLookupHuffmanTable {
 public:
  // An index into table `values`.
  // We use `uint8_t` instead of `size_t` to limit the space
  // used by the table.
  using InternalIndex = uint8_t;

  // The largest bit length that may be represented by this table.
  static const uint8_t MAX_BIT_LENGTH = sizeof(InternalIndex) * 8;

  explicit SingleLookupHuffmanTable(JSContext* cx)
      : values(cx), saturated(cx), largestBitLength(-1) {}
  SingleLookupHuffmanTable(SingleLookupHuffmanTable&& other) = default;

  // Initialize a Huffman table containing a single value.
  JS::Result<Ok> initWithSingleValue(JSContext* cx, T&& value);

  // Initialize a Huffman table containing `numberOfSymbols`.
  // Symbols must be added with `addSymbol`.
  // If you initialize with `initStart`, you MUST call `initComplete()`
  // at the end of initialization.
  JS::Result<Ok> initStart(JSContext* cx, size_t numberOfSymbols,
                           uint8_t maxBitLength);

  JS::Result<Ok> initComplete();

  // Add a `(bit, bits_length) => value` mapping.
  JS::Result<Ok> addSymbol(uint32_t bits, uint8_t bits_length, T&& value);

  SingleLookupHuffmanTable() = delete;
  SingleLookupHuffmanTable(SingleLookupHuffmanTable&) = delete;

  // Lookup a value in the table.
  //
  // The return of this method contains:
  //
  // - the resulting value (`nullptr` if the value is not in the table);
  // - the number of bits in the entry associated to this value.
  //
  // Note that entries inside a single table are typically associated to
  // distinct bit lengths. The caller is responsible for checking
  // the result of this method and advancing the bitstream by
  // `result.key.bitLength` bits.
  HuffmanEntry<const T*> lookup(HuffmanLookup key) const;

  // The number of values in the table.
  size_t length() const { return values.length(); }

  // Iterating in the order of insertion.
  struct Iterator {
    explicit Iterator(const HuffmanEntry<T>* position);
    void operator++();
    const T* operator*() const;
    bool operator==(const Iterator& other) const;
    bool operator!=(const Iterator& other) const;

   private:
    const HuffmanEntry<T>* position;
  };
  Iterator begin() const { return Iterator(values.begin()); }
  Iterator end() const { return Iterator(values.end()); }

 private:
  // The entries in this Huffman Table, sorted in the order of insertion.
  //
  // Invariant (once `init*` has been called):
  // - Length is the number of values inserted in the table.
  // - for all i, `values[i].bitLength <= largestBitLength`.
  Vector<HuffmanEntry<T>> values;

  // The entries in this Huffman table, prepared for lookup.
  //
  // Invariant (once `init*` has been called):
  // - Length is `1 << largestBitLength`.
  // - for all i, `saturated[i] < values.length()`
  Vector<InternalIndex> saturated;

  // The maximal bitlength of a value in this table.
  //
  // Invariant (once `init*` has been called):
  // - `largestBitLength <= MAX_CODE_BIT_LENGTH`
  uint8_t largestBitLength;

  friend class HuffmanPreludeReader;
};

/// A table designed to support fast lookup in large sets of data.
/// In most cases, lookup will be slower than a `SingleLookupHuffmanTable`
/// but, particularly in heavily unbalanced trees, the table will
/// take ~2^prefix_len fewer internal entries than a `SingleLookupHuffmanTable`.
///
/// Typically, use this table whenever codes range between 10 and 20 bits.
///
/// # Time complexity
///
/// A lookup in `MultiLookupHuffmanTable` will also take constant time:
///
/// - a constant-time lookup to determine into which sub-table to perform the
/// lookup;
/// - a constant-time lookup into the sub-table;
/// - a constant-time lookup into the array of values.
///
///
/// # Space complexity
///
/// TBD. Highly dependent on the shape of the Huffman Tree.
///
///
/// # Algorithm
///
/// Consider the following Huffman table
///
/// Symbol | Binary Code  | Bit Length
/// ------ | ------------ | ----------
/// A      | 11000        | 5
/// B      | 11001        | 5
/// C      | 1101         | 4
/// D      | 100          | 3
/// E      | 101          | 3
/// F      | 111          | 3
/// G      | 00           | 2
/// H      | 01           | 2
///
/// With a prefix length of 3, we will precompute all possible 3-bit prefixes
/// and split the table across such prefixes. Note that we have picked a
/// length of 3 bits arbitrarily – in this case it is larger than the
/// bit length of some symbols.
///
/// Prefix | Int Value of Prefix | Symbols   | Max bit length
/// ------ | ------------------- | --------- | --------------
/// 000    | 0                   | G         | 0
/// 001    | 1                   | G         | 0
/// 010    | 2                   | H         | 0
/// 011    | 3                   | H         | 0
/// 100    | 4                   | D         | 0
/// 101    | 5                   | E         | 0
/// 110    | 6                   | A, B, C   | 2
/// 111    | 7                   | F         | 0
///
/// For each prefix, we build the table containing the Symbols,
/// stripping prefix from the Binary Code.
///
/// - Prefix 000
///
/// Symbol | Binary Code | Bit Length | Total Bit Length
/// ------ | ----------- | ---------- | ----------------
/// G      | (none)      | 0          | 2
///
/// - Prefix 001
///
/// Symbol | Binary Code | Bit Length | Total Bit Length
/// ------ | ----------- | ---------- | ----------------
/// G      | (none)      | 0          | 2
///
/// - Prefix 010
///
/// Symbol | Binary Code | Bit Length | Total Bit Length
/// ------ | ----------- | ---------- | --------------
/// H      | (none)      | 0          | 2
///
/// - Prefix 11
///
/// Symbol | Binary Code | Bit Length | Total Bit Length
/// ------ | ----------- | ---------- | ----------------
/// H      | (none)      | 0          | 2
///
/// - Prefix 100
///
/// Symbol | Binary Code | Bit Length | Total Bit Length
/// ------ | ----------- | ---------- | ----------------
/// D      | (none)      | 0          | 3
///
/// - Prefix 101
///
/// Symbol | Binary Code | Bit Length | Total Bit Length
/// ------ | ----------- | ---------- | ----------------
/// E      | (none)      | 0          | 3
///
/// - Prefix 110
///
/// Symbol | Binary Code | Bit Length | Total Bit Length
/// ------ | ----------- | ---------- | ----------------
/// A      | 00          | 2          | 5
/// B      | 01          | 2          | 5
/// C      | 1           | 1          | 4
///
/// - Prefix 111
///
/// Symbol | Binary Code | Bit Length | Total Bit Length
/// ------ | ----------- | ---------- | ----------------
/// F      | (none)      | 0          | 3
///
/// With this transformation, we have represented one table
/// with an initial max bit length of 5 as:
///
/// - 1 table with a max bit length of 2;
/// - 7 tables with a max bit length of 0.
///
/// Consequently, instead of storing 2^5 = 32 internal references,
/// as we would have done with a SingleLookupHuffmanTable, we only
/// need to store:
///
/// - 7 subtables with 1 reference each;
/// - 1 subtable with 2^2 = 4 references.
template <typename T, typename Subtable, uint8_t PrefixBitLength>
class MultiLookupHuffmanTable {
 public:
  // The largest bit length that may be represented by this table.
  static const uint8_t MAX_BIT_LENGTH =
      PrefixBitLength + Subtable::MAX_BIT_LENGTH;

  explicit MultiLookupHuffmanTable(JSContext* cx)
      : cx_(cx), values(cx), subTables(cx), largestBitLength(-1) {}
  MultiLookupHuffmanTable(MultiLookupHuffmanTable&& other) = default;

  // Initialize a Huffman table containing `numberOfSymbols`.
  // Symbols must be added with `addSymbol`.
  // If you initialize with `initStart`, you MUST call `initComplete()`
  // at the end of initialization.
  JS::Result<Ok> initStart(JSContext* cx, size_t numberOfSymbols,
                           uint8_t largestBitLength);

  JS::Result<Ok> initComplete();

  // Add a `(bit, bits_length) => value` mapping.
  JS::Result<Ok> addSymbol(uint32_t bits, uint8_t bits_length, T&& value);

  MultiLookupHuffmanTable() = delete;
  MultiLookupHuffmanTable(MultiLookupHuffmanTable&) = delete;

  // Lookup a value in the table.
  //
  // The return of this method contains:
  //
  // - the resulting value (`nullptr` if the value is not in the table);
  // - the number of bits in the entry associated to this value.
  //
  // Note that entries inside a single table are typically associated to
  // distinct bit lengths. The caller is responsible for checking
  // the result of this method and advancing the bitstream by
  // `result.key.bitLength` bits.
  HuffmanEntry<const T*> lookup(HuffmanLookup key) const;

  // The number of values in the table.
  size_t length() const { return values.length(); }

  // Iterating in the order of insertion.
  struct Iterator {
    explicit Iterator(const HuffmanEntry<T>* position);
    void operator++();
    const T* operator*() const;
    bool operator==(const Iterator& other) const;
    bool operator!=(const Iterator& other) const;

   private:
    const HuffmanEntry<T>* position;
  };
  Iterator begin() const { return Iterator(values.begin()); }
  Iterator end() const { return Iterator(values.end()); }

 public:
  // An index into table `values`.
  // We use `uint8_t` instead of `size_t` to limit the space
  // used by the table.
  using InternalIndex = uint8_t;

 private:
  JSContext* cx_;

  // The entries in this Huffman Table, sorted in the order of insertion.
  //
  // Invariant (once `init*` has been called):
  // - Length is the number of values inserted in the table.
  // - for all i, `values[i].bitLength <= largestBitLength`.
  //
  // FIXME: In a ThreeLookupsHuffmanTable, we currently store each value
  // three times. We could at least get down to twice.
  Vector<HuffmanEntry<T>> values;

  // A mapping from 0..2^prefixBitLen such that index `i`
  // maps to a subtable that holds all values associated
  // with a key that starts with `HuffmanKey(i, prefixBitLen)`.
  //
  // Note that, to allow the use of smaller tables, keys
  // inside the subtables have been stripped
  // from the prefix `HuffmanKey(i, prefixBitLen)`.
  Vector<Subtable> subTables;

  // The maximal bitlength of a value in this table.
  //
  // Invariant (once `init*` has been called):
  // - `largestBitLength <= MAX_CODE_BIT_LENGTH`
  uint8_t largestBitLength;

  friend class HuffmanPreludeReader;
};

/// A Huffman table suitable for max bit lengths in [8, 14]
template <typename T>
using TwoLookupsHuffmanTable = MultiLookupHuffmanTable<
    T, SingleLookupHuffmanTable</* external index */ size_t>, 6>;

/// A Huffman table suitable for max bit lengths in [15, 20]
template <typename T>
using ThreeLookupsHuffmanTable = MultiLookupHuffmanTable<
    T, TwoLookupsHuffmanTable</* external index */ size_t>, 6>;

// An empty Huffman table. Attempting to get a value from this table is a syntax
// error. This is the default value for `HuffmanTableValue` and represents all
// states that may not be reached.
//
// Part of variants `HuffmanTableValue`, `HuffmanTableListLength` and
// `GenericHuffmanTable::implementation`.
struct HuffmanTableUnreachable {};

// Generic implementation of Huffman tables.
//
//
template <typename T>
struct GenericHuffmanTable {
  explicit GenericHuffmanTable(JSContext* cx);
  explicit GenericHuffmanTable() = delete;

  // Initialize a Huffman table containing a single value.
  JS::Result<Ok> initWithSingleValue(JSContext* cx, T&& value);

  // Initialize a Huffman table containing `numberOfSymbols`.
  // Symbols must be added with `addSymbol`.
  // If you initialize with `initStart`, you MUST call `initComplete()`
  // at the end of initialization.
  JS::Result<Ok> initStart(JSContext* cx, size_t numberOfSymbols,
                           uint8_t maxBitLength);

  // Add a `(bit, bits_length) => value` mapping.
  JS::Result<Ok> addSymbol(uint32_t bits, uint8_t bits_length, T&& value);

  JS::Result<Ok> initComplete();

  // The number of values in the table.
  size_t length() const;

  struct Iterator {
    explicit Iterator(typename SingleLookupHuffmanTable<T>::Iterator&&);
    explicit Iterator(typename TwoLookupsHuffmanTable<T>::Iterator&&);
    explicit Iterator(typename ThreeLookupsHuffmanTable<T>::Iterator&&);
    Iterator(Iterator&&) = default;
    Iterator(const Iterator&) = default;
    void operator++();
    const T* operator*() const;
    bool operator==(const Iterator& other) const;
    bool operator!=(const Iterator& other) const;

   private:
    mozilla::Variant<typename SingleLookupHuffmanTable<T>::Iterator,
                     typename TwoLookupsHuffmanTable<T>::Iterator,
                     typename ThreeLookupsHuffmanTable<T>::Iterator>
        implementation;
  };

  // Iterating in the order of insertion.
  Iterator begin() const;
  Iterator end() const;

  // Lookup a value in the table.
  //
  // The return of this method contains:
  //
  // - the resulting value (`nullptr` if the value is not in the table);
  // - the number of bits in the entry associated to this value.
  //
  // Note that entries inside a single table are typically associated to
  // distinct bit lengths. The caller is responsible for checking
  // the result of this method and advancing the bitstream by
  // `result.key.bitLength` bits.
  HuffmanEntry<const T*> lookup(HuffmanLookup key) const;

 private:
  mozilla::Variant<SingleLookupHuffmanTable<T>, TwoLookupsHuffmanTable<T>,
                   ThreeLookupsHuffmanTable<T>, HuffmanTableUnreachable>
      implementation;
};

// While reading the Huffman prelude, whenever we first encounter a
// `HuffmanTableUnreachable`, we replace it with a `HuffmanTableInitializing`
// to mark that we should not attempt to read/initialize it again.
//
// Attempting to get a value from this table is an internal error.
//
// Part of variants `HuffmanTableValue` and `HuffmanTableListLength`.
struct HuffmanTableInitializing {};

// These classes are all parts of variant `HuffmanTableValue`.

struct HuffmanTableExplicitSymbolsF64 : GenericHuffmanTable<double> {
  using Contents = double;
  explicit HuffmanTableExplicitSymbolsF64(JSContext* cx)
      : GenericHuffmanTable(cx) {}
};

struct HuffmanTableExplicitSymbolsU32 : GenericHuffmanTable<uint32_t> {
  using Contents = uint32_t;
  explicit HuffmanTableExplicitSymbolsU32(JSContext* cx)
      : GenericHuffmanTable(cx) {}
};

struct HuffmanTableIndexedSymbolsSum : GenericHuffmanTable<BinASTKind> {
  using Contents = BinASTKind;
  explicit HuffmanTableIndexedSymbolsSum(JSContext* cx)
      : GenericHuffmanTable(cx) {}
};

struct HuffmanTableIndexedSymbolsBool : NaiveHuffmanTable<bool, 2> {
  using Contents = bool;
  explicit HuffmanTableIndexedSymbolsBool(JSContext* cx)
      : NaiveHuffmanTable(cx) {}
};

// A Huffman table that may only ever contain two values:
// `BinASTKind::_Null` and another `BinASTKind`.
struct HuffmanTableIndexedSymbolsMaybeInterface
    : NaiveHuffmanTable<BinASTKind, 2> {
  using Contents = BinASTKind;
  explicit HuffmanTableIndexedSymbolsMaybeInterface(JSContext* cx)
      : NaiveHuffmanTable(cx) {}

  // `true` if this table only contains values for `null`.
  bool isAlwaysNull() const {
    MOZ_ASSERT(length() > 0);

    // By definition, we have either 1 or 2 values.
    // By definition, if we have 2 values, one of them is not null.
    if (length() != 1) {
      return false;
    }
    // Otherwise, check the single value.
    return begin()->value == BinASTKind::_Null;
  }
};

struct HuffmanTableIndexedSymbolsStringEnum
    : GenericHuffmanTable<BinASTVariant> {
  using Contents = BinASTVariant;
  explicit HuffmanTableIndexedSymbolsStringEnum(JSContext* cx)
      : GenericHuffmanTable(cx) {}
};

struct HuffmanTableIndexedSymbolsLiteralString : GenericHuffmanTable<JSAtom*> {
  using Contents = JSAtom*;
  explicit HuffmanTableIndexedSymbolsLiteralString(JSContext* cx)
      : GenericHuffmanTable(cx) {}
};

struct HuffmanTableIndexedSymbolsOptionalLiteralString
    : GenericHuffmanTable<JSAtom*> {
  using Contents = JSAtom*;
  explicit HuffmanTableIndexedSymbolsOptionalLiteralString(JSContext* cx)
      : GenericHuffmanTable(cx) {}
};

// A single Huffman table, used for values.
using HuffmanTableValue = mozilla::Variant<
    HuffmanTableUnreachable,  // Default value.
    HuffmanTableInitializing, HuffmanTableExplicitSymbolsF64,
    HuffmanTableExplicitSymbolsU32, HuffmanTableIndexedSymbolsSum,
    HuffmanTableIndexedSymbolsMaybeInterface, HuffmanTableIndexedSymbolsBool,
    HuffmanTableIndexedSymbolsStringEnum,
    HuffmanTableIndexedSymbolsLiteralString,
    HuffmanTableIndexedSymbolsOptionalLiteralString>;

struct HuffmanTableExplicitSymbolsListLength : GenericHuffmanTable<uint32_t> {
  using Contents = uint32_t;
  explicit HuffmanTableExplicitSymbolsListLength(JSContext* cx)
      : GenericHuffmanTable(cx) {}
};

// A single Huffman table, specialized for list lengths.
using HuffmanTableListLength =
    mozilla::Variant<HuffmanTableUnreachable,  // Default value.
                     HuffmanTableInitializing,
                     HuffmanTableExplicitSymbolsListLength>;

// A Huffman dictionary for the current file.
//
// A Huffman dictionary consists in a (contiguous) set of Huffman tables
// to predict field values and a second (contiguous) set of Huffman tables
// to predict list lengths.
class HuffmanDictionary {
 public:
  explicit HuffmanDictionary(JSContext* cx);

  HuffmanTableValue& tableForField(NormalizedInterfaceAndField index);
  HuffmanTableListLength& tableForListLength(BinASTList list);

 private:
  // Huffman tables for `(Interface, Field)` pairs, used to decode the value of
  // `Interface::Field`. Some tables may be `HuffmanTableUnreacheable`
  // if they represent fields of interfaces that actually do not show up
  // in the file.
  //
  // The mapping from `(Interface, Field) -> index` is extracted statically from
  // the webidl specs.
  mozilla::Array<HuffmanTableValue, BINAST_INTERFACE_AND_FIELD_LIMIT> fields;

  // Huffman tables for list lengths. Some tables may be
  // `HuffmanTableUnreacheable` if they represent lists that actually do not
  // show up in the file.
  //
  // The mapping from `List -> index` is extracted statically from the webidl
  // specs.
  mozilla::Array<HuffmanTableListLength, BINAST_NUMBER_OF_LIST_TYPES>
      listLengths;
};

/**
 * A token reader implementing the "context" serialization format for BinAST.
 *
 * This serialization format, which is also supported by the reference
 * implementation of the BinAST compression suite, is designed to be
 * space- and time-efficient.
 *
 * As other token readers for the BinAST:
 *
 * - the reader does not support error recovery;
 * - the reader does not support lookahead or pushback.
 */
class MOZ_STACK_CLASS BinASTTokenReaderContext : public BinASTTokenReaderBase {
  using Base = BinASTTokenReaderBase;

 public:
  class AutoList;
  class AutoTaggedTuple;

  using CharSlice = BinaryASTSupport::CharSlice;
  using RootContext = BinASTTokenReaderBase::RootContext;
  using ListContext = BinASTTokenReaderBase::ListContext;
  using FieldContext = BinASTTokenReaderBase::FieldContext;
  using FieldOrRootContext = BinASTTokenReaderBase::FieldOrRootContext;
  using FieldOrListContext = BinASTTokenReaderBase::FieldOrListContext;
  using Chars = CharSlice;

 public:
  /**
   * Construct a token reader.
   *
   * Does NOT copy the buffer.
   */
  BinASTTokenReaderContext(JSContext* cx, ErrorReporter* er,
                           const uint8_t* start, const size_t length);

  /**
   * Construct a token reader.
   *
   * Does NOT copy the buffer.
   */
  BinASTTokenReaderContext(JSContext* cx, ErrorReporter* er,
                           const Vector<uint8_t>& chars);

  ~BinASTTokenReaderContext();

  // {readByte, readBuf, readVarU32} are implemented both for uncompressed
  // stream and brotli-compressed stream.
  //
  // Uncompressed variant is for reading the magic header, and compressed
  // variant is for reading the remaining part.
  //
  // Once compressed variant is called, the underlying uncompressed stream is
  // buffered and uncompressed variant cannot be called.
  enum class Compression { No, Yes };

  // Determine what to do if we reach the end of the file.
  enum class EndOfFilePolicy {
    // End of file was not expected, raise an error.
    RaiseError,
    // End of file is ok, read as many bytes as possible.
    BestEffort
  };

 protected:
  // A buffer of bits used to lookup data from the Huffman tables.
  // It may contain up to 64 bits.
  //
  // To interact with the buffer, see methods
  // - advanceBitBuffer()
  // - getHuffmanLookup()
  struct BitBuffer {
    BitBuffer();

    // Return the HuffmanLookup for the next lookup in a Huffman table.
    // After calling this method, do not forget to call `advanceBitBuffer`.
    //
    // If `result.bitLength == 0`, you have reached the end of the stream.
    template <Compression Compression>
    MOZ_MUST_USE JS::Result<HuffmanLookup> getHuffmanLookup(
        BinASTTokenReaderContext& owner);

    // Advance the bit buffer by `bitLength` bits.
    template <Compression Compression>
    void advanceBitBuffer(const uint8_t bitLength);

   private:
    // The contents of the buffer.
    //
    // - Bytes are added in the same order as the bytestream.
    // - Individual bits within bytes are mirrored.
    //
    // In other words, if the byte stream starts with
    // `0b_HGFE_DCBA`, `0b_PONM_LKJI`, `0b_0000_0000`,
    // .... `0b_0000_0000`, `bits` will hold
    // `0b_0000_...0000__ABCD_EFGH__IJKL_MNOP`.
    //
    // Note: By opposition to `HuffmanKey` or `HuffmanLookup`,
    // the highest bits are NOT guaranteed to be `0`.
    uint64_t bits;

    // The number of elements in `bits`.
    //
    // Until we start lookup up into Huffman tables, `bitLength == 0`.
    // Once we do, we refill the buffer before any lookup, i.e.
    // `MAX_PREFIX_BIT_LENGTH = 32 <= bitLength <= BIT_BUFFER_SIZE = 64`
    // until we reach the last few bytes of the stream,
    // in which case `length` decreases monotonically to 0.
    //
    // If `bitLength < BIT_BUFFER_SIZE = 64`, some of the highest
    // bits of `bits` are unused.
    uint8_t bitLength;
  } bitBuffer;

  // Returns true if the brotli stream finished.
  bool isEOF() const;

  /**
   * Read a single byte.
   */
  template <Compression compression>
  MOZ_MUST_USE JS::Result<uint8_t> readByte();

  /**
   * Read several bytes.
   *
   * If the tokenizer has previously been poisoned, return an error.
   * If the end of file is reached, in the case of
   * EndOfFilePolicy::RaiseError, raise an error. Otherwise, update
   * `len` to indicate how many bytes have actually been read.
   */
  template <Compression compression, EndOfFilePolicy policy>
  MOZ_MUST_USE JS::Result<Ok> readBuf(uint8_t* bytes, uint32_t& len);

  enum class FillResult { EndOfStream, Filled };

 public:
  /**
   * Read the header of the file.
   */
  MOZ_MUST_USE JS::Result<Ok> readHeader();

  /**
   * Read the string dictionary from the header of the file.
   */
  MOZ_MUST_USE JS::Result<Ok> readStringPrelude();

  /**
   * Read the huffman dictionary from the header of the file.
   */
  MOZ_MUST_USE JS::Result<Ok> readHuffmanPrelude();

  // --- Primitive values.
  //
  // Note that the underlying format allows for a `null` value for primitive
  // values.
  //
  // Reading will return an error either in case of I/O error or in case of
  // a format problem. Reading if an exception in pending is an error and
  // will cause assertion failures. Do NOT attempt to read once an exception
  // has been cleared: the token reader does NOT support recovery, by design.

  /**
   * Read a single `true | false` value.
   */
  MOZ_MUST_USE JS::Result<bool> readBool(const FieldContext&);

  /**
   * Read a single `number` value.
   */
  MOZ_MUST_USE JS::Result<double> readDouble(const FieldContext&);

  /**
   * Read a single `string | null` value.
   *
   * Fails if that string is not valid UTF-8.
   */
  MOZ_MUST_USE JS::Result<JSAtom*> readMaybeAtom(const FieldContext&);
  MOZ_MUST_USE JS::Result<JSAtom*> readAtom(const FieldContext&);

  /**
   * Read a single IdentifierName value.
   */
  MOZ_MUST_USE JS::Result<JSAtom*> readMaybeIdentifierName(const FieldContext&);
  MOZ_MUST_USE JS::Result<JSAtom*> readIdentifierName(const FieldContext&);

  /**
   * Read a single PropertyKey value.
   */
  MOZ_MUST_USE JS::Result<JSAtom*> readPropertyKey(const FieldContext&);

  /**
   * Read a single `string | null` value.
   *
   * MAY check if that string is not valid UTF-8.
   */
  MOZ_MUST_USE JS::Result<Ok> readChars(Chars&, const FieldContext&);

  /**
   * Read a single `BinASTVariant | null` value.
   */
  MOZ_MUST_USE JS::Result<BinASTVariant> readVariant(const ListContext&);
  MOZ_MUST_USE JS::Result<BinASTVariant> readVariant(const FieldContext&);

  /**
   * Read over a single `[Skippable]` subtree value.
   *
   * This does *not* attempt to parse the subtree itself. Rather, the
   * returned `SkippableSubTree` contains the necessary information
   * to parse/tokenize the subtree at a later stage
   */
  MOZ_MUST_USE JS::Result<SkippableSubTree> readSkippableSubTree(
      const FieldContext&);

  // --- Composite values.
  //
  // The underlying format does NOT allows for a `null` composite value.
  //
  // Reading will return an error either in case of I/O error or in case of
  // a format problem. Reading from a poisoned tokenizer is an error and
  // will cause assertion failures.

  /**
   * Start reading a list.
   *
   * @param length (OUT) The number of elements in the list.
   * @param guard (OUT) A guard, ensuring that we read the list correctly.
   *
   * The `guard` is dedicated to ensuring that reading the list has consumed
   * exactly all the bytes from that list. The `guard` MUST therefore be
   * destroyed at the point where the caller has reached the end of the list.
   * If the caller has consumed too few/too many bytes, this will be reported
   * in the call go `guard.done()`.
   */
  MOZ_MUST_USE JS::Result<Ok> enterList(uint32_t& length, const ListContext&);

  /**
   * Start reading a tagged tuple.
   *
   * @param tag (OUT) The tag of the tuple.
   * @param fields Ignored, provided for API compatibility.
   * @param guard (OUT) A guard, ensuring that we read the tagged tuple
   * correctly.
   *
   * The `guard` is dedicated to ensuring that reading the list has consumed
   * exactly all the bytes from that tuple. The `guard` MUST therefore be
   * destroyed at the point where the caller has reached the end of the tuple.
   * If the caller has consumed too few/too many bytes, this will be reported
   * in the call go `guard.done()`.
   *
   * @return out If the header of the tuple is invalid.
   */
  MOZ_MUST_USE JS::Result<Ok> enterInterface(BinASTKind& tag) {
    // We're entering a monomorphic interface, so the tag is encoded as 0 bits.
    MOZ_ASSERT(tag != BinASTKind::_Uninitialized);
    return Ok();
  }
  MOZ_MUST_USE JS::Result<Ok> enterInterface(BinASTKind& tag,
                                             const FieldOrRootContext&) {
    return enterInterface(tag);
  }
  MOZ_MUST_USE JS::Result<Ok> enterInterface(BinASTKind& tag,
                                             const FieldOrListContext&) {
    return enterInterface(tag);
  }
  MOZ_MUST_USE JS::Result<Ok> enterInterface(BinASTKind& tag,
                                             const RootContext&) {
    return enterInterface(tag);
  }
  MOZ_MUST_USE JS::Result<Ok> enterInterface(BinASTKind& tag,
                                             const ListContext&) {
    return enterInterface(tag);
  }
  MOZ_MUST_USE JS::Result<Ok> enterInterface(BinASTKind& tag,
                                             const FieldContext&) {
    return enterInterface(tag);
  }
  MOZ_MUST_USE JS::Result<Ok> enterOptionalInterface(
      BinASTKind& tag, const FieldOrRootContext& context) {
    return enterSum(tag, context);
  }
  MOZ_MUST_USE JS::Result<Ok> enterOptionalInterface(
      BinASTKind& tag, const FieldOrListContext& context) {
    return enterSum(tag, context);
  }
  MOZ_MUST_USE JS::Result<Ok> enterOptionalInterface(
      BinASTKind& tag, const RootContext& context) {
    return enterSum(tag, context);
  }
  MOZ_MUST_USE JS::Result<Ok> enterOptionalInterface(
      BinASTKind& tag, const ListContext& context) {
    return enterSum(tag, context);
  }
  MOZ_MUST_USE JS::Result<Ok> enterOptionalInterface(
      BinASTKind& tag, const FieldContext& context) {
    return enterSum(tag, context);
  }
  MOZ_MUST_USE JS::Result<Ok> enterSum(BinASTKind& tag,
                                       const FieldOrRootContext&);
  MOZ_MUST_USE JS::Result<Ok> enterSum(BinASTKind& tag,
                                       const FieldOrListContext&);
  MOZ_MUST_USE JS::Result<Ok> enterSum(BinASTKind& tag, const RootContext&);
  MOZ_MUST_USE JS::Result<Ok> enterSum(BinASTKind& tag, const ListContext&);
  MOZ_MUST_USE JS::Result<Ok> enterSum(BinASTKind& tag, const FieldContext&);

  /**
   * Read a single unsigned long.
   */
  MOZ_MUST_USE JS::Result<uint32_t> readUnsignedLong(const FieldContext&);
  MOZ_MUST_USE JS::Result<uint32_t> readUnpackedLong();

 private:
  MOZ_MUST_USE JS::Result<BinASTKind> readTagFromTable(
      const BinASTInterfaceAndField&);

  template <typename Table>
  MOZ_MUST_USE JS::Result<typename Table::Contents> readFieldFromTable(
      const BinASTInterfaceAndField&);

  /**
   * Report an "invalid value error".
   */
  MOZ_MUST_USE ErrorResult<JS::Error&> raiseInvalidValue();

  /**
   * Report a "value not in prelude".
   */
  MOZ_MUST_USE ErrorResult<JS::Error&> raiseNotInPrelude();

 private:
  /**
   * Read a single uint32_t.
   */
  template <Compression compression>
  MOZ_MUST_USE JS::Result<uint32_t> readVarU32();

  template <EndOfFilePolicy policy>
  MOZ_MUST_USE JS::Result<Ok> handleEndOfStream();

  template <EndOfFilePolicy policy>
  MOZ_MUST_USE JS::Result<Ok> readBufCompressedAux(uint8_t* bytes,
                                                   uint32_t& len);

 private:
  // A mapping string index => BinASTVariant as extracted from the [STRINGS]
  // section of the file. Populated lazily.
  js::HashMap<FlatHuffmanKey, BinASTVariant, DefaultHasher<uint32_t>,
              SystemAllocPolicy>
      variantsTable_;

  enum class MetadataOwnership { Owned, Unowned };
  MetadataOwnership metadataOwned_ = MetadataOwnership::Owned;
  BinASTSourceMetadata* metadata_;

  class HuffmanDictionary dictionary;

  const uint8_t* posBeforeTree_;

 public:
  BinASTTokenReaderContext(const BinASTTokenReaderContext&) = delete;
  BinASTTokenReaderContext(BinASTTokenReaderContext&&) = delete;
  BinASTTokenReaderContext& operator=(BinASTTokenReaderContext&) = delete;

 public:
  void traceMetadata(JSTracer* trc);
  BinASTSourceMetadata* takeMetadata();
  MOZ_MUST_USE JS::Result<Ok> initFromScriptSource(ScriptSource* scriptSource);

 protected:
  friend class HuffmanPreludeReader;

 public:
  // The following classes are used whenever we encounter a tuple/tagged
  // tuple/list to make sure that:
  //
  // - if the construct "knows" its byte length, we have exactly consumed all
  //   the bytes (otherwise, this means that the file is corrupted, perhaps on
  //   purpose, so we need to reject the stream);
  // - if the construct has a footer, once we are done reading it, we have
  //   reached the footer (this is to aid with debugging).
  //
  // In either case, the caller MUST call method `done()` of the guard once
  // it is done reading the tuple/tagged tuple/list, to report any pending
  // error.

  // Base class used by other Auto* classes.
  class MOZ_STACK_CLASS AutoBase {
   protected:
    explicit AutoBase(BinASTTokenReaderContext& reader)
#ifdef DEBUG
        : initialized_(false),
          reader_(reader)
#endif
    {
    }
    ~AutoBase() {
      // By now, the `AutoBase` must have been deinitialized by calling
      // `done()`. The only case in which we can accept not calling `done()` is
      // if we have bailed out because of an error.
      MOZ_ASSERT_IF(initialized_, reader_.hasRaisedError());
    }

    friend BinASTTokenReaderContext;

   public:
    inline void init() {
#ifdef DEBUG
      initialized_ = true;
#endif
    }

    inline MOZ_MUST_USE JS::Result<Ok> done() {
#ifdef DEBUG
      initialized_ = false;
#endif
      return Ok();
    }

   protected:
#ifdef DEBUG
    bool initialized_;
    BinASTTokenReaderContext& reader_;
#endif
  };

  // Guard class used to ensure that `enterList` is used properly.
  class MOZ_STACK_CLASS AutoList : public AutoBase {
   public:
    explicit AutoList(BinASTTokenReaderContext& reader) : AutoBase(reader) {}
  };

  // Guard class used to ensure that `enterTaggedTuple` is used properly.
  class MOZ_STACK_CLASS AutoTaggedTuple : public AutoBase {
   public:
    explicit AutoTaggedTuple(BinASTTokenReaderContext& reader)
        : AutoBase(reader) {}
  };

  // Compare a `Chars` and a string literal (ONLY a string literal).
  template <size_t N>
  static bool equals(const Chars& left, const char (&right)[N]) {
    MOZ_ASSERT(N > 0);
    MOZ_ASSERT(right[N - 1] == 0);
    if (left.byteLen_ + 1 /* implicit NUL */ != N) {
      return false;
    }

    if (!std::equal(left.start_, left.start_ + left.byteLen_, right)) {
      return false;
    }

    return true;
  }
};

}  // namespace frontend
}  // namespace js

#endif  // frontend_BinASTTokenReaderContext_h

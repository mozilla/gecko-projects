/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef frontend_BinASTTokenReaderContext_h
#define frontend_BinASTTokenReaderContext_h

#include "mozilla/Assertions.h"  // MOZ_ASSERT
#include "mozilla/Attributes.h"  // MOZ_MUST_USE, MOZ_STACK_CLASS

#include "mozilla/Maybe.h"  // mozilla::Maybe

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
  HuffmanLookup(uint32_t bits, uint8_t bitLength)
      // We zero out the highest `32 - bitLength` bits.
      : bits(bits & (uint32_t(0xFFFFFFFF) >> (32 - bitLength))),
        bitLength(bitLength) {
    MOZ_ASSERT(bitLength <= 32);
    MOZ_ASSERT(bits >> bitLength == 0);
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

  // The buffer holding the bits. At this stage, bits are stored
  // in the same order as `HuffmanKey`. See the implementation of
  // `BitBuffer` methods for more details about how this order
  // is implemented.
  //
  // If `bitLength < 32`, the unused highest bits are guaranteed
  // to be 0.
  uint32_t bits;

  // The actual length of buffer `bits`.
  //
  // MUST be within `[0, 32]`.
  //
  // If `bitLength < 32`, it means that some of the highest bits are unused.
  uint8_t bitLength;
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
  HuffmanKey(const uint32_t bits, const uint8_t bitLength)
      : bits(bits), bitLength(bitLength) {
    MOZ_ASSERT(bitLength <= 32);
    MOZ_ASSERT(bits >> bitLength == 0);
  }

  // The buffer holding the bits.
  //
  // For a Huffman code of `0100`
  // - `bits = 0b0000_0000__0000_0000__0000_0000__0000_0100`;
  //
  // If `bitLength < 32`, the unused highest bits are guaranteed
  // to be 0.
  uint32_t bits;

  // The actual length of buffer `bits`.
  //
  // MUST be within `[0, 32]`.
  //
  // If `bitLength < 32`, it means that some of the highest bits are unused.
  uint8_t bitLength;
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

// The default inline buffer length for instances of HuffmanTable.
// Specific type (e.g. booleans) will override this to provide something
// more suited to their type.
const size_t HUFFMAN_TABLE_DEFAULT_INLINE_BUFFER_LENGTH = 8;

// A flag that determines only whether a value is `null`.
// Used for optional interface.
enum class Nullable {
  Null,
  NonNull,
};

template <typename T, int N = HUFFMAN_TABLE_DEFAULT_INLINE_BUFFER_LENGTH>
class HuffmanTableImpl {
 public:
  explicit HuffmanTableImpl(JSContext* cx) : values(cx) {}
  HuffmanTableImpl(HuffmanTableImpl&& other) noexcept
      : values(std::move(other.values)) {}

  // Initialize a Huffman table containing a single value.
  JS::Result<Ok> initWithSingleValue(JSContext* cx, T&& value);

  // Initialize a Huffman table containing `numberOfSymbols`.
  // Symbols must be added with `addSymbol`.
  JS::Result<Ok> init(JSContext* cx, size_t numberOfSymbols);

  // Add a symbol to a value.
  JS::Result<Ok> addSymbol(uint32_t bits, uint8_t bits_length, T&& value);

  HuffmanTableImpl() = delete;
  HuffmanTableImpl(HuffmanTableImpl&) = delete;

  // Lookup a value in the table.
  //
  // Return an entry with a value of `nullptr` if the value is not in the table.
  //
  // The lookup may consume `[0, key.bitLength]` bits of `key`. Typically, in a
  // table with a single instance, or if the value is not in the table, 0 bits
  // will be consumed. The caller is responsible for advancing its bitstream by
  // `result.key.bitLength` bits.
  HuffmanEntry<const T*> lookup(HuffmanLookup key) const;

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

// An empty Huffman table. Attempting to get a value from this table is an
// error. This is the default value for `HuffmanTable` and represents all states
// that may not be reached.
//
// Part of variant `HuffmanTable`.
struct HuffmanTableUnreachable {};

// --- Explicit instantiations of `HuffmanTableImpl`.
// These classes are all parts of variant `HuffmanTable`.

struct HuffmanTableExplicitSymbolsF64 {
  using Contents = double;
  HuffmanTableImpl<double> impl;
  explicit HuffmanTableExplicitSymbolsF64(JSContext* cx) : impl(cx) {}
};

struct HuffmanTableExplicitSymbolsU32 {
  using Contents = uint32_t;
  HuffmanTableImpl<uint32_t> impl;
};

struct HuffmanTableIndexedSymbolsSum {
  using Contents = BinASTKind;
  HuffmanTableImpl<BinASTKind> impl;
  explicit HuffmanTableIndexedSymbolsSum(JSContext* cx) : impl(cx) {}
};

struct HuffmanTableIndexedSymbolsBool {
  using Contents = bool;
  HuffmanTableImpl<bool, 2> impl;
  explicit HuffmanTableIndexedSymbolsBool(JSContext* cx) : impl(cx) {}
};

struct HuffmanTableIndexedSymbolsMaybeInterface {
  using Contents = Nullable;
  HuffmanTableImpl<Nullable, 2> impl;
  explicit HuffmanTableIndexedSymbolsMaybeInterface(JSContext* cx) : impl(cx) {}

  // `true` if this table only contains values for `null`.
  bool isAlwaysNull() const {
    MOZ_ASSERT(impl.length() > 0);

    // By definition, we have either 1 or 2 values.
    // By definition, if we have 2 values, one of them is not null.
    if (impl.length() != 1) {
      return false;
    }
    // Otherwise, check the single value.
    return impl.begin()->value == Nullable::Null;
  }
};

struct HuffmanTableIndexedSymbolsStringEnum {
  using Contents = BinASTVariant;
  HuffmanTableImpl<BinASTVariant> impl;
  explicit HuffmanTableIndexedSymbolsStringEnum(JSContext* cx) : impl(cx) {}
};

struct HuffmanTableIndexedSymbolsLiteralString {
  using Contents = JSAtom*;
  HuffmanTableImpl<JSAtom*> impl;
  explicit HuffmanTableIndexedSymbolsLiteralString(JSContext* cx) : impl(cx) {}
};

struct HuffmanTableIndexedSymbolsOptionalLiteralString {
  using Contents = JSAtom*;
  HuffmanTableImpl<JSAtom*> impl;
  explicit HuffmanTableIndexedSymbolsOptionalLiteralString(JSContext* cx)
      : impl(cx) {}
};

// A single Huffman table.
using HuffmanTable = mozilla::Variant<
    HuffmanTableUnreachable,  // Default value.
    HuffmanTableExplicitSymbolsF64, HuffmanTableExplicitSymbolsU32,
    HuffmanTableIndexedSymbolsSum, HuffmanTableIndexedSymbolsMaybeInterface,
    HuffmanTableIndexedSymbolsBool, HuffmanTableIndexedSymbolsStringEnum,
    HuffmanTableIndexedSymbolsLiteralString,
    HuffmanTableIndexedSymbolsOptionalLiteralString>;

struct HuffmanTableExplicitSymbolsListLength {
  using Contents = uint32_t;
  HuffmanTableImpl<uint32_t> impl;
  explicit HuffmanTableExplicitSymbolsListLength(JSContext* cx) : impl(cx) {}
};

// A single Huffman table, specialized for list lengths.
using HuffmanTableListLength =
    mozilla::Variant<HuffmanTableUnreachable,  // Default value.
                     HuffmanTableExplicitSymbolsListLength>;

// A Huffman dictionary for the current file.
//
// A Huffman dictionary consists in a (contiguous) set of Huffman tables
// to predict field values and a second (contiguous) set of Huffman tables
// to predict list lengths.
class HuffmanDictionary {
 public:
  explicit HuffmanDictionary(JSContext* cx) : fields(cx), listLengths(cx) {}

  HuffmanTable& tableForField(NormalizedInterfaceAndField index);
  HuffmanTableListLength& tableForListLength(BinASTList list);

 private:
  // Huffman tables for `(Interface, Field)` pairs, used to decode the value of
  // `Interface::Field`. Some tables may be `HuffmanTableUnreacheable`
  // if they represent fields of interfaces that actually do not show up
  // in the file.
  //
  // The mapping from `(Interface, Field) -> index` is extracted statically from
  // the webidl specs.
  Vector<HuffmanTable, BINAST_INTERFACE_AND_FIELD_LIMIT> fields;

  // Huffman tables for list lengths. Some tables may be
  // `HuffmanTableUnreacheable` if they represent lists that actually do not
  // show up in the file.
  //
  // The mapping from `List -> index` is extracted statically from the webidl
  // specs.
  Vector<HuffmanTableListLength, BINAST_NUMBER_OF_LIST_TYPES> listLengths;
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
  using Context = BinASTTokenReaderBase::Context;

  // This implementation of `BinASTFields` is effectively `void`, as the format
  // does not embed field information.
  class BinASTFields {
   public:
    explicit BinASTFields(JSContext*) {}
  };
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

 private:
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
    HuffmanLookup getHuffmanLookup();

    // Advance the bit buffer by `bitLength` bits.
    template <Compression Compression>
    MOZ_MUST_USE JS::Result<Ok> advanceBitBuffer(
        BinASTTokenReaderContext& owner, const uint8_t bitLength);

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
  MOZ_MUST_USE JS::Result<bool> readBool(const Context&);

  /**
   * Read a single `number` value.
   */
  MOZ_MUST_USE JS::Result<double> readDouble(const Context&);

  /**
   * Read a single `string | null` value.
   *
   * Fails if that string is not valid UTF-8.
   */
  MOZ_MUST_USE JS::Result<JSAtom*> readMaybeAtom(const Context&);
  MOZ_MUST_USE JS::Result<JSAtom*> readAtom(const Context&);

  /**
   * Read a single IdentifierName value.
   */
  MOZ_MUST_USE JS::Result<JSAtom*> readMaybeIdentifierName(const Context&);
  MOZ_MUST_USE JS::Result<JSAtom*> readIdentifierName(const Context&);

  /**
   * Read a single PropertyKey value.
   */
  MOZ_MUST_USE JS::Result<JSAtom*> readPropertyKey(const Context&);

  /**
   * Read a single `string | null` value.
   *
   * MAY check if that string is not valid UTF-8.
   */
  MOZ_MUST_USE JS::Result<Ok> readChars(Chars&, const Context&);

  /**
   * Read a single `BinASTVariant | null` value.
   */
  MOZ_MUST_USE JS::Result<mozilla::Maybe<BinASTVariant>> readMaybeVariant(
      const Context&);
  MOZ_MUST_USE JS::Result<BinASTVariant> readVariant(const Context&);

  /**
   * Read over a single `[Skippable]` subtree value.
   *
   * This does *not* attempt to parse the subtree itself. Rather, the
   * returned `SkippableSubTree` contains the necessary information
   * to parse/tokenize the subtree at a later stage
   */
  MOZ_MUST_USE JS::Result<SkippableSubTree> readSkippableSubTree(
      const Context&);

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
  MOZ_MUST_USE JS::Result<Ok> enterList(uint32_t& length, const Context&,
                                        AutoList& guard);

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
  MOZ_MUST_USE JS::Result<Ok> enterTaggedTuple(
      BinASTKind& tag, BinASTTokenReaderContext::BinASTFields& fields,
      const Context&, AutoTaggedTuple& guard);

  /**
   * Read a single unsigned long.
   */
  MOZ_MUST_USE JS::Result<uint32_t> readUnsignedLong(const Context&);

 private:
  template <typename Table>
  MOZ_MUST_USE JS::Result<typename Table::Contents> readFieldFromTable(
      const Context&);

  /**
   * Report an "invalid value error".
   */
  MOZ_MUST_USE ErrorResult<JS::Error&> raiseInvalidValue(const Context&);

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
  js::HashMap<uint32_t, BinASTVariant, DefaultHasher<uint32_t>,
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

  JSContext* cx_;

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
    explicit AutoBase(BinASTTokenReaderContext& reader);
    ~AutoBase();

    // Raise an error if we are not in the expected position.
    MOZ_MUST_USE JS::Result<Ok> checkPosition(const uint8_t* expectedPosition);

    friend BinASTTokenReaderContext;
    void init();

    // Set to `true` if `init()` has been called. Reset to `false` once
    // all conditions have been checked.
    bool initialized_;
    BinASTTokenReaderContext& reader_;
  };

  // Guard class used to ensure that `enterList` is used properly.
  class MOZ_STACK_CLASS AutoList : public AutoBase {
   public:
    explicit AutoList(BinASTTokenReaderContext& reader);

    // Check that we have properly read to the end of the list.
    MOZ_MUST_USE JS::Result<Ok> done();

   protected:
    friend BinASTTokenReaderContext;
    void init();
  };

  // Guard class used to ensure that `enterTaggedTuple` is used properly.
  class MOZ_STACK_CLASS AutoTaggedTuple : public AutoBase {
   public:
    explicit AutoTaggedTuple(BinASTTokenReaderContext& reader);

    // Check that we have properly read to the end of the tuple.
    MOZ_MUST_USE JS::Result<Ok> done();
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

  template <size_t N>
  static JS::Result<Ok, JS::Error&> checkFields(
      const BinASTKind kind, const BinASTFields& actual,
      const BinASTField (&expected)[N]) {
    // Not implemented in this tokenizer.
    return Ok();
  }

  static JS::Result<Ok, JS::Error&> checkFields0(const BinASTKind kind,
                                                 const BinASTFields& actual) {
    // Not implemented in this tokenizer.
    return Ok();
  }
};

}  // namespace frontend
}  // namespace js

#endif  // frontend_BinASTTokenReaderContext_h

/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "frontend/BinASTTokenReaderContext.h"

#include "mozilla/Result.h"  // MOZ_TRY*

#include <string.h>  // memcmp, memmove

#include "frontend/BinAST-macros.h"  // BINJS_TRY*, BINJS_MOZ_TRY*
#include "vm/JSScript.h"             // ScriptSource

namespace js {
namespace frontend {

// The magic header, at the start of every binjs file.
const char CX_MAGIC_HEADER[] =
    "\x89"
    "BJS\r\n\0\n";

// The latest format version understood by this tokenizer.
const uint32_t MAGIC_FORMAT_VERSION = 2;

using AutoList = BinASTTokenReaderContext::AutoList;
using AutoTaggedTuple = BinASTTokenReaderContext::AutoTaggedTuple;
using CharSlice = BinaryASTSupport::CharSlice;
using Chars = BinASTTokenReaderContext::Chars;

BinASTTokenReaderContext::BinASTTokenReaderContext(JSContext* cx,
                                                   ErrorReporter* er,
                                                   const uint8_t* start,
                                                   const size_t length)
    : BinASTTokenReaderBase(cx, er, start, length),
      metadata_(nullptr),
      posBeforeTree_(nullptr) {
  MOZ_ASSERT(er);
}

BinASTTokenReaderContext::~BinASTTokenReaderContext() {
  if (metadata_ && metadataOwned_ == MetadataOwnership::Owned) {
    UniqueBinASTSourceMetadataPtr ptr(metadata_);
  }
  if (decoder_) {
    BrotliDecoderDestroyInstance(decoder_);
  }
}

template <>
JS::Result<Ok>
BinASTTokenReaderContext::readBuf<BinASTTokenReaderContext::Compression::No>(
    uint8_t* bytes, uint32_t len) {
  return Base::readBuf(bytes, len);
}

template <>
JS::Result<Ok>
BinASTTokenReaderContext::readBuf<BinASTTokenReaderContext::Compression::Yes>(
    uint8_t* bytes, uint32_t len) {
  while (availableDecodedLength() < len) {
    if (availableDecodedLength()) {
      memmove(bytes, decodedBufferBegin(), availableDecodedLength());
      bytes += availableDecodedLength();
      len -= availableDecodedLength();
    }

    if (isEOF()) {
      return raiseError("Unexpected end of file");
    }

    // We have exhausted the in-memory buffer. Start from the beginning.
    decodedBegin_ = 0;

    size_t inSize = stop_ - current_;
    size_t outSize = DECODED_BUFFER_SIZE;
    uint8_t* out = decodedBuffer_;

    BrotliDecoderResult result;
    result = BrotliDecoderDecompressStream(decoder_, &inSize, &current_,
                                           &outSize, &out,
                                           /* total_out = */ nullptr);
    if (result == BROTLI_DECODER_RESULT_ERROR) {
      return raiseError("Failed to decompress brotli stream");
    }

    decodedEnd_ = out - decodedBuffer_;
  }

  memmove(bytes, decodedBufferBegin(), len);
  decodedBegin_ += len;
  return Ok();
}

bool BinASTTokenReaderContext::isEOF() const {
  return BrotliDecoderIsFinished(decoder_);
}

template <>
JS::Result<uint8_t> BinASTTokenReaderContext::readByte<
    BinASTTokenReaderContext::Compression::No>() {
  return Base::readByte();
}

template <>
JS::Result<uint8_t> BinASTTokenReaderContext::readByte<
    BinASTTokenReaderContext::Compression::Yes>() {
  uint8_t buf;
  MOZ_TRY(readBuf<Compression::Yes>(&buf, 1));
  return buf;
}

BinASTSourceMetadata* BinASTTokenReaderContext::takeMetadata() {
  MOZ_ASSERT(metadataOwned_ == MetadataOwnership::Owned);
  metadataOwned_ = MetadataOwnership::Unowned;
  return metadata_;
}

JS::Result<Ok> BinASTTokenReaderContext::initFromScriptSource(
    ScriptSource* scriptSource) {
  metadata_ = scriptSource->binASTSourceMetadata();
  metadataOwned_ = MetadataOwnership::Unowned;

  return Ok();
}

JS::Result<Ok> BinASTTokenReaderContext::readHeader() {
  // Check that we don't call this function twice.
  MOZ_ASSERT(!posBeforeTree_);

  // Read global headers.
  MOZ_TRY(readConst(CX_MAGIC_HEADER));
  BINJS_MOZ_TRY_DECL(version, readVarU32<Compression::No>());

  if (version != MAGIC_FORMAT_VERSION) {
    return raiseError("Format version not implemented");
  }

  decoder_ = BrotliDecoderCreateInstance(/* alloc_func = */ nullptr,
                                         /* free_func = */ nullptr,
                                         /* opaque = */ nullptr);
  if (!decoder_) {
    return raiseError("Failed to create brotli decoder");
  }

  // TODO: handle strings and models here.

  return raiseError("Not Yet Implemented");
}

void BinASTTokenReaderContext::traceMetadata(JSTracer* trc) {
  if (metadata_) {
    metadata_->trace(trc);
  }
}

JS::Result<bool> BinASTTokenReaderContext::readBool(const Context&) {
  return raiseError("Not Yet Implemented");
}

JS::Result<double> BinASTTokenReaderContext::readDouble(const Context&) {
  return raiseError("Not Yet Implemented");
}

JS::Result<JSAtom*> BinASTTokenReaderContext::readMaybeAtom(const Context&) {
  return raiseError("Not Yet Implemented");
}

JS::Result<JSAtom*> BinASTTokenReaderContext::readAtom(const Context&) {
  return raiseError("Not Yet Implemented");
}

JS::Result<JSAtom*> BinASTTokenReaderContext::readMaybeIdentifierName(
    const Context&) {
  return raiseError("Not Yet Implemented");
}

JS::Result<JSAtom*> BinASTTokenReaderContext::readIdentifierName(
    const Context&) {
  return raiseError("Not Yet Implemented");
}

JS::Result<JSAtom*> BinASTTokenReaderContext::readPropertyKey(const Context&) {
  return raiseError("Not Yet Implemented");
}

JS::Result<Ok> BinASTTokenReaderContext::readChars(Chars& out, const Context&) {
  return raiseError("Not Yet Implemented");
}

JS::Result<BinASTVariant> BinASTTokenReaderContext::readVariant(
    const Context&) {
  return raiseError("Not Yet Implemented");
}

JS::Result<BinASTTokenReaderBase::SkippableSubTree>
BinASTTokenReaderContext::readSkippableSubTree(const Context&) {
  return raiseError("Not Yet Implemented");
}

JS::Result<Ok> BinASTTokenReaderContext::enterTaggedTuple(
    BinASTKind& tag, BinASTTokenReaderContext::BinASTFields&, const Context&,
    AutoTaggedTuple& guard) {
  return raiseError("Not Yet Implemented");
}

JS::Result<Ok> BinASTTokenReaderContext::enterList(uint32_t& items,
                                                   const Context&,
                                                   AutoList& guard) {
  return raiseError("Not Yet Implemented");
}

void BinASTTokenReaderContext::AutoBase::init() { initialized_ = true; }

BinASTTokenReaderContext::AutoBase::AutoBase(BinASTTokenReaderContext& reader)
    : initialized_(false), reader_(reader) {}

BinASTTokenReaderContext::AutoBase::~AutoBase() {
  // By now, the `AutoBase` must have been deinitialized by calling `done()`.
  // The only case in which we can accept not calling `done()` is if we have
  // bailed out because of an error.
  MOZ_ASSERT_IF(initialized_, reader_.hasRaisedError());
}

JS::Result<Ok> BinASTTokenReaderContext::AutoBase::checkPosition(
    const uint8_t* expectedEnd) {
  return reader_.raiseError("Not Yet Implemented");
}

BinASTTokenReaderContext::AutoList::AutoList(BinASTTokenReaderContext& reader)
    : AutoBase(reader) {}

void BinASTTokenReaderContext::AutoList::init() { AutoBase::init(); }

JS::Result<Ok> BinASTTokenReaderContext::AutoList::done() {
  return reader_.raiseError("Not Yet Implemented");
}

// Internal uint32_t
// Note that this is different than varnum in multipart.
//
// Encoded as variable length number.

template <BinASTTokenReaderContext::Compression compression>
JS::Result<uint32_t> BinASTTokenReaderContext::readVarU32() {
  uint32_t result = 0;
  uint32_t shift = 0;
  while (true) {
    MOZ_ASSERT(shift < 32);
    uint32_t byte;
    MOZ_TRY_VAR(byte, readByte<compression>());

    const uint32_t newResult = result | (byte & 0x7f) << shift;
    if (newResult < result) {
      return raiseError("Overflow during readVarU32");
    }

    result = newResult;
    shift += 7;

    if ((byte & 0x80) == 0) {
      return result;
    }

    if (shift >= 32) {
      return raiseError("Overflow during readVarU32");
    }
  }
}

JS::Result<uint32_t> BinASTTokenReaderContext::readUnsignedLong(const Context&) {
  return readVarU32<Compression::Yes>();
}

BinASTTokenReaderContext::AutoTaggedTuple::AutoTaggedTuple(
    BinASTTokenReaderContext& reader)
    : AutoBase(reader) {}

JS::Result<Ok> BinASTTokenReaderContext::AutoTaggedTuple::done() {
  return reader_.raiseError("Not Yet Implemented");
}

}  // namespace frontend

}  // namespace js

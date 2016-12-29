/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "webrtc/modules/rtp_rtcp/source/rtcp_packet/transport_feedback.h"

#include "webrtc/base/checks.h"
#include "webrtc/base/logging.h"
#include "webrtc/modules/rtp_rtcp/source/byte_io.h"
#include "webrtc/modules/rtp_rtcp/source/rtcp_utility.h"

namespace webrtc {
namespace rtcp {

// Header size:
// * 12 bytes Common Packet Format for RTCP Feedback Messages
// * 8 bytes FeedbackPacket header
static const uint32_t kHeaderSizeBytes = 12 + 8;
static const uint32_t kChunkSizeBytes = 2;
static const uint32_t kOneBitVectorCapacity = 14;
static const uint32_t kTwoBitVectorCapacity = 7;
static const uint32_t kRunLengthCapacity = 0x1FFF;
// TODO(sprang): Add support for dynamic max size for easier fragmentation,
// eg. set it to what's left in the buffer or IP_PACKET_SIZE.
// Size constraint imposed by RTCP common header: 16bit size field interpreted
// as number of four byte words minus the first header word.
static const uint32_t kMaxSizeBytes = (1 << 16) * 4;
static const uint32_t kMinSizeBytes = kHeaderSizeBytes + kChunkSizeBytes;
static const uint32_t kBaseScaleFactor =
    TransportFeedback::kDeltaScaleFactor * (1 << 8);

class PacketStatusChunk {
 public:
  virtual ~PacketStatusChunk() {}
  virtual uint16_t NumSymbols() const = 0;
  virtual void AppendSymbolsTo(
      std::vector<TransportFeedback::StatusSymbol>* vec) const = 0;
  virtual void WriteTo(uint8_t* buffer) const = 0;
};

uint8_t EncodeSymbol(TransportFeedback::StatusSymbol symbol) {
  switch (symbol) {
    case TransportFeedback::StatusSymbol::kNotReceived:
      return 0;
    case TransportFeedback::StatusSymbol::kReceivedSmallDelta:
      return 1;
    case TransportFeedback::StatusSymbol::kReceivedLargeDelta:
      return 2;
    default:
      RTC_NOTREACHED();
      return 0;
  }
}

TransportFeedback::StatusSymbol DecodeSymbol(uint8_t value) {
  switch (value) {
    case 0:
      return TransportFeedback::StatusSymbol::kNotReceived;
    case 1:
      return TransportFeedback::StatusSymbol::kReceivedSmallDelta;
    case 2:
      return TransportFeedback::StatusSymbol::kReceivedLargeDelta;
    default:
      RTC_NOTREACHED();
      return TransportFeedback::StatusSymbol::kNotReceived;
  }
}

TransportFeedback::TransportFeedback()
    : packet_sender_ssrc_(0),
      media_source_ssrc_(0),
      base_seq_(-1),
      base_time_(-1),
      feedback_seq_(0),
      last_seq_(-1),
      last_timestamp_(-1),
      first_symbol_cardinality_(0),
      vec_needs_two_bit_symbols_(false),
      size_bytes_(kHeaderSizeBytes) {
}

TransportFeedback::~TransportFeedback() {
  for (PacketStatusChunk* chunk : status_chunks_)
    delete chunk;
}

//  One Bit Status Vector Chunk
//
//  0                   1
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |T|S|       symbol list         |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
//  T = 1
//  S = 0
//  symbol list = 14 entries where 0 = not received, 1 = received

class OneBitVectorChunk : public PacketStatusChunk {
 public:
  static const int kCapacity = 14;

  explicit OneBitVectorChunk(
      std::deque<TransportFeedback::StatusSymbol>* symbols) {
    size_t input_size = symbols->size();
    for (size_t i = 0; i < kCapacity; ++i) {
      if (i < input_size) {
        symbols_[i] = symbols->front();
        symbols->pop_front();
      } else {
        symbols_[i] = TransportFeedback::StatusSymbol::kNotReceived;
      }
    }
  }

  virtual ~OneBitVectorChunk() {}

  uint16_t NumSymbols() const override { return kCapacity; }

  void AppendSymbolsTo(
      std::vector<TransportFeedback::StatusSymbol>* vec) const override {
    vec->insert(vec->end(), &symbols_[0], &symbols_[kCapacity]);
  }

  void WriteTo(uint8_t* buffer) const override {
    const int kSymbolsInFirstByte = 6;
    const int kSymbolsInSecondByte = 8;
    buffer[0] = 0x80u;
    for (int i = 0; i < kSymbolsInFirstByte; ++i) {
      uint8_t encoded_symbol = EncodeSymbol(symbols_[i]);
      RTC_DCHECK_LE(encoded_symbol, 1u);
      buffer[0] |= encoded_symbol << (kSymbolsInFirstByte - (i + 1));
    }
    buffer[1] = 0x00u;
    for (int i = 0; i < kSymbolsInSecondByte; ++i) {
      uint8_t encoded_symbol = EncodeSymbol(symbols_[i + kSymbolsInFirstByte]);
      RTC_DCHECK_LE(encoded_symbol, 1u);
      buffer[1] |= encoded_symbol << (kSymbolsInSecondByte - (i + 1));
    }
  }

  static OneBitVectorChunk* ParseFrom(const uint8_t* data) {
    OneBitVectorChunk* chunk = new OneBitVectorChunk();

    size_t index = 0;
    for (int i = 5; i >= 0; --i)  // Last 5 bits from first byte.
      chunk->symbols_[index++] = DecodeSymbol((data[0] >> i) & 0x01);
    for (int i = 7; i >= 0; --i)  // 8 bits from the last byte.
      chunk->symbols_[index++] = DecodeSymbol((data[1] >> i) & 0x01);

    return chunk;
  }

 private:
  OneBitVectorChunk() {}

  TransportFeedback::StatusSymbol symbols_[kCapacity];
};

//  Two Bit Status Vector Chunk
//
//  0                   1
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |T|S|       symbol list         |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
//  T = 1
//  S = 1
//  symbol list = 7 entries of two bits each, see (Encode|Decode)Symbol

class TwoBitVectorChunk : public PacketStatusChunk {
 public:
  static const int kCapacity = 7;

  explicit TwoBitVectorChunk(
      std::deque<TransportFeedback::StatusSymbol>* symbols) {
    size_t input_size = symbols->size();
    for (size_t i = 0; i < kCapacity; ++i) {
      if (i < input_size) {
        symbols_[i] = symbols->front();
        symbols->pop_front();
      } else {
        symbols_[i] = TransportFeedback::StatusSymbol::kNotReceived;
      }
    }
  }

  virtual ~TwoBitVectorChunk() {}

  uint16_t NumSymbols() const override { return kCapacity; }

  void AppendSymbolsTo(
      std::vector<TransportFeedback::StatusSymbol>* vec) const override {
    vec->insert(vec->end(), &symbols_[0], &symbols_[kCapacity]);
  }

  void WriteTo(uint8_t* buffer) const override {
    buffer[0] = 0xC0;
    buffer[0] |= EncodeSymbol(symbols_[0]) << 4;
    buffer[0] |= EncodeSymbol(symbols_[1]) << 2;
    buffer[0] |= EncodeSymbol(symbols_[2]);
    buffer[1] = EncodeSymbol(symbols_[3]) << 6;
    buffer[1] |= EncodeSymbol(symbols_[4]) << 4;
    buffer[1] |= EncodeSymbol(symbols_[5]) << 2;
    buffer[1] |= EncodeSymbol(symbols_[6]);
  }

  static TwoBitVectorChunk* ParseFrom(const uint8_t* buffer) {
    TwoBitVectorChunk* chunk = new TwoBitVectorChunk();

    chunk->symbols_[0] = DecodeSymbol((buffer[0] >> 4) & 0x03);
    chunk->symbols_[1] = DecodeSymbol((buffer[0] >> 2) & 0x03);
    chunk->symbols_[2] = DecodeSymbol(buffer[0] & 0x03);
    chunk->symbols_[3] = DecodeSymbol((buffer[1] >> 6) & 0x03);
    chunk->symbols_[4] = DecodeSymbol((buffer[1] >> 4) & 0x03);
    chunk->symbols_[5] = DecodeSymbol((buffer[1] >> 2) & 0x03);
    chunk->symbols_[6] = DecodeSymbol(buffer[1] & 0x03);

    return chunk;
  }

 private:
  TwoBitVectorChunk() {}

  TransportFeedback::StatusSymbol symbols_[kCapacity];
};

//  Two Bit Status Vector Chunk
//
//  0                   1
//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |T| S |       Run Length        |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
//  T = 0
//  S = symbol, see (Encode|Decode)Symbol
//  Run Length = Unsigned integer denoting the run length of the symbol

class RunLengthChunk : public PacketStatusChunk {
 public:
  RunLengthChunk(TransportFeedback::StatusSymbol symbol, size_t size)
      : symbol_(symbol), size_(size) {
    RTC_DCHECK_LE(size, 0x1FFFu);
  }

  virtual ~RunLengthChunk() {}

  uint16_t NumSymbols() const override { return size_; }

  void AppendSymbolsTo(
      std::vector<TransportFeedback::StatusSymbol>* vec) const override {
    vec->insert(vec->end(), size_, symbol_);
  }

  void WriteTo(uint8_t* buffer) const override {
    buffer[0] = EncodeSymbol(symbol_) << 5;  // Write S (T = 0 implicitly)
    buffer[0] |= (size_ >> 8) & 0x1F;  // 5 most significant bits of run length.
    buffer[1] = size_ & 0xFF;  // 8 least significant bits of run length.
  }

  static RunLengthChunk* ParseFrom(const uint8_t* buffer) {
    RTC_DCHECK_EQ(0, buffer[0] & 0x80);
    TransportFeedback::StatusSymbol symbol =
        DecodeSymbol((buffer[0] >> 5) & 0x03);
    uint16_t count = (static_cast<uint16_t>(buffer[0] & 0x1F) << 8) | buffer[1];

    return new RunLengthChunk(symbol, count);
  }

 private:
  const TransportFeedback::StatusSymbol symbol_;
  const size_t size_;
};

// Unwrap to a larger type, for easier handling of wraps.
int64_t TransportFeedback::Unwrap(uint16_t sequence_number) {
  if (last_seq_ == -1)
    return sequence_number;

  int64_t delta = sequence_number - last_seq_;
  if (IsNewerSequenceNumber(sequence_number,
                            static_cast<uint16_t>(last_seq_))) {
    if (delta < 0)
      delta += (1 << 16);
  } else if (delta > 0) {
    delta -= (1 << 16);
  }

  return last_seq_ + delta;
}

void TransportFeedback::WithPacketSenderSsrc(uint32_t ssrc) {
  packet_sender_ssrc_ = ssrc;
}

void TransportFeedback::WithMediaSourceSsrc(uint32_t ssrc) {
  media_source_ssrc_ = ssrc;
}

uint32_t TransportFeedback::GetPacketSenderSsrc() const {
  return packet_sender_ssrc_;
}

uint32_t TransportFeedback::GetMediaSourceSsrc() const {
  return media_source_ssrc_;
}
void TransportFeedback::WithBase(uint16_t base_sequence,
                                 int64_t ref_timestamp_us) {
  RTC_DCHECK_EQ(-1, base_seq_);
  RTC_DCHECK_NE(-1, ref_timestamp_us);
  base_seq_ = base_sequence;
  last_seq_ = base_sequence;
  base_time_ = ref_timestamp_us / kBaseScaleFactor;
  last_timestamp_ = base_time_ * kBaseScaleFactor;
}

void TransportFeedback::WithFeedbackSequenceNumber(uint8_t feedback_sequence) {
  feedback_seq_ = feedback_sequence;
}

bool TransportFeedback::WithReceivedPacket(uint16_t sequence_number,
                                           int64_t timestamp) {
  RTC_DCHECK_NE(-1, base_seq_);
  int64_t seq = Unwrap(sequence_number);
  if (seq != base_seq_ && seq <= last_seq_)
    return false;

  // Convert to ticks and round.
  int64_t delta_full = timestamp - last_timestamp_;
  delta_full +=
      delta_full < 0 ? -(kDeltaScaleFactor / 2) : kDeltaScaleFactor / 2;
  delta_full /= kDeltaScaleFactor;

  int16_t delta = static_cast<int16_t>(delta_full);
  // If larger than 16bit signed, we can't represent it - need new fb packet.
  if (delta != delta_full) {
    LOG(LS_WARNING) << "Delta value too large ( >= 2^16 ticks )";
    return false;
  }

  StatusSymbol symbol;
  if (delta >= 0 && delta <= 0xFF) {
    symbol = StatusSymbol::kReceivedSmallDelta;
  } else {
    symbol = StatusSymbol::kReceivedLargeDelta;
  }

  if (!AddSymbol(symbol, seq))
    return false;

  receive_deltas_.push_back(delta);
  last_timestamp_ += delta * kDeltaScaleFactor;
  return true;
}

// Add a symbol for a received packet, with the given sequence number. This
// method will add any "packet not received" symbols needed before this one.
bool TransportFeedback::AddSymbol(StatusSymbol symbol, int64_t seq) {
  while (last_seq_ < seq - 1) {
    if (!Encode(StatusSymbol::kNotReceived))
      return false;
    ++last_seq_;
  }

  if (!Encode(symbol))
    return false;

  last_seq_ = seq;
  return true;
}

// Append a symbol to the internal symbol vector. If the new state cannot be
// represented using a single status chunk, a chunk will first be emitted and
// the associated symbols removed from the internal symbol vector.
bool TransportFeedback::Encode(StatusSymbol symbol) {
  if (last_seq_ - base_seq_ + 1 > 0xFFFF) {
    LOG(LS_WARNING) << "Packet status count too large ( >= 2^16 )";
    return false;
  }

  bool is_two_bit;
  int delta_size;
  switch (symbol) {
    case StatusSymbol::kReceivedSmallDelta:
      delta_size = 1;
      is_two_bit = false;
      break;
    case StatusSymbol::kReceivedLargeDelta:
      delta_size = 2;
      is_two_bit = true;
      break;
    case StatusSymbol::kNotReceived:
      is_two_bit = false;
      delta_size = 0;
      break;
    default:
      RTC_NOTREACHED();
      return false;
  }

  if (symbol_vec_.empty()) {
    if (size_bytes_ + delta_size + kChunkSizeBytes > kMaxSizeBytes)
      return false;

    symbol_vec_.push_back(symbol);
    vec_needs_two_bit_symbols_ = is_two_bit;
    first_symbol_cardinality_ = 1;
    size_bytes_ += delta_size + kChunkSizeBytes;
    return true;
  }
  if (size_bytes_ + delta_size > kMaxSizeBytes)
    return false;

  // Capacity, in number of symbols, that a vector chunk could hold.
  size_t capacity = vec_needs_two_bit_symbols_ ? kTwoBitVectorCapacity
                                               : kOneBitVectorCapacity;

  // first_symbol_cardinality_ is the number of times the first symbol in
  // symbol_vec is repeated. So if that is equal to the size of symbol_vec,
  // there is only one kind of symbol - we can potentially RLE encode it.
  // If we have less than (capacity) symbols in symbol_vec, we can't know
  // for certain this will be RLE-encoded; if a different symbol is added
  // these symbols will be needed to emit a vector chunk instead. However,
  // if first_symbol_cardinality_ > capacity, then we cannot encode the
  // current state as a vector chunk - we must first emit symbol_vec as an
  // RLE-chunk and then add the new symbol.
  bool rle_candidate = symbol_vec_.size() == first_symbol_cardinality_ ||
                       first_symbol_cardinality_ > capacity;
  if (rle_candidate) {
    if (symbol_vec_.back() == symbol) {
      ++first_symbol_cardinality_;
      if (first_symbol_cardinality_ <= capacity) {
        symbol_vec_.push_back(symbol);
      } else if (first_symbol_cardinality_ == kRunLengthCapacity) {
        // Max length for an RLE-chunk reached.
        EmitRunLengthChunk();
      }
      size_bytes_ += delta_size;
      return true;
    } else {
      // New symbol does not match what's already in symbol_vec.
      if (first_symbol_cardinality_ >= capacity) {
        // Symbols in symbol_vec can only be RLE-encoded. Emit the RLE-chunk
        // and re-add input. symbol_vec is then guaranteed to have room for the
        // symbol, so recursion cannot continue.
        EmitRunLengthChunk();
        return Encode(symbol);
      }
      // Fall through and treat state as non RLE-candidate.
    }
  }

  // If this code point is reached, symbols in symbol_vec cannot be RLE-encoded.

  if (is_two_bit && !vec_needs_two_bit_symbols_) {
    // If the symbols in symbol_vec can be encoded using a one-bit chunk but
    // the input symbol cannot, first check if we can simply change target type.
    vec_needs_two_bit_symbols_ = true;
    if (symbol_vec_.size() >= kTwoBitVectorCapacity) {
      // symbol_vec contains more symbols than we can encode in a single
      // two-bit chunk. Emit a new vector append to the remains, if any.
      if (size_bytes_ + delta_size + kChunkSizeBytes > kMaxSizeBytes)
        return false;
      EmitVectorChunk();
      // If symbol_vec isn't empty after emitting a vector chunk, we need to
      // account for chunk size (otherwise handled by Encode method).
      if (!symbol_vec_.empty())
        size_bytes_ += kChunkSizeBytes;
      return Encode(symbol);
    }
    // symbol_vec symbols fit within a single two-bit vector chunk.
    capacity = kTwoBitVectorCapacity;
  }

  symbol_vec_.push_back(symbol);
  if (symbol_vec_.size() == capacity)
    EmitVectorChunk();

  size_bytes_ += delta_size;
  return true;
}

// Upon packet completion, emit any remaining symbols in symbol_vec that have
// not yet been emitted in a status chunk.
void TransportFeedback::EmitRemaining() {
  if (symbol_vec_.empty())
    return;

  size_t capacity = vec_needs_two_bit_symbols_ ? kTwoBitVectorCapacity
                                               : kOneBitVectorCapacity;
  if (first_symbol_cardinality_ > capacity) {
    EmitRunLengthChunk();
  } else {
    EmitVectorChunk();
  }
}

void TransportFeedback::EmitVectorChunk() {
  if (vec_needs_two_bit_symbols_) {
    status_chunks_.push_back(new TwoBitVectorChunk(&symbol_vec_));
  } else {
    status_chunks_.push_back(new OneBitVectorChunk(&symbol_vec_));
  }
  // Update first symbol cardinality to match what is potentially left in in
  // symbol_vec.
  first_symbol_cardinality_ = 1;
  for (size_t i = 1; i < symbol_vec_.size(); ++i) {
    if (symbol_vec_[i] != symbol_vec_[0])
      break;
    ++first_symbol_cardinality_;
  }
}

void TransportFeedback::EmitRunLengthChunk() {
  RTC_DCHECK_GE(first_symbol_cardinality_, symbol_vec_.size());
  status_chunks_.push_back(
      new RunLengthChunk(symbol_vec_.front(), first_symbol_cardinality_));
  symbol_vec_.clear();
}

size_t TransportFeedback::BlockLength() const {
  return size_bytes_;
}

uint16_t TransportFeedback::GetBaseSequence() const {
  return base_seq_;
}

int64_t TransportFeedback::GetBaseTimeUs() const {
  return base_time_ * kBaseScaleFactor;
}

std::vector<TransportFeedback::StatusSymbol>
TransportFeedback::GetStatusVector() const {
  std::vector<TransportFeedback::StatusSymbol> symbols;
  for (PacketStatusChunk* chunk : status_chunks_)
    chunk->AppendSymbolsTo(&symbols);
  int64_t status_count = last_seq_ - base_seq_ + 1;
  // If packet ends with a vector chunk, it may contain extraneous "packet not
  // received"-symbols at the end. Crop any such symbols.
  symbols.erase(symbols.begin() + status_count, symbols.end());
  return symbols;
}

std::vector<int16_t> TransportFeedback::GetReceiveDeltas() const {
  return receive_deltas_;
}

std::vector<int64_t> TransportFeedback::GetReceiveDeltasUs() const {
  if (receive_deltas_.empty())
    return std::vector<int64_t>();

  std::vector<int64_t> us_deltas;
  for (int16_t delta : receive_deltas_)
    us_deltas.push_back(static_cast<int64_t>(delta) * kDeltaScaleFactor);

  return us_deltas;
}

// Serialize packet.
bool TransportFeedback::Create(uint8_t* packet,
                               size_t* position,
                               size_t max_length,
                               PacketReadyCallback* callback) const {
  if (base_seq_ == -1)
    return false;

  while (*position + size_bytes_ > max_length) {
    if (!OnBufferFull(packet, position, callback))
      return false;
  }

  CreateHeader(kFeedbackMessageType, kPayloadType, HeaderLength(), packet,
               position);
  ByteWriter<uint32_t>::WriteBigEndian(&packet[*position], packet_sender_ssrc_);
  *position += 4;
  ByteWriter<uint32_t>::WriteBigEndian(&packet[*position], media_source_ssrc_);
  *position += 4;

  RTC_DCHECK_LE(base_seq_, 0xFFFF);
  ByteWriter<uint16_t>::WriteBigEndian(&packet[*position], base_seq_);
  *position += 2;

  int64_t status_count = last_seq_ - base_seq_ + 1;
  RTC_DCHECK_LE(status_count, 0xFFFF);
  ByteWriter<uint16_t>::WriteBigEndian(&packet[*position], status_count);
  *position += 2;

  ByteWriter<int32_t, 3>::WriteBigEndian(&packet[*position],
                                         static_cast<int32_t>(base_time_));
  *position += 3;

  packet[(*position)++] = feedback_seq_;

  // TODO(sprang): Get rid of this cast.
  const_cast<TransportFeedback*>(this)->EmitRemaining();
  for (PacketStatusChunk* chunk : status_chunks_) {
    chunk->WriteTo(&packet[*position]);
    *position += 2;
  }

  for (int16_t delta : receive_deltas_) {
    if (delta >= 0 && delta <= 0xFF) {
      packet[(*position)++] = delta;
    } else {
      ByteWriter<int16_t>::WriteBigEndian(&packet[*position], delta);
      *position += 2;
    }
  }

  while ((*position % 4) != 0)
    packet[(*position)++] = 0;

  return true;
}

//    Message format
//
//     0                   1                   2                   3
//     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//    |V=2|P|  FMT=15 |    PT=205     |           length              |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//    |                     SSRC of packet sender                     |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//    |                      SSRC of media source                     |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//    |      base sequence number     |      packet status count      |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//    |                 reference time                | fb pkt. count |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//    |          packet chunk         |         packet chunk          |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//    .                                                               .
//    .                                                               .
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//    |         packet chunk          |  recv delta   |  recv delta   |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//    .                                                               .
//    .                                                               .
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//    |           recv delta          |  recv delta   | zero padding  |
//    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

// De-serialize packet.
rtc::scoped_ptr<TransportFeedback> TransportFeedback::ParseFrom(
    const uint8_t* buffer,
    size_t length) {
  rtc::scoped_ptr<TransportFeedback> packet(new TransportFeedback());

  if (length < kMinSizeBytes) {
    LOG(LS_WARNING) << "Buffer too small (" << length
                    << " bytes) to fit a "
                       "FeedbackPacket. Minimum size = " << kMinSizeBytes;
    return nullptr;
  }

  RTCPUtility::RtcpCommonHeader header;
  if (!RtcpParseCommonHeader(buffer, length, &header))
    return nullptr;

  if (header.count_or_format != kFeedbackMessageType) {
    LOG(LS_WARNING) << "Invalid RTCP header: FMT must be "
                    << kFeedbackMessageType << " but was "
                    << header.count_or_format;
    return nullptr;
  }

  if (header.packet_type != kPayloadType) {
    LOG(LS_WARNING) << "Invalid RTCP header: PT must be " << kPayloadType
                    << " but was " << header.packet_type;
    return nullptr;
  }

  packet->packet_sender_ssrc_ = ByteReader<uint32_t>::ReadBigEndian(&buffer[4]);
  packet->media_source_ssrc_ = ByteReader<uint32_t>::ReadBigEndian(&buffer[8]);
  packet->base_seq_ = ByteReader<uint16_t>::ReadBigEndian(&buffer[12]);
  uint16_t num_packets = ByteReader<uint16_t>::ReadBigEndian(&buffer[14]);
  packet->base_time_ = ByteReader<int32_t, 3>::ReadBigEndian(&buffer[16]);
  packet->feedback_seq_ = buffer[19];
  size_t index = 20;
  const size_t end_index = kHeaderLength + header.payload_size_bytes;

  if (num_packets == 0) {
    LOG(LS_WARNING) << "Empty feedback messages not allowed.";
    return nullptr;
  }
  packet->last_seq_ = packet->base_seq_ + num_packets - 1;

  size_t packets_read = 0;
  while (packets_read < num_packets) {
    if (index + 2 > end_index) {
      LOG(LS_WARNING) << "Buffer overflow while parsing packet.";
      return nullptr;
    }

    PacketStatusChunk* chunk =
        ParseChunk(&buffer[index], num_packets - packets_read);
    if (chunk == nullptr)
      return nullptr;

    index += 2;
    packet->status_chunks_.push_back(chunk);
    packets_read += chunk->NumSymbols();
  }

  std::vector<StatusSymbol> symbols = packet->GetStatusVector();

  RTC_DCHECK_EQ(num_packets, symbols.size());

  for (StatusSymbol symbol : symbols) {
    switch (symbol) {
      case StatusSymbol::kReceivedSmallDelta:
        if (index + 1 > end_index) {
          LOG(LS_WARNING) << "Buffer overflow while parsing packet.";
          return nullptr;
        }
        packet->receive_deltas_.push_back(buffer[index]);
        ++index;
        break;
      case StatusSymbol::kReceivedLargeDelta:
        if (index + 2 > end_index) {
          LOG(LS_WARNING) << "Buffer overflow while parsing packet.";
          return nullptr;
        }
        packet->receive_deltas_.push_back(
            ByteReader<int16_t>::ReadBigEndian(&buffer[index]));
        index += 2;
        break;
      default:
        continue;
    }
  }

  RTC_DCHECK_GE(index, end_index - 3);
  RTC_DCHECK_LE(index, end_index);

  return packet;
}

PacketStatusChunk* TransportFeedback::ParseChunk(const uint8_t* buffer,
                                                 size_t max_size) {
  if (buffer[0] & 0x80) {
    // First bit set => vector chunk.
    std::deque<StatusSymbol> symbols;
    if (buffer[0] & 0x40) {
      // Second bit set => two bits per symbol vector.
      return TwoBitVectorChunk::ParseFrom(buffer);
    }

    // Second bit not set => one bit per symbol vector.
    return OneBitVectorChunk::ParseFrom(buffer);
  }

  // First bit not set => RLE chunk.
  RunLengthChunk* rle_chunk = RunLengthChunk::ParseFrom(buffer);
  if (rle_chunk->NumSymbols() > max_size) {
    LOG(LS_WARNING) << "Header/body mismatch. "
                       "RLE block of size " << rle_chunk->NumSymbols()
                    << " but only " << max_size << " left to read.";
    delete rle_chunk;
    return nullptr;
  }
  return rle_chunk;
}

}  // namespace rtcp
}  // namespace webrtc

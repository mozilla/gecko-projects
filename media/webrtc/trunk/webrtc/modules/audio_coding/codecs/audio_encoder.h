/*
 *  Copyright (c) 2014 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_MODULES_AUDIO_CODING_CODECS_AUDIO_ENCODER_H_
#define WEBRTC_MODULES_AUDIO_CODING_CODECS_AUDIO_ENCODER_H_

#include <algorithm>
#include <vector>

#include "webrtc/base/array_view.h"
#include "webrtc/typedefs.h"

namespace webrtc {

// This is the interface class for encoders in AudioCoding module. Each codec
// type must have an implementation of this class.
class AudioEncoder {
 public:
  struct EncodedInfoLeaf {
    size_t encoded_bytes = 0;
    uint32_t encoded_timestamp = 0;
    int payload_type = 0;
    bool send_even_if_empty = false;
    bool speech = true;
  };

  // This is the main struct for auxiliary encoding information. Each encoded
  // packet should be accompanied by one EncodedInfo struct, containing the
  // total number of |encoded_bytes|, the |encoded_timestamp| and the
  // |payload_type|. If the packet contains redundant encodings, the |redundant|
  // vector will be populated with EncodedInfoLeaf structs. Each struct in the
  // vector represents one encoding; the order of structs in the vector is the
  // same as the order in which the actual payloads are written to the byte
  // stream. When EncoderInfoLeaf structs are present in the vector, the main
  // struct's |encoded_bytes| will be the sum of all the |encoded_bytes| in the
  // vector.
  struct EncodedInfo : public EncodedInfoLeaf {
    EncodedInfo();
    ~EncodedInfo();

    std::vector<EncodedInfoLeaf> redundant;
  };

  virtual ~AudioEncoder() = default;

  // Returns the maximum number of bytes that can be produced by the encoder
  // at each Encode() call. The caller can use the return value to determine
  // the size of the buffer that needs to be allocated. This value is allowed
  // to depend on encoder parameters like bitrate, frame size etc., so if
  // any of these change, the caller of Encode() is responsible for checking
  // that the buffer is large enough by calling MaxEncodedBytes() again.
  virtual size_t MaxEncodedBytes() const = 0;

  // Returns the input sample rate in Hz and the number of input channels.
  // These are constants set at instantiation time.
  virtual int SampleRateHz() const = 0;
  virtual size_t NumChannels() const = 0;

  // Returns the rate at which the RTP timestamps are updated. The default
  // implementation returns SampleRateHz().
  virtual int RtpTimestampRateHz() const;

  // Returns the number of 10 ms frames the encoder will put in the next
  // packet. This value may only change when Encode() outputs a packet; i.e.,
  // the encoder may vary the number of 10 ms frames from packet to packet, but
  // it must decide the length of the next packet no later than when outputting
  // the preceding packet.
  virtual size_t Num10MsFramesInNextPacket() const = 0;

  // Returns the maximum value that can be returned by
  // Num10MsFramesInNextPacket().
  virtual size_t Max10MsFramesInAPacket() const = 0;

  // Returns the current target bitrate in bits/s. The value -1 means that the
  // codec adapts the target automatically, and a current target cannot be
  // provided.
  virtual int GetTargetBitrate() const = 0;

  // Accepts one 10 ms block of input audio (i.e., SampleRateHz() / 100 *
  // NumChannels() samples). Multi-channel audio must be sample-interleaved.
  // The encoder produces zero or more bytes of output in |encoded| and
  // returns additional encoding information.
  // The caller is responsible for making sure that |max_encoded_bytes| is
  // not smaller than the number of bytes actually produced by the encoder.
  // Encode() checks some preconditions, calls EncodeInternal() which does the
  // actual work, and then checks some postconditions.
  EncodedInfo Encode(uint32_t rtp_timestamp,
                     rtc::ArrayView<const int16_t> audio,
                     size_t max_encoded_bytes,
                     uint8_t* encoded);

  virtual EncodedInfo EncodeInternal(uint32_t rtp_timestamp,
                                     rtc::ArrayView<const int16_t> audio,
                                     size_t max_encoded_bytes,
                                     uint8_t* encoded) = 0;

  // Resets the encoder to its starting state, discarding any input that has
  // been fed to the encoder but not yet emitted in a packet.
  virtual void Reset() = 0;

  // Enables or disables codec-internal FEC (forward error correction). Returns
  // true if the codec was able to comply. The default implementation returns
  // true when asked to disable FEC and false when asked to enable it (meaning
  // that FEC isn't supported).
  virtual bool SetFec(bool enable);

  // Enables or disables codec-internal VAD/DTX. Returns true if the codec was
  // able to comply. The default implementation returns true when asked to
  // disable DTX and false when asked to enable it (meaning that DTX isn't
  // supported).
  virtual bool SetDtx(bool enable);

  // Sets the application mode. Returns true if the codec was able to comply.
  // The default implementation just returns false.
  enum class Application { kSpeech, kAudio };
  virtual bool SetApplication(Application application);

  // Tells the encoder about the highest sample rate the decoder is expected to
  // use when decoding the bitstream. The encoder would typically use this
  // information to adjust the quality of the encoding. The default
  // implementation does nothing.
  virtual void SetMaxPlaybackRate(int frequency_hz);

  // Tells the encoder what the projected packet loss rate is. The rate is in
  // the range [0.0, 1.0]. The encoder would typically use this information to
  // adjust channel coding efforts, such as FEC. The default implementation
  // does nothing.
  virtual void SetProjectedPacketLossRate(double fraction);

  // Tells the encoder what average bitrate we'd like it to produce. The
  // encoder is free to adjust or disregard the given bitrate (the default
  // implementation does the latter).
  virtual void SetTargetBitrate(int target_bps);
};
}  // namespace webrtc
#endif  // WEBRTC_MODULES_AUDIO_CODING_CODECS_AUDIO_ENCODER_H_

/*
 *  Copyright (c) 2014 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "webrtc/modules/audio_coding/codecs/red/audio_encoder_copy_red.h"

#include <string.h>

#include "webrtc/base/checks.h"

namespace webrtc {

AudioEncoderCopyRed::AudioEncoderCopyRed(const Config& config)
    : speech_encoder_(config.speech_encoder),
      red_payload_type_(config.payload_type) {
  RTC_CHECK(speech_encoder_) << "Speech encoder not provided.";
}

AudioEncoderCopyRed::~AudioEncoderCopyRed() = default;

size_t AudioEncoderCopyRed::MaxEncodedBytes() const {
  return 2 * speech_encoder_->MaxEncodedBytes();
}

int AudioEncoderCopyRed::SampleRateHz() const {
  return speech_encoder_->SampleRateHz();
}

size_t AudioEncoderCopyRed::NumChannels() const {
  return speech_encoder_->NumChannels();
}

int AudioEncoderCopyRed::RtpTimestampRateHz() const {
  return speech_encoder_->RtpTimestampRateHz();
}

size_t AudioEncoderCopyRed::Num10MsFramesInNextPacket() const {
  return speech_encoder_->Num10MsFramesInNextPacket();
}

size_t AudioEncoderCopyRed::Max10MsFramesInAPacket() const {
  return speech_encoder_->Max10MsFramesInAPacket();
}

int AudioEncoderCopyRed::GetTargetBitrate() const {
  return speech_encoder_->GetTargetBitrate();
}

AudioEncoder::EncodedInfo AudioEncoderCopyRed::EncodeInternal(
    uint32_t rtp_timestamp,
    rtc::ArrayView<const int16_t> audio,
    size_t max_encoded_bytes,
    uint8_t* encoded) {
  EncodedInfo info =
      speech_encoder_->Encode(rtp_timestamp, audio, max_encoded_bytes, encoded);
  RTC_CHECK_GE(max_encoded_bytes,
               info.encoded_bytes + secondary_info_.encoded_bytes);
  RTC_CHECK(info.redundant.empty()) << "Cannot use nested redundant encoders.";

  if (info.encoded_bytes > 0) {
    // |info| will be implicitly cast to an EncodedInfoLeaf struct, effectively
    // discarding the (empty) vector of redundant information. This is
    // intentional.
    info.redundant.push_back(info);
    RTC_DCHECK_EQ(info.redundant.size(), 1u);
    if (secondary_info_.encoded_bytes > 0) {
      memcpy(&encoded[info.encoded_bytes], secondary_encoded_.data(),
             secondary_info_.encoded_bytes);
      info.redundant.push_back(secondary_info_);
      RTC_DCHECK_EQ(info.redundant.size(), 2u);
    }
    // Save primary to secondary.
    secondary_encoded_.SetData(encoded, info.encoded_bytes);
    secondary_info_ = info;
    RTC_DCHECK_EQ(info.speech, info.redundant[0].speech);
  }
  // Update main EncodedInfo.
  info.payload_type = red_payload_type_;
  info.encoded_bytes = 0;
  for (std::vector<EncodedInfoLeaf>::const_iterator it = info.redundant.begin();
       it != info.redundant.end(); ++it) {
    info.encoded_bytes += it->encoded_bytes;
  }
  return info;
}

void AudioEncoderCopyRed::Reset() {
  speech_encoder_->Reset();
  secondary_encoded_.Clear();
  secondary_info_.encoded_bytes = 0;
}

bool AudioEncoderCopyRed::SetFec(bool enable) {
  return speech_encoder_->SetFec(enable);
}

bool AudioEncoderCopyRed::SetDtx(bool enable) {
  return speech_encoder_->SetDtx(enable);
}

bool AudioEncoderCopyRed::SetApplication(Application application) {
  return speech_encoder_->SetApplication(application);
}

void AudioEncoderCopyRed::SetMaxPlaybackRate(int frequency_hz) {
  speech_encoder_->SetMaxPlaybackRate(frequency_hz);
}

void AudioEncoderCopyRed::SetProjectedPacketLossRate(double fraction) {
  speech_encoder_->SetProjectedPacketLossRate(fraction);
}

void AudioEncoderCopyRed::SetTargetBitrate(int bits_per_second) {
  speech_encoder_->SetTargetBitrate(bits_per_second);
}

}  // namespace webrtc

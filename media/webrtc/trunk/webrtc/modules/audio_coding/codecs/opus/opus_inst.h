/*
 *  Copyright (c) 2014 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_MODULES_AUDIO_CODING_CODECS_OPUS_OPUS_INST_H_
#define WEBRTC_MODULES_AUDIO_CODING_CODECS_OPUS_OPUS_INST_H_

#include <stddef.h>

#include "opus.h"

struct WebRtcOpusEncInst {
  OpusEncoder* encoder;
  size_t channels;
  int in_dtx_mode;
  // When Opus is in DTX mode, we use |zero_counts| to count consecutive zeros
  // to break long zero segment so as to prevent DTX from going wrong. We use
  // one counter for each channel. After each encoding, |zero_counts| contain
  // the remaining zeros from the last frame.
  // TODO(minyue): remove this when Opus gets an internal fix to DTX.
  size_t* zero_counts;
};

struct WebRtcOpusDecInst {
  OpusDecoder* decoder;
  int prev_decoded_samples;
  size_t channels;
  int in_dtx_mode;
};


#endif  // WEBRTC_MODULES_AUDIO_CODING_CODECS_OPUS_OPUS_INST_H_

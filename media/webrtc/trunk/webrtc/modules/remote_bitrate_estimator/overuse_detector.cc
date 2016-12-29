/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "webrtc/modules/remote_bitrate_estimator/overuse_detector.h"

#include <math.h>
#include <stdlib.h>

#include <algorithm>
#include <sstream>
#include <string>

#include "webrtc/base/checks.h"
#include "webrtc/base/common.h"
#include "webrtc/modules/remote_bitrate_estimator/include/bwe_defines.h"
#include "webrtc/modules/remote_bitrate_estimator/test/bwe_test_logging.h"
#include "webrtc/modules/rtp_rtcp/source/rtp_utility.h"
#include "webrtc/system_wrappers/include/field_trial.h"
#include "webrtc/system_wrappers/include/trace.h"

namespace webrtc {

const char kAdaptiveThresholdExperiment[] = "WebRTC-AdaptiveBweThreshold";
const char kEnabledPrefix[] = "Enabled";
const size_t kEnabledPrefixLength = sizeof(kEnabledPrefix) - 1;
const size_t kMinExperimentLength = kEnabledPrefixLength + 3;

const double kMaxAdaptOffsetMs = 15.0;
const double kOverUsingTimeThreshold = 10;

bool AdaptiveThresholdExperimentIsEnabled() {
#ifdef CONVERT_TO_MOZILLA_ABOUT_CONFIG
  std::string experiment_string =
      webrtc::field_trial::FindFullName(kAdaptiveThresholdExperiment);
  if (experiment_string.length() < kMinExperimentLength)
    return false;
  return experiment_string.substr(0, kEnabledPrefixLength) == kEnabledPrefix;
#else
  return false;
#endif
}

// Gets thresholds from the experiment name following the format
// "WebRTC-AdaptiveBweThreshold/Enabled-0.5,0.002/".
bool ReadExperimentConstants(double* k_up, double* k_down) {
#ifdef CONVERT_TO_MOZILLA_ABOUT_CONFIG
  std::string experiment_string =
      webrtc::field_trial::FindFullName(kAdaptiveThresholdExperiment);
  return sscanf(experiment_string.substr(kEnabledPrefixLength + 1).c_str(),
                "%lf,%lf", k_up, k_down) == 2;
#else
  return false;
#endif
}

OveruseDetector::OveruseDetector(const OverUseDetectorOptions& options)
    : in_experiment_(AdaptiveThresholdExperimentIsEnabled()),
      k_up_(0.01),
      k_down_(0.00018),
      overusing_time_threshold_(100),
      options_(options),
      threshold_(12.5),
      last_update_ms_(-1),
      prev_offset_(0.0),
      time_over_using_(-1),
      overuse_counter_(0),
      hypothesis_(kBwNormal) {
  if (in_experiment_)
    InitializeExperiment();
}

OveruseDetector::~OveruseDetector() {}

BandwidthUsage OveruseDetector::State() const {
  return hypothesis_;
}

BandwidthUsage OveruseDetector::Detect(double offset,
                                       double ts_delta,
                                       int num_of_deltas,
                                       int64_t now_ms) {
  if (num_of_deltas < 2) {
    return kBwNormal;
  }
  const double prev_offset = prev_offset_;
  prev_offset_ = offset;
  const double T = std::min(num_of_deltas, 60) * offset;
  BWE_TEST_LOGGING_PLOT(1, "offset", now_ms, T);
  BWE_TEST_LOGGING_PLOT(1, "threshold", now_ms, threshold_);
  if (T > threshold_) {
    if (time_over_using_ == -1) {
      // Initialize the timer. Assume that we've been
      // over-using half of the time since the previous
      // sample.
      time_over_using_ = ts_delta / 2;
    } else {
      // Increment timer
      time_over_using_ += ts_delta;
    }
    overuse_counter_++;
    if (time_over_using_ > overusing_time_threshold_ && overuse_counter_ > 1) {
      if (offset >= prev_offset) {
        time_over_using_ = 0;
        overuse_counter_ = 0;
        hypothesis_ = kBwOverusing;
      }
    }
  } else if (T < -threshold_) {
    time_over_using_ = -1;
    overuse_counter_ = 0;
    hypothesis_ = kBwUnderusing;
  } else {
    time_over_using_ = -1;
    overuse_counter_ = 0;
    hypothesis_ = kBwNormal;
  }

  UpdateThreshold(T, now_ms);

  return hypothesis_;
}

void OveruseDetector::UpdateThreshold(double modified_offset, int64_t now_ms) {
  if (!in_experiment_)
    return;

  if (last_update_ms_ == -1)
    last_update_ms_ = now_ms;

  if (fabs(modified_offset) > threshold_ + kMaxAdaptOffsetMs) {
    // Avoid adapting the threshold to big latency spikes, caused e.g.,
    // by a sudden capacity drop.
    last_update_ms_ = now_ms;
    return;
  }

  const double k = fabs(modified_offset) < threshold_ ? k_down_ : k_up_;
  threshold_ +=
      k * (fabs(modified_offset) - threshold_) * (now_ms - last_update_ms_);

  const double kMinThreshold = 6;
  const double kMaxThreshold = 600;
  threshold_ = std::min(std::max(threshold_, kMinThreshold), kMaxThreshold);

  last_update_ms_ = now_ms;
}

void OveruseDetector::InitializeExperiment() {
  RTC_DCHECK(in_experiment_);
  double k_up = 0.0;
  double k_down = 0.0;
  overusing_time_threshold_ = kOverUsingTimeThreshold;
  if (ReadExperimentConstants(&k_up, &k_down)) {
    k_up_ = k_up;
    k_down_ = k_down;
  }
}
}  // namespace webrtc

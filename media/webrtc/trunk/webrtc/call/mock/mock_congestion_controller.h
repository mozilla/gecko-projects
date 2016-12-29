/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_CALL_MOCK_MOCK_CONGESTION_CONTROLLER_H_
#define WEBRTC_CALL_MOCK_MOCK_CONGESTION_CONTROLLER_H_

#include "testing/gmock/include/gmock/gmock.h"
#include "webrtc/call/congestion_controller.h"

namespace webrtc {
namespace test {

class MockCongestionController : public CongestionController {
 public:
  MockCongestionController(ProcessThread* process_thread,
                           CallStats* call_stats,
                           BitrateObserver* bitrate_observer)
      : CongestionController(process_thread, call_stats, bitrate_observer) {}
  MOCK_METHOD1(AddEncoder, void(ViEEncoder* encoder));
  MOCK_METHOD1(RemoveEncoder, void(ViEEncoder* encoder));
  MOCK_METHOD3(SetBweBitrates,
               void(int min_bitrate_bps,
                    int start_bitrate_bps,
                    int max_bitrate_bps));
  MOCK_METHOD3(SetChannelRembStatus,
               void(bool sender, bool receiver, RtpRtcp* rtp_module));
  MOCK_METHOD1(SignalNetworkState, void(NetworkState state));
  MOCK_CONST_METHOD0(GetBitrateController, BitrateController*());
  MOCK_CONST_METHOD1(GetRemoteBitrateEstimator,
                     RemoteBitrateEstimator*(bool send_side_bwe));
  MOCK_CONST_METHOD0(GetPacerQueuingDelayMs, int64_t());
  MOCK_CONST_METHOD0(pacer, PacedSender*());
  MOCK_CONST_METHOD0(packet_router, PacketRouter*());
  MOCK_METHOD0(GetTransportFeedbackObserver, TransportFeedbackObserver*());
  MOCK_METHOD3(UpdatePacerBitrate,
               void(int bitrate_kbps,
                    int max_bitrate_kbps,
                    int min_bitrate_kbps));
  MOCK_METHOD1(OnSentPacket, void(const rtc::SentPacket& sent_packet));

  RTC_DISALLOW_IMPLICIT_CONSTRUCTORS(MockCongestionController);
};
}  // namespace test
}  // namespace webrtc
#endif  // WEBRTC_CALL_MOCK_MOCK_CONGESTION_CONTROLLER_H_

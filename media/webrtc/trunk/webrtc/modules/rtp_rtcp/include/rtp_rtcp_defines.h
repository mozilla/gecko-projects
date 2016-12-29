/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_MODULES_RTP_RTCP_INCLUDE_RTP_RTCP_DEFINES_H_
#define WEBRTC_MODULES_RTP_RTCP_INCLUDE_RTP_RTCP_DEFINES_H_

#include <stddef.h>
#include <list>

#include "webrtc/modules/include/module_common_types.h"
#include "webrtc/system_wrappers/include/clock.h"
#include "webrtc/typedefs.h"

#define RTCP_CNAME_SIZE 256    // RFC 3550 page 44, including null termination
#define IP_PACKET_SIZE 1500    // we assume ethernet
#define MAX_NUMBER_OF_PARALLEL_TELEPHONE_EVENTS 10
#define TIMEOUT_SEI_MESSAGES_MS 30000   // in milliseconds

namespace webrtc {
namespace rtcp {
class TransportFeedback;
}

const int kVideoPayloadTypeFrequency = 90000;

// Minimum RTP header size in bytes.
const uint8_t kRtpHeaderSize = 12;

struct AudioPayload {
    uint32_t    frequency;
    size_t      channels;
    uint32_t    rate;
};

struct VideoPayload {
    RtpVideoCodecTypes   videoCodecType;
    uint32_t       maxRate;
};

union PayloadUnion {
    AudioPayload Audio;
    VideoPayload Video;
};

enum RTPAliveType { kRtpDead = 0, kRtpNoRtp = 1, kRtpAlive = 2 };

enum ProtectionType {
  kUnprotectedPacket,
  kProtectedPacket
};

enum StorageType {
  kDontRetransmit,
  kAllowRetransmission
};

enum RTPExtensionType {
  kRtpExtensionNone,
  kRtpExtensionTransmissionTimeOffset,
  kRtpExtensionAudioLevel,
  kRtpExtensionAbsoluteSendTime,
  kRtpExtensionVideoRotation,
  kRtpExtensionTransportSequenceNumber,
  kRtpExtensionRtpStreamId,
};

enum RTCPAppSubTypes { kAppSubtypeBwe = 0x00 };

// TODO(sprang): Make this an enum class once rtcp_receiver has been cleaned up.
enum RTCPPacketType : uint32_t {
  kRtcpReport = 0x0001,
  kRtcpSr = 0x0002,
  kRtcpRr = 0x0004,
  kRtcpSdes = 0x0008,
  kRtcpBye = 0x0010,
  kRtcpPli = 0x0020,
  kRtcpNack = 0x0040,
  kRtcpFir = 0x0080,
  kRtcpTmmbr = 0x0100,
  kRtcpTmmbn = 0x0200,
  kRtcpSrReq = 0x0400,
  kRtcpXrVoipMetric = 0x0800,
  kRtcpApp = 0x1000,
  kRtcpSli = 0x4000,
  kRtcpRpsi = 0x8000,
  kRtcpRemb = 0x10000,
  kRtcpTransmissionTimeOffset = 0x20000,
  kRtcpXrReceiverReferenceTime = 0x40000,
  kRtcpXrDlrrReportBlock = 0x80000,
  kRtcpTransportFeedback = 0x100000,
};

enum KeyFrameRequestMethod { kKeyFrameReqPliRtcp, kKeyFrameReqFirRtcp };

enum RtpRtcpPacketType { kPacketRtp = 0, kPacketKeepAlive = 1 };

enum NACKMethod { kNackOff = 0, kNackRtcp = 2 };

enum RetransmissionMode : uint8_t {
  kRetransmitOff = 0x0,
  kRetransmitFECPackets = 0x1,
  kRetransmitBaseLayer = 0x2,
  kRetransmitHigherLayers = 0x4,
  kRetransmitAllPackets = 0xFF
};

enum RtxMode {
  kRtxOff                 = 0x0,
  kRtxRetransmitted       = 0x1,  // Only send retransmissions over RTX.
  kRtxRedundantPayloads   = 0x2   // Preventively send redundant payloads
                                  // instead of padding.
};

const size_t kRtxHeaderSize = 2;

struct RTCPSenderInfo {
    uint32_t NTPseconds;
    uint32_t NTPfraction;
    uint32_t RTPtimeStamp;
    uint32_t sendPacketCount;
    uint32_t sendOctetCount;
};

struct RTCPReportBlock {
  RTCPReportBlock()
      : remoteSSRC(0), sourceSSRC(0), fractionLost(0), cumulativeLost(0),
        extendedHighSeqNum(0), jitter(0), lastSR(0),
        delaySinceLastSR(0) {}

  RTCPReportBlock(uint32_t remote_ssrc,
                  uint32_t source_ssrc,
                  uint8_t fraction_lost,
                  uint32_t cumulative_lost,
                  uint32_t extended_high_sequence_number,
                  uint32_t jitter,
                  uint32_t last_sender_report,
                  uint32_t delay_since_last_sender_report)
      : remoteSSRC(remote_ssrc),
        sourceSSRC(source_ssrc),
        fractionLost(fraction_lost),
        cumulativeLost(cumulative_lost),
        extendedHighSeqNum(extended_high_sequence_number),
        jitter(jitter),
        lastSR(last_sender_report),
        delaySinceLastSR(delay_since_last_sender_report) {}

  // Fields as described by RFC 3550 6.4.2.
  uint32_t remoteSSRC;  // SSRC of sender of this report.
  uint32_t sourceSSRC;  // SSRC of the RTP packet sender.
  uint8_t fractionLost;
  uint32_t cumulativeLost;  // 24 bits valid.
  uint32_t extendedHighSeqNum;
  uint32_t jitter;
  uint32_t lastSR;
  uint32_t delaySinceLastSR;
};

struct RtcpReceiveTimeInfo {
  // Fields as described by RFC 3611 4.5.
  uint32_t sourceSSRC;
  uint32_t lastRR;
  uint32_t delaySinceLastRR;
};

typedef std::list<RTCPReportBlock> ReportBlockList;

struct RtpState {
  RtpState()
      : sequence_number(0),
        start_timestamp(0),
        timestamp(0),
        capture_time_ms(-1),
        last_timestamp_time_ms(-1),
        media_has_been_sent(false) {}
  uint16_t sequence_number;
  uint32_t start_timestamp;
  uint32_t timestamp;
  int64_t capture_time_ms;
  int64_t last_timestamp_time_ms;
  bool media_has_been_sent;
};

class RtpData {
 public:
  virtual ~RtpData() {}

  virtual int32_t OnReceivedPayloadData(const uint8_t* payloadData,
                                        const size_t payloadSize,
                                        const WebRtcRTPHeader* rtpHeader) = 0;

  virtual bool OnRecoveredPacket(const uint8_t* packet,
                                 size_t packet_length) = 0;
};

class RtpFeedback {
 public:
  virtual ~RtpFeedback() {}

  // Receiving payload change or SSRC change. (return success!)
  /*
  *   channels    - number of channels in codec (1 = mono, 2 = stereo)
  */
  virtual int32_t OnInitializeDecoder(
      const int8_t payloadType,
      const char payloadName[RTP_PAYLOAD_NAME_SIZE],
      const int frequency,
      const size_t channels,
      const uint32_t rate) = 0;

  virtual void OnIncomingSSRCChanged(const uint32_t ssrc) = 0;

  virtual void OnIncomingCSRCChanged(const uint32_t CSRC, const bool added) = 0;
};

class RtpAudioFeedback {
 public:
  virtual void OnPlayTelephoneEvent(const uint8_t event,
                                    const uint16_t lengthMs,
                                    const uint8_t volume) = 0;

 protected:
  virtual ~RtpAudioFeedback() {}
};

class RtcpIntraFrameObserver {
 public:
  virtual void OnReceivedIntraFrameRequest(uint32_t ssrc) = 0;

  virtual void OnReceivedSLI(uint32_t ssrc,
                             uint8_t picture_id) = 0;

  virtual void OnReceivedRPSI(uint32_t ssrc,
                              uint64_t picture_id) = 0;

  virtual void OnLocalSsrcChanged(uint32_t old_ssrc, uint32_t new_ssrc) = 0;

  virtual ~RtcpIntraFrameObserver() {}
};

class RtcpBandwidthObserver {
 public:
  // REMB or TMMBR
  virtual void OnReceivedEstimatedBitrate(uint32_t bitrate) = 0;

  virtual void OnReceivedRtcpReceiverReport(
      const ReportBlockList& report_blocks,
      int64_t rtt,
      int64_t now_ms) = 0;

  virtual ~RtcpBandwidthObserver() {}
};

struct PacketInfo {
  PacketInfo(int64_t arrival_time_ms, uint16_t sequence_number)
      : PacketInfo(-1, arrival_time_ms, -1, sequence_number, 0, false) {}

  PacketInfo(int64_t arrival_time_ms,
             int64_t send_time_ms,
             uint16_t sequence_number,
             size_t payload_size,
             bool was_paced)
      : PacketInfo(-1,
                   arrival_time_ms,
                   send_time_ms,
                   sequence_number,
                   payload_size,
                   was_paced) {}

  PacketInfo(int64_t creation_time_ms,
             int64_t arrival_time_ms,
             int64_t send_time_ms,
             uint16_t sequence_number,
             size_t payload_size,
             bool was_paced)
      : creation_time_ms(creation_time_ms),
        arrival_time_ms(arrival_time_ms),
        send_time_ms(send_time_ms),
        sequence_number(sequence_number),
        payload_size(payload_size),
        was_paced(was_paced) {}

  // Time corresponding to when this object was created.
  int64_t creation_time_ms;
  // Time corresponding to when the packet was received. Timestamped with the
  // receiver's clock.
  int64_t arrival_time_ms;
  // Time corresponding to when the packet was sent, timestamped with the
  // sender's clock.
  int64_t send_time_ms;
  // Packet identifier, incremented with 1 for every packet generated by the
  // sender.
  uint16_t sequence_number;
  // Size of the packet excluding RTP headers.
  size_t payload_size;
  // True if the packet was paced out by the pacer.
  bool was_paced;
};

class TransportFeedbackObserver {
 public:
  TransportFeedbackObserver() {}
  virtual ~TransportFeedbackObserver() {}

  // Note: Transport-wide sequence number as sequence number. Arrival time
  // must be set to 0.
  virtual void AddPacket(uint16_t sequence_number,
                         size_t length,
                         bool was_paced) = 0;

  virtual void OnTransportFeedback(const rtcp::TransportFeedback& feedback) = 0;
};

class RtcpRttStats {
 public:
  virtual void OnRttUpdate(int64_t rtt) = 0;

  virtual int64_t LastProcessedRtt() const = 0;

  virtual ~RtcpRttStats() {}
};

// Null object version of RtpFeedback.
class NullRtpFeedback : public RtpFeedback {
 public:
  virtual ~NullRtpFeedback() {}

  int32_t OnInitializeDecoder(const int8_t payloadType,
                              const char payloadName[RTP_PAYLOAD_NAME_SIZE],
                              const int frequency,
                              const size_t channels,
                              const uint32_t rate) override {
    return 0;
  }

  void OnIncomingSSRCChanged(const uint32_t ssrc) override {}
  void OnIncomingCSRCChanged(const uint32_t CSRC, const bool added) override {}
};

// Null object version of RtpData.
class NullRtpData : public RtpData {
 public:
  virtual ~NullRtpData() {}

  int32_t OnReceivedPayloadData(const uint8_t* payloadData,
                                const size_t payloadSize,
                                const WebRtcRTPHeader* rtpHeader) override {
    return 0;
  }

  bool OnRecoveredPacket(const uint8_t* packet, size_t packet_length) override {
    return true;
  }
};

// Null object version of RtpAudioFeedback.
class NullRtpAudioFeedback : public RtpAudioFeedback {
 public:
  virtual ~NullRtpAudioFeedback() {}

  void OnPlayTelephoneEvent(const uint8_t event,
                            const uint16_t lengthMs,
                            const uint8_t volume) override {}
};

// Statistics about packet loss for a single directional connection. All values
// are totals since the connection initiated.
struct RtpPacketLossStats {
  // The number of packets lost in events where no adjacent packets were also
  // lost.
  uint64_t single_packet_loss_count;
  // The number of events in which more than one adjacent packet was lost.
  uint64_t multiple_packet_loss_event_count;
  // The number of packets lost in events where more than one adjacent packet
  // was lost.
  uint64_t multiple_packet_loss_packet_count;
};

class RtpPacketSender {
 public:
  RtpPacketSender() {}
  virtual ~RtpPacketSender() {}

  enum Priority {
    kHighPriority = 0,    // Pass through; will be sent immediately.
    kNormalPriority = 2,  // Put in back of the line.
    kLowPriority = 3,     // Put in back of the low priority line.
  };
  // Low priority packets are mixed with the normal priority packets
  // while we are paused.

  // Returns true if we send the packet now, else it will add the packet
  // information to the queue and call TimeToSendPacket when it's time to send.
  virtual void InsertPacket(Priority priority,
                            uint32_t ssrc,
                            uint16_t sequence_number,
                            int64_t capture_time_ms,
                            size_t bytes,
                            bool retransmission) = 0;
};

class TransportSequenceNumberAllocator {
 public:
  TransportSequenceNumberAllocator() {}
  virtual ~TransportSequenceNumberAllocator() {}

  virtual uint16_t AllocateSequenceNumber() = 0;
};

}  // namespace webrtc
#endif  // WEBRTC_MODULES_RTP_RTCP_INCLUDE_RTP_RTCP_DEFINES_H_

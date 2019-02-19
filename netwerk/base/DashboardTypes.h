/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_DashboardTypes_h_
#define mozilla_net_DashboardTypes_h_

#include "ipc/IPCMessageUtils.h"
#include "nsHttp.h"
#include "nsString.h"
#include "nsTArray.h"

namespace mozilla {
namespace net {

struct SocketInfo {
  nsCString host;
  uint64_t sent;
  uint64_t received;
  uint16_t port;
  bool active;
  bool tcp;
};

struct HalfOpenSockets {
  bool speculative;
};

struct DNSCacheEntries {
  nsCString hostname;
  nsTArray<nsCString> hostaddr;
  uint16_t family;
  int64_t expiration;
  nsCString netInterface;
  bool TRR;
};

struct HttpConnInfo {
  uint32_t ttl;
  uint32_t rtt;
  nsString protocolVersion;

  void SetHTTP1ProtocolVersion(HttpVersion pv);
  void SetHTTP2ProtocolVersion(SpdyVersion pv);
};

struct HttpRetParams {
  nsCString host;
  nsTArray<HttpConnInfo> active;
  nsTArray<HttpConnInfo> idle;
  nsTArray<HalfOpenSockets> halfOpens;
  uint32_t counter;
  uint16_t port;
  bool spdy;
  bool ssl;
};

}  // namespace net
}  // namespace mozilla

namespace IPC {

template <>
struct ParamTraits<mozilla::net::HalfOpenSockets> {
  typedef mozilla::net::HalfOpenSockets paramType;

  static void Write(Message* aMsg, const paramType& aParam) {
    WriteParam(aMsg, aParam.speculative);
  }

  static bool Read(const Message* aMsg, PickleIterator* aIter,
                   paramType* aResult) {
    return ReadParam(aMsg, aIter, &aResult->speculative);
  }
};

template <>
struct ParamTraits<mozilla::net::HttpConnInfo> {
  typedef mozilla::net::HttpConnInfo paramType;

  static void Write(Message* aMsg, const paramType& aParam) {
    WriteParam(aMsg, aParam.ttl);
    WriteParam(aMsg, aParam.rtt);
    WriteParam(aMsg, aParam.protocolVersion);
  }

  static bool Read(const Message* aMsg, PickleIterator* aIter,
                   paramType* aResult) {
    return ReadParam(aMsg, aIter, &aResult->ttl) &&
           ReadParam(aMsg, aIter, &aResult->rtt) &&
           ReadParam(aMsg, aIter, &aResult->protocolVersion);
  }
};

template <>
struct ParamTraits<mozilla::net::HttpRetParams> {
  typedef mozilla::net::HttpRetParams paramType;

  static void Write(Message* aMsg, const paramType& aParam) {
    WriteParam(aMsg, aParam.host);
    WriteParam(aMsg, aParam.active);
    WriteParam(aMsg, aParam.idle);
    WriteParam(aMsg, aParam.halfOpens);
    WriteParam(aMsg, aParam.counter);
    WriteParam(aMsg, aParam.port);
    WriteParam(aMsg, aParam.spdy);
    WriteParam(aMsg, aParam.ssl);
  }

  static bool Read(const Message* aMsg, PickleIterator* aIter,
                   paramType* aResult) {
    return ReadParam(aMsg, aIter, &aResult->host) &&
           ReadParam(aMsg, aIter, &aResult->active) &&
           ReadParam(aMsg, aIter, &aResult->idle) &&
           ReadParam(aMsg, aIter, &aResult->halfOpens) &&
           ReadParam(aMsg, aIter, &aResult->counter) &&
           ReadParam(aMsg, aIter, &aResult->port) &&
           ReadParam(aMsg, aIter, &aResult->spdy) &&
           ReadParam(aMsg, aIter, &aResult->ssl);
  }
};

}  // namespace IPC

#endif  // mozilla_net_DashboardTypes_h_

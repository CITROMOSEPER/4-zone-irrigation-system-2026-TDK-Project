#pragma once
#include <ThreeWire.h>
namespace host { extern uint32_t rtcGlitch; extern uint8_t rtcGlitchReads; extern uint32_t rtcBase; extern uint32_t rtcBaseMs; extern bool rtcValid; extern bool rtcBroken; extern uint32_t sysBase, sysBaseMs; }
class RtcDateTime {
public:
  void InitWithUnix32Time(uint32_t t) { _t = t; }
  uint32_t Unix32Time() const { return _t; }
  uint32_t _t = 0;
};
template <typename W> class RtcDS1302 {
public:
  explicit RtcDS1302(W&) {}
  void Begin() {}
  bool GetIsWriteProtected() { return false; }
  void SetIsWriteProtected(bool) {}
  bool GetIsRunning() { return true; }
  void SetIsRunning(bool) {}
  bool IsDateTimeValid() { return host::rtcValid; }
  RtcDateTime GetDateTime() {
    RtcDateTime d; d._t = host::rtcBase + (millis() - host::rtcBaseMs) / 1000;
    if (host::rtcGlitchReads) { host::rtcGlitchReads--; d._t += host::rtcGlitch; }   // a misread
    return d;
  }
  void SetDateTime(const RtcDateTime& d) { if (host::rtcBroken) return; host::rtcBase = d._t; host::rtcBaseMs = millis(); host::rtcValid = true; }
};

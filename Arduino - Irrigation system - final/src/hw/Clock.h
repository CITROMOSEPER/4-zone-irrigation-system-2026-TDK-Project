#pragma once
// DS1302 real-time clock, kept in UTC. Read once a second (it is bit-banged).
// When Wi-Fi is up, SNTP corrects it now and then (syncFromSntp); the phone's
// "Set clock" button stays the offline way to set it.
#include <Arduino.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>
#include <time.h>
#include "Config.h"
#include "PINS.h"
#include "util/Util.h"

class Clock {
public:
  Clock() : _wire(PIN_RTC_DAT, PIN_RTC_CLK, PIN_RTC_RST), _rtc(_wire) {}

  void begin() {
    _rtc.Begin();
    if (_rtc.GetIsWriteProtected()) _rtc.SetIsWriteProtected(false);
    if (!_rtc.GetIsRunning()) _rtc.SetIsRunning(true);
    refresh();
  }

  void update() {
    if (elapsed(_t, 1000)) refresh();
  }

  bool valid() const { return _valid; }
  uint32_t unixNow() const { return _valid ? _unix + (millis() - _t) / 1000 : 0; }   // 0 = unknown

  void setUnix(uint32_t t) {
    RtcDateTime dt;
    dt.InitWithUnix32Time(t);
    _rtc.SetDateTime(dt);
    refresh();
  }

  // Compare with the system clock that SNTP sets; write the RTC if it is off by more than 2 s.
  // Returns true when it wrote the RTC.
  bool syncFromSntp() {
    time_t sys = time(nullptr);
    if (sys < (time_t)RTC_MIN_VALID_UNIX) return false;   // SNTP has not answered yet
    int32_t diff = (int32_t)((uint32_t)sys - unixNow());
    if (_valid && diff >= -2 && diff <= 2) return false;
    setUnix((uint32_t)sys);
    return true;
  }

private:
  void refresh() {
    _t = millis();
    _unix = _rtc.GetDateTime().Unix32Time();
    _valid = _rtc.IsDateTimeValid() && _unix >= RTC_MIN_VALID_UNIX;
  }

  ThreeWire _wire;
  RtcDS1302<ThreeWire> _rtc;
  uint32_t _unix = 0, _t = 0;
  bool _valid = false;
};

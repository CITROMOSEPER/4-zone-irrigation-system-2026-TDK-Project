#pragma once
// DS1302 real-time clock, kept in UTC. Read once a second (it is bit-banged).
// When Wi-Fi is up, SNTP corrects it now and then (syncFromSntp); the phone's
// "Set clock" button stays the offline way to set it.
// The system clock (set by SNTP or the phone's "Set clock" since this boot) is the reference when it
// is set; the RTC carries the time across power cuts and runs things until then.
// The DS1302 is bit-banged and now and then reads garbage (seen: the right day and time in 2090).
// A read is taken only if it continues the running time (within 2 s); a different value has to be
// read RTC_CONFIRM_READS times in a row, consistently, before it is believed (someone set the RTC).
// One bad read used to move everything with it: a forecast stamped 2090, a weekday's DND window,
// calendar entries marked done until 2090, cost periods, log months.
// Fallback: when the RTC has no valid time (dead battery, a broken or unplugged module), the ESP32's
// own system clock is used. It keeps time only while powered.
#include <Arduino.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>
#include <time.h>
#include <sys/time.h>
#ifndef ARDUINO_ARCH_ESP32
namespace host { extern uint32_t sysBase, sysBaseMs; }
#endif
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
    refresh();                                         // two agreeing reads: a time from the start
  }

  void update() {
    if (elapsed(_t0, 1000)) refresh();
  }

  bool valid() const { return _valid || sysUnix() != 0; }
  bool rtcOk() const { return _valid; }        // false while running on the system clock (or with no time at all)
  uint32_t unixNow() const {                   // 0 = unknown
    uint32_t s = sysUnix();
    if (s) return s;
    return _valid ? _unix + (millis() - _t) / 1000 : 0;
  }
  uint32_t glitches() const { return _glitches; }   // RTC reads ignored since boot (for /health)

  void setUnix(uint32_t t) {
    setSystem(t);                              // also the fallback, in case the RTC does not take it
    RtcDateTime dt;
    dt.InitWithUnix32Time(t);
    _rtc.SetDateTime(dt);
    restart();
  }

  // Compare with the system clock that SNTP sets; write the RTC if it is off by more than 2 s.
  // Returns true when it wrote the RTC.
  bool syncFromSntp() {
    uint32_t sys = sysUnix();
    if (!sys) return false;                                // SNTP has not answered yet
    int32_t diff = (int32_t)(sys - unixNow());
    if (_valid && diff >= -2 && diff <= 2) return false;
    RtcDateTime dt;                                        // the RTC only (the system clock is already right)
    dt.InitWithUnix32Time(sys);
    _rtc.SetDateTime(dt);
    restart();
    return true;
  }

private:
  static uint32_t sysUnix() {
#ifdef ARDUINO_ARCH_ESP32
    time_t s = time(nullptr);
#else
    uint32_t s = host::sysBase ? host::sysBase + (millis() - host::sysBaseMs) / 1000 : 0;
#endif
    return s >= (time_t)RTC_MIN_VALID_UNIX ? (uint32_t)s : 0;
  }
  static void setSystem(uint32_t t) {
#ifdef ARDUINO_ARCH_ESP32
    timeval tv{(time_t)t, 0};
    settimeofday(&tv, nullptr);
#else
    host::sysBase = t; host::sysBaseMs = millis();
#endif
  }
  static uint32_t absDiff(uint32_t a, uint32_t b) { return a > b ? a - b : b - a; }

  // We wrote the RTC: read it afresh (two agreeing reads; the system clock covers the second meanwhile)
  void restart() {
    _valid = false;
    _cand = 0;
    refresh();
    refresh();
  }

  void refresh() {
    const uint32_t now = millis();
    _t0 = now;
    RtcDateTime dt = _rtc.GetDateTime();
    const uint32_t r = dt.Unix32Time();
    const bool ok = _rtc.IsDateTimeValid() && r >= RTC_MIN_VALID_UNIX && r < RTC_MAX_VALID_UNIX;
    if (!ok) {                                         // nothing usable: keep counting from the last good read a while
      if (_valid && ++_bad < RTC_CONFIRM_READS) { _glitches++; return; }
      _valid = false;
      _cand = 0;
      return;
    }
    _bad = 0;
    if (_valid && absDiff(r, _unix + (now - _t) / 1000) <= 2) {   // continues the running time
      _unix = r;
      _t = now;
      _cand = 0;
      return;
    }
    // a different time: the RTC was set, or a misread. Believe it once it repeats consistently.
    if (_cand && absDiff(r, _cand + (now - _candT) / 1000) <= 2) {
      if (++_candN >= (_valid ? RTC_CONFIRM_READS : 2)) {
        _unix = r;
        _t = now;
        _valid = true;
        _cand = 0;
      }
      return;
    }
    if (_valid) _glitches++;
    _cand = r;
    _candT = now;
    _candN = 1;
  }

  ThreeWire _wire;
  RtcDS1302<ThreeWire> _rtc;
  uint32_t _unix = 0, _t = 0, _t0 = 0;               // last accepted read and its millis(); last read attempt
  uint32_t _cand = 0, _candT = 0, _glitches = 0;      // a different time waiting for confirmation
  uint8_t _candN = 0, _bad = 0;
  bool _valid = false;
};

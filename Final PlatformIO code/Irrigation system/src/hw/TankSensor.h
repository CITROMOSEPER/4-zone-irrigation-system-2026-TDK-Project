#pragma once
// Tank level: HC-SR04 on top, looking down at the water.
//   - one ping every TANK_PERIOD_MS; the level is the MEDIAN of the last TANK_MEDIAN_N pings, recomputed
//     after every ping (two bad echoes cannot move it; with most of them missing the reading is invalid).
//     A real change shows after (N+1)/2 pings (600 ms for 5). rawCm() is the last ping as it came,
//     for calibrating ("Distance now")
//   - speed of sound corrected with the air temperature from the BME280 (0.17 % per C)
//   - the SHOWN level only moves when the median moved more than TANK_SHOW_STEP_CM, so the
//     percentage does not flicker between two values
//   - low below TANK_LOW_PCT, OK again only above TANK_OK_PCT (hysteresis), each after a few readings
// Fail-safe: starts LOW; 5 s without a valid reading (no echo, implausible distance) counts as LOW.
// Shorter dropouts keep the last state: the HC-SR04 misses an echo now and then (ripples, the tank wall).
// Calibration: distance to the water when the tank is empty (E) and full (F), from Settings.
#include <Arduino.h>
#include "Config.h"
#include "util/Util.h"

class TankSensor {
public:
  void begin(uint8_t trig, uint8_t echo) {
    _trig = trig;
    _echo = echo;
    pinMode(_trig, OUTPUT);
    digitalWrite(_trig, LOW);
    pinMode(_echo, INPUT);
    _low = true;
  }

  void setCal(float emptyCm, float fullCm) {
    if (emptyCm > fullCm + 1) { _empty = emptyCm; _full = fullCm; }
  }

  // Air temperature for the speed of sound; NAN = 20 C
  void setAirTemp(float c) { _airC = isnan(c) ? 20.0f : constrain(c, -20.0f, 60.0f); }

  void update() {
    if (!elapsed(_last, TANK_PERIOD_MS)) return;
    _last = millis();
    float d = measureCm();
    _raw = d;
    _ping[_pi] = d;
    _pi = (_pi + 1) % TANK_MEDIAN_N;
    if (_pn < TANK_MEDIAN_N) _pn++;

    float med = median();
    bool echoOk = !isnan(med) && med >= TANK_MIN_VALID_CM && med <= TANK_MAX_VALID_CM;
    if (echoOk) {
      _filt = med;
      if (isnan(_shown) || fabsf(_filt - _shown) > TANK_SHOW_STEP_CM) _shown = _filt;
    }
    if (!echoOk) {                                   // no reading: wait, then fail safe
      if (_miss < 255) _miss++;
      _good = 0;
      if (_miss >= TANK_MISS_TO_LOW) _low = true;
      return;
    }
    _miss = 0;
    bool good = pctOf(_filt) >= (_low ? TANK_OK_PCT : TANK_LOW_PCT);

    // Consecutive readings, so one bad median does not flip the state
    if (good) { if (_good < 255) _good++; _bad = 0; }
    else      { if (_bad < 255) _bad++;  _good = 0; }
    if (_bad >= TANK_BAD_TO_LOW) _low = true;
    else if (_good >= TANK_GOOD_TO_OK) _low = false;
  }

  bool low() const { return _low; }
  bool noEcho() const { return _miss >= TANK_MISS_TO_LOW; }   // low because the sensor gives no reading
  float distanceCm() const { return _shown; }                // shown distance (steps of > 2 mm), NAN if none
  float filteredCm() const { return _filt; }                 // the median, for the charts
  float rawCm() const { return _raw; }                       // the last ping, NAN if it had no echo
  int percent() const { return isnan(_shown) ? -1 : pctOf(_shown); }   // -1 = unknown

private:
  int pctOf(float d) const {
    float p = (_empty - d) / (_empty - _full) * 100.0f;
    return (int)roundf(constrain(p, 0.0f, 100.0f));
  }

  float median() const {
    float v[TANK_MEDIAN_N];
    uint8_t n = 0;
    for (uint8_t i = 0; i < _pn; i++) if (!isnan(_ping[i])) v[n++] = _ping[i];
    if (n == 0 || n * 2 <= _pn) return NAN;                   // mostly failed pings: no reading
    for (uint8_t i = 1; i < n; i++) {                          // insertion sort, n <= TANK_MEDIAN_N
      float x = v[i];
      uint8_t j = i;
      while (j > 0 && v[j - 1] > x) { v[j] = v[j - 1]; j--; }
      v[j] = x;
    }
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
  }

  float measureCm() {
    digitalWrite(_trig, LOW);
    delayMicroseconds(2);
    digitalWrite(_trig, HIGH);
    delayMicroseconds(10);
    digitalWrite(_trig, LOW);
    const float cmPerUs = (331.3f + 0.606f * _airC) / 10000.0f;   // speed of sound, cm per microsecond
    // Wait only as long as the farthest valid distance needs
    unsigned long us = pulseIn(_echo, HIGH, (uint32_t)(TANK_MAX_VALID_CM * 2.0f / cmPerUs) + 1000);
    if (us == 0) return NAN;
    return us * cmPerUs / 2.0f;
  }

  uint8_t _trig = 0, _echo = 0, _good = 0, _bad = 0, _miss = 0, _pi = 0, _pn = 0;
  uint32_t _last = 0;
  float _ping[TANK_MEDIAN_N];                                 // only the first _pn are used
  float _filt = NAN, _shown = NAN, _raw = NAN, _airC = 20.0f;
  float _empty = DEFAULT_TANK_EMPTY_CM, _full = DEFAULT_TANK_FULL_CM;
  bool _low = true;
};

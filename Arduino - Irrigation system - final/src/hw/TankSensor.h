#pragma once
// Tank level: HC-SR04 on top, looking down at the water.
// Fail-safe: starts LOW; no echo, an implausible reading or a level under TANK_LOW_PCT counts as LOW.
// Calibration: distance to the water when the tank is empty (E) and full (F), from Settings.
#include <Arduino.h>
#include "Config.h"
#include "util/Util.h"

class TankSensor {
public:
  static constexpr float CM_PER_US = 0.0343f;   // speed of sound at about 20 C

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

  void update() {
    if (!elapsed(_last, TANK_PERIOD_MS)) return;
    _last = millis();
    _dist = measureCm();

    bool echoOk = !isnan(_dist) && _dist >= TANK_MIN_VALID_CM && _dist <= TANK_MAX_VALID_CM;
    bool good = echoOk && pctOf(_dist) >= TANK_LOW_PCT;

    // Consecutive readings, so one bad echo does not flip the state
    if (good) { if (_good < 255) _good++; _bad = 0; }
    else      { if (_bad < 255) _bad++;  _good = 0; }
    if (_bad >= TANK_BAD_TO_LOW) _low = true;
    else if (_good >= TANK_GOOD_TO_OK) _low = false;
    if (echoOk) _okDist = _dist;
  }

  bool low() const { return _low; }
  float distanceCm() const { return _okDist; }               // last valid distance, NAN if none
  int percent() const { return isnan(_okDist) ? -1 : pctOf(_okDist); }   // -1 = unknown

private:
  int pctOf(float d) const {
    float p = (_empty - d) / (_empty - _full) * 100.0f;
    return (int)roundf(constrain(p, 0.0f, 100.0f));
  }

  float measureCm() {
    digitalWrite(_trig, LOW);
    delayMicroseconds(2);
    digitalWrite(_trig, HIGH);
    delayMicroseconds(10);
    digitalWrite(_trig, LOW);
    // Wait only as long as the farthest valid distance needs
    unsigned long us = pulseIn(_echo, HIGH, (uint32_t)(TANK_MAX_VALID_CM * 2.0f / CM_PER_US) + 1000);
    if (us == 0) return NAN;
    return us * CM_PER_US / 2.0f;
  }

  uint8_t _trig = 0, _echo = 0, _good = 0, _bad = 0;
  uint32_t _last = 0;
  float _dist = NAN, _okDist = NAN;
  float _empty = DEFAULT_TANK_EMPTY_CM, _full = DEFAULT_TANK_FULL_CM;
  bool _low = true;
};

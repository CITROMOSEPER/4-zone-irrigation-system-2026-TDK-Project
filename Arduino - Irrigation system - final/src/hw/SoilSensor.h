#pragma once
// HW-390 capacitive soil sensor (analog, ADC1). Higher raw = drier.
// The calibration (dry/wet raw) comes from Settings and can change at runtime.
#include <Arduino.h>
#include "Config.h"

class SoilSensor {
public:
  void begin(uint8_t pin) {
    _pin = pin;
    pinMode(_pin, INPUT);
    analogSetPinAttenuation(_pin, ADC_11db);
  }

  void setCal(int dryRaw, int wetRaw) {
    if (dryRaw > wetRaw) { _dry = dryRaw; _wet = wetRaw; }
  }

  // Median of three 8-sample averages: one spike cannot move the reading
  void update() {
    int a = avg8(), b = avg8(), c = avg8();
    _raw = max(min(a, b), min(max(a, b), c));
    _fault = _raw < SOIL_RAW_MIN_OK || _raw > SOIL_RAW_MAX_OK;
  }

  int raw() const { return _raw; }
  bool fault() const { return _fault; }

  // 0..100 % between the dry and wet calibration points, NAN on fault
  float percent() const {
    if (_fault) return NAN;
    float p = 100.0f * (float)(_dry - _raw) / (float)(_dry - _wet);
    return constrain(p, 0.0f, 100.0f);
  }

private:
  int avg8() {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < 8; i++) {
      sum += analogRead(_pin);
      delayMicroseconds(100);
    }
    return (int)(sum / 8);
  }

  uint8_t _pin = 0;
  int _dry = DEFAULT_SOIL_DRY, _wet = DEFAULT_SOIL_WET, _raw = 0;
  bool _fault = true;
};

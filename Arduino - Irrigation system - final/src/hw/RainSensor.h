#pragma once
// SA-28 rain sensor (digital, wet = LOW). Its VCC comes from a GPIO and is switched on only
// while sampling, to slow electrode corrosion. Two samples in a row must agree to change state.
#include <Arduino.h>
#include "Config.h"
#include "util/Util.h"

class RainSensor {
public:
  void begin(uint8_t outPin, uint8_t vccPin) {
    _pin = outPin;
    _vcc = vccPin;
    pinMode(_vcc, OUTPUT);
    digitalWrite(_vcc, LOW);
    pinMode(_pin, INPUT_PULLUP);       // idle = HIGH = dry, so an unplugged sensor also reads dry
    _tSample = millis() - RAIN_SAMPLE_PERIOD_MS;   // first sample at once
  }

  void update() {
    if (!_powered) {
      if (!elapsed(_tSample, RAIN_SAMPLE_PERIOD_MS)) return;
      digitalWrite(_vcc, HIGH);
      _powered = true;
      _tOn = millis();
      return;
    }
    if (!elapsed(_tOn, RAIN_POWERUP_MS)) return;   // let the module settle
    bool w = digitalRead(_pin) == LOW;
    digitalWrite(_vcc, LOW);
    _powered = false;
    _tSample = millis();

    if (w == _last) _state = w;                    // two agreeing samples
    _last = w;
  }

  bool wet() const { return _state; }

private:
  uint8_t _pin = 0, _vcc = 0;
  bool _powered = false, _last = false, _state = false;
  uint32_t _tSample = 0, _tOn = 0;
};

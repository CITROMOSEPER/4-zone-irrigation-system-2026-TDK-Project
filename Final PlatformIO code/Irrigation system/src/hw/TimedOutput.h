#pragma once
// Relay output with its own on-time cap: a backstop under WaterController.
// If the cap trips, the output is forced off and stays off (latched) until clearFault().
#include <Arduino.h>
#include "Config.h"
#include "util/Util.h"

class TimedOutput {
public:
  void begin(uint8_t pin, uint32_t hardCapMs) {
    _pin = pin;
    _cap = hardCapMs;
    digitalWrite(_pin, offLevel());   // set the OFF level before the pin becomes an output
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, offLevel());
    _on = false;
    _fault = false;
  }

  // False (and nothing happens) while latched in fault
  bool on() {
    if (_fault) return false;
    if (!_on) {
      _since = millis();
      _on = true;
      digitalWrite(_pin, onLevel());
    }
    return true;
  }

  void off() {
    _on = false;
    digitalWrite(_pin, offLevel());
  }

  void update() {
    if (_on && elapsed(_since, _cap)) {
      off();
      _fault = true;
    }
  }

  bool isOn() const { return _on; }
  bool fault() const { return _fault; }
  void clearFault() { _fault = false; }

private:
  uint8_t onLevel() const  { return RELAY_ACTIVE_LOW ? LOW : HIGH; }
  uint8_t offLevel() const { return RELAY_ACTIVE_LOW ? HIGH : LOW; }

  uint8_t _pin = 0;
  uint32_t _cap = 0, _since = 0;
  bool _on = false, _fault = false;
};

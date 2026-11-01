#pragma once
// Debounced push button.
//   PRESS  once the press is confirmed (immediate feedback, stop watering)
//   SHORT  on release before BUTTON_LONG_MS
//   LONG   once, while still held, at BUTTON_LONG_MS (no SHORT follows)
// The PCB has a 1k pull-down and the button goes to 3.3 V, so pressed = HIGH.
#include <Arduino.h>
#include "Config.h"
#include "util/Util.h"

enum class ButtonEvent : uint8_t { NONE, PRESS, SHORT, LONG };

class Button {
public:
  void begin(uint8_t pin) {
    _pin = pin;
    pinMode(_pin, INPUT);
  }

  bool pressed() const { return digitalRead(_pin) == HIGH; }   // raw, not debounced

  ButtonEvent update() {
    bool down = pressed();
    if (down != _raw) {                                   // level changed: restart the debounce timer
      _raw = down;
      _t = millis();
      return ButtonEvent::NONE;
    }
    if (!elapsed(_t, BUTTON_DEBOUNCE_MS)) return ButtonEvent::NONE;

    if (down && !_down) {                                 // press confirmed
      _down = true;
      _longSent = false;
      _pressT = millis();
      return ButtonEvent::PRESS;
    }
    if (down && !_longSent && elapsed(_pressT, BUTTON_LONG_MS)) {
      _longSent = true;
      return ButtonEvent::LONG;
    }
    if (!down && _down) {                                 // released
      _down = false;
      if (!_longSent) return ButtonEvent::SHORT;
    }
    return ButtonEvent::NONE;
  }

private:
  uint8_t _pin = 0;
  bool _raw = false, _down = false, _longSent = false;
  uint32_t _t = 0, _pressT = 0;
};

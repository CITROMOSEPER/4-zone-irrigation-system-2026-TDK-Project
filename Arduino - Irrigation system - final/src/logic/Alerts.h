#pragma once
// Writes an ALERT row to the log when a problem starts or clears (after a short debounce, so a
// flicker does not fill the log): TANK, ENV, FAULT, RTC, WIFI, SOIL (per enabled zone).
#include <Arduino.h>
#include "Config.h"

class Alerts {
public:
  void update();                       // once a second is enough

private:
  struct Flag {
    const char* code;                  // TANK_LOW / ENV / ... (the cleared code adds _OK)
    char level;                        // E error, W warning
    uint8_t zone;                      // 0 = system
    uint16_t debounceS;
    bool raw, logged;
    uint32_t since;
  };
  void check(Flag& f, bool now);

  static constexpr uint8_t N = 5 + ZONES;
  Flag _f[N] = {
    {"TANK", 'W', 0, 5, false, false, 0},
    {"ENV", 'W', 0, 30, false, false, 0},
    {"FAULT", 'E', 0, 0, false, false, 0},
    {"RTC", 'E', 0, 5, false, false, 0},
    {"WIFI", 'W', 0, 60, false, false, 0},
  };
  uint32_t _tick = 0;
  bool _init = false;
};

#pragma once
// Writes an ALERT row to the log when a problem starts or clears (after a short debounce, so a
// flicker does not fill the log): TANK, ENV, FAULT, RTC, WIFI (lost or unusable), SOIL (per enabled zone).
// Also the fallbacks, so the log says what the controller is running on:
//   WX         no usable forecast (rain skips off, no forecast ET0)
//   ENV_WEB    the air temperature comes from the forecast (sensor faulty)
//   CLOCK_SYS  the time comes from the ESP32's own clock (RTC invalid; lost at the next power cut)
//   NORISE     a closed-loop zone saw no rise after RISE_MISSES_MAX runs: it waters by the calendar
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

  static constexpr uint8_t SYS = 8;
  static constexpr uint8_t N = SYS + 2 * ZONES;     // then SOIL and NORISE per zone
  Flag _f[N] = {
    {"TANK", 'W', 0, 5, false, false, 0},
    {"ENV", 'W', 0, 30, false, false, 0},
    {"FAULT", 'E', 0, 0, false, false, 0},
    {"RTC", 'E', 0, 5, false, false, 0},
    {"WIFI", 'W', 0, WIFI_ALERT_S, false, false, 0},
    {"WX", 'W', 0, 600, false, false, 0},          // 10 min: the first fetch after boot takes a while
    {"ENV_WEB", 'I', 0, 30, false, false, 0},
    {"CLOCK_SYS", 'I', 0, 5, false, false, 0},
  };
  uint32_t _tick = 0;
  bool _init = false;
};

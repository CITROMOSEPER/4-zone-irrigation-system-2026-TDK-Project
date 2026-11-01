#pragma once
// How much water a zone gets.
//
// Deficit (Water now "Fill the deficit", the button, closed and open loop):
//   available mm = (fc - wp)/100 x root(cm) x 10
//   deficit mm   = (hi - soil)/100 x available
//   seconds      = deficit / (eff/100) / rate(mm/h) x 3600, rounded to 10 s, at most MAX_RUN_S
//
// Modes, when a calendar entry fires:
//   Schedule      the entry's length
//   Closed loop   soil sensor below lo -> deficit dose; otherwise skip.
//                 Sensor fault, or RISE_MISSES_MAX runs in a row without a rise -> Schedule fallback
//   Open loop     the same with the water-balance estimate
#include <Arduino.h>
#include "Config.h"

struct Deficit {
  uint32_t sec;
  float mm;
};

class Dose {
public:
  // The soil value the zone's mode uses: sensor %, or the open-loop estimate. False if none.
  bool soil(uint8_t zone, float& pct) const;
  bool deficit(uint8_t zone, Deficit& out) const;

  // Dose for a calendar entry; 0 = skip, with 'why' = the log reason
  uint32_t forEntry(uint8_t zone, uint32_t entrySec, const char*& why) const;

  // Closed loop falls back to Schedule (UI shows the sensor warning)
  bool fallback(uint8_t zone) const;

  // Closed-loop rise check: called when a run ends; evaluated RISE_CHECK_DELAY_MS later
  void runEnded(uint8_t zone, float soil0);
  void update();

private:
  uint8_t _misses[ZONES] = {};
  float _soil0[ZONES] = {};
  uint32_t _checkAt[ZONES] = {};
  bool _pending[ZONES] = {};
};

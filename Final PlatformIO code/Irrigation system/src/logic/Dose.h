#pragma once
// How much water a zone gets.
//
// Deficit (Water now "Fill the deficit", the button, closed and open loop):
//   available mm = (fc - wp)/100 x root(cm) x 10
//   deficit mm   = (hi - soil)/100 x available
//   seconds      = deficit / (eff/100) / rate(mm/h) x 3600, rounded to 1 s, at most MAX_RUN_S
//   rate         = flow (L/min) x 60 / area (m2)
//
// Cycle and soak (automatic and deficit runs; not a fixed-length Water now): when the rate is above
// the soil's infiltration, the dose is split into bursts (burstS) with pauses (soakS) between them.
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

  // The expected length of the automatic watering at 'at': today's soil moved by the forecast ET (x Kc)
  // and rain (up to 48 h ahead) until then. Seconds; 0 = probably moist enough; -1 = unknown. Only a
  // forecast for the zone card: forEntry() decides with the real reading when it fires.
  int32_t estimate(uint8_t zone, uint32_t at, uint32_t entrySec) const;

  // Dose for a calendar entry; 0 = skip, with 'why' = the log reason
  uint32_t forEntry(uint8_t zone, uint32_t entrySec, const char*& why) const;

  uint32_t burstS(uint8_t zone) const;                 // longest burst before runoff, 0 = no split
  uint32_t soakS(uint8_t zone, uint32_t burstSec) const;   // pause after a burst

  // Closed loop falls back to Schedule (UI shows the sensor warning)
  bool fallback(uint8_t zone) const;
  bool noRise(uint8_t zone) const { return zone < ZONES && _misses[zone] >= RISE_MISSES_MAX; }

  // Closed-loop rise check: called when a run ends; evaluated RISE_CHECK_DELAY_MS later
  void runEnded(uint8_t zone, float soil0);
  void update();

private:
  uint8_t _misses[ZONES] = {};
  float _soil0[ZONES] = {};
  uint32_t _checkAt[ZONES] = {};
  bool _pending[ZONES] = {};
};

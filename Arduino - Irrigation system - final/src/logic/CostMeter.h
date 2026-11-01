#pragma once
// Cost: the controller only counts PUMP SECONDS per period (day, week from Monday, month, season,
// year, local time). Litres, kWh and money are worked out from the current settings whenever they
// are shown or logged. A season starts when All off (winter mode) is switched off.
// When a period in 'clog' ends, a COST row freezes its total at that moment's price.
#include <Arduino.h>

enum CostPeriod : uint8_t { CP_DAY, CP_WEEK, CP_MONTH, CP_SEASON, CP_YEAR, CP_N };

struct CostCounter { uint32_t sec; uint32_t f; };   // f = period start (unix), 0 = no season yet

class CostMeter {
public:
  void begin();
  void update();                                    // period rollover, once a second

  void add(uint32_t pumpSeconds);
  void reset();                                     // all counters to 0 (log rows stay)
  void seasonStart();

  const CostCounter& counter(uint8_t p) const { return _s.c[p]; }

  static float litres(uint32_t sec);
  static float kwh(uint32_t sec);
  static float money(uint32_t sec);

private:
  struct Stored { CostCounter c[CP_N]; };
  void save();
  Stored _s{};
  uint32_t _tick = 0;
};

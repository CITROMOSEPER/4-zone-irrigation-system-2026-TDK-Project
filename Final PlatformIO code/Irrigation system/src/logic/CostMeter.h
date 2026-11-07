#pragma once
// Cost: per period (day, week from Monday, month, year, local time) the controller counts pump
// seconds and litres (each zone's own flow). Money is worked out from the current price whenever it
// is shown or logged: water price x litres, or electricity price x pump power x seconds.
// When a period in 'clog' ends, a COST row freezes its total at that moment's price.
#include <Arduino.h>

enum CostPeriod : uint8_t { CP_DAY, CP_WEEK, CP_MONTH, CP_YEAR, CP_N };

struct CostCounter { uint32_t sec; float litres; uint32_t f; };   // f = period start (unix)

class CostMeter {
public:
  void begin();
  void update();                                    // period rollover, once a second

  void add(uint8_t zone, uint32_t pumpSeconds);
  void reset();                                     // all counters to 0 (log rows stay)
  void repairFuture(uint32_t now);                  // a period dated after now (a clock error): back to now's, counts kept

  const CostCounter& counter(uint8_t p) const { return _s.c[p]; }

  static float kwh(uint32_t sec);
  static float money(const CostCounter& c);

private:
  struct Stored { CostCounter c[CP_N]; };
  void save();
  Stored _s{};
  uint32_t _tick = 0;
};

#include "logic/CostMeter.h"
#include "System.h"
#include "util/Nvs.h"
#include "util/TimeUtil.h"

namespace {
// The periods that roll over by the calendar, with their log letter and clog bit
struct Roll { CostPeriod p; TimeUtil::Period tp; char letter; uint8_t bit; };
const Roll ROLLS[] = {
  {CP_DAY,   TimeUtil::Period::DAY,   'D', 1},
  {CP_WEEK,  TimeUtil::Period::WEEK,  'W', 2},
  {CP_MONTH, TimeUtil::Period::MONTH, 'M', 4},
  {CP_YEAR,  TimeUtil::Period::YEAR,  'Y', 16},
};
}  // namespace

void CostMeter::begin() {
  if (!Nvs::load("rt", "cost", _s)) memset(&_s, 0, sizeof(_s));
}

void CostMeter::save() { Nvs::save("rt", "cost", _s); }

float CostMeter::litres(uint32_t sec) { return sec * sys.cfg.g.flow / 60.0f; }
float CostMeter::kwh(uint32_t sec) { return sys.cfg.g.cpp * (float)sec / 3.6e6f; }
float CostMeter::money(uint32_t sec) {
  switch (sys.cfg.g.cmode) {
    case 1:  return litres(sec) / 1000.0f * sys.cfg.g.cpw;
    case 2:  return kwh(sec) * sys.cfg.g.cpe;
    default: return 0;
  }
}

void CostMeter::update() {
  if (!elapsed(_tick, 1000)) return;
  _tick = millis();
  if (!sys.clock.valid()) return;
  const uint32_t now = sys.clock.unixNow();
  bool ch = false;
  for (const Roll& r : ROLLS) {
    CostCounter& c = _s.c[r.p];
    uint32_t start = TimeUtil::periodStart(r.tp, now);
    if (c.f >= start) continue;                     // still the same period
    if (c.f && (sys.cfg.g.clog & r.bit) && c.sec > 0) {
      sys.log.cost(r.letter, c.f, c.sec, litres(c.sec), money(c.sec));
    }
    c = CostCounter{0, start};
    ch = true;
  }
  if (ch) save();
}

void CostMeter::add(uint32_t sec) {
  if (!sec) return;
  for (uint8_t p = 0; p < CP_N; p++) {
    if (p == CP_SEASON && !_s.c[p].f) continue;     // no season yet
    _s.c[p].sec += sec;
  }
  save();
}

void CostMeter::reset() {
  const uint32_t now = sys.clock.unixNow();
  for (uint8_t p = 0; p < CP_N; p++) {
    _s.c[p].sec = 0;
    if (p != CP_SEASON || _s.c[p].f) _s.c[p].f = now;
  }
  save();
}

void CostMeter::seasonStart() {
  _s.c[CP_SEASON] = CostCounter{0, sys.clock.unixNow()};
  save();
}

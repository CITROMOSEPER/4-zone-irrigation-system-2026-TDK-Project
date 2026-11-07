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

// "cost3" is the key since 1.2.6 (the 1.0 "cost" converter was removed in 1.3.2). Keep the name:
// renaming it would zero everybody's counters.
void CostMeter::begin() {
  if (Nvs::load("rt", "cost3", _s)) return;
  memset(&_s, 0, sizeof(_s));
  save();
}

void CostMeter::save() { Nvs::save("rt", "cost3", _s); }

float CostMeter::kwh(uint32_t sec) { return sys.cfg.g.cpp * (float)sec / 3.6e6f; }
float CostMeter::money(const CostCounter& c) {
  switch (sys.cfg.g.cmode) {
    case 1:  return c.litres / 1000.0f * sys.cfg.g.cpw;
    case 2:  return kwh(c.sec) * sys.cfg.g.cpe;
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
      sys.log.cost(r.letter, c.f, c.sec, c.litres, money(c));
    }
    c = CostCounter{0, 0, start};
    ch = true;
  }
  if (ch) save();
}

void CostMeter::repairFuture(uint32_t now) {
  bool ch = false;
  for (const Roll& r : ROLLS) {
    CostCounter& c = _s.c[r.p];
    if (c.f <= now + 86400) continue;
    c.f = TimeUtil::periodStart(r.tp, now);
    ch = true;
  }
  if (ch) { save(); Serial.println("COST: a period dated in the future was reset to now's"); }
}

void CostMeter::add(uint8_t zone, uint32_t sec) {
  if (!sec || zone >= ZONES) return;
  float l = Settings::litres(sys.cfg.z[zone], sec);
  for (auto& c : _s.c) { c.sec += sec; c.litres += l; }
  save();
}

void CostMeter::reset() {
  const uint32_t now = sys.clock.unixNow();
  for (auto& c : _s.c) c = CostCounter{0, 0, now};
  save();
}

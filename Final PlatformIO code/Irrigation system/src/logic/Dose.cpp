#include "logic/Dose.h"
#include "System.h"

bool Dose::soil(uint8_t z, float& pct) const {
  if (z >= ZONES) return false;
  if (sys.cfg.z[z].m == (uint8_t)ZoneMode::OPEN_LOOP) {
    pct = sys.wb.estimate(z);
  } else {
    if (sys.soil[z].fault()) return false;
    pct = sys.soil[z].percent();
  }
  return !isnan(pct);
}

bool Dose::deficit(uint8_t z, Deficit& out) const {
  float s;
  if (!soil(z, s)) return false;
  const ZoneCfg& c = sys.cfg.z[z];
  float mm = max(0.0f, (c.plant.hi - s) / 100.0f * Settings::availableMm(c));
  out.sec = min(Settings::secondsFor(c, mm), MAX_RUN_S);
  out.mm = roundf(mm * 10) / 10;
  return true;
}

// The same sum as forEntry() + deficit(), on the soil moved forward to 'at'
int32_t Dose::estimate(uint8_t z, uint32_t at, uint32_t entrySec) const {
  if (z >= ZONES) return -1;
  const ZoneCfg& c = sys.cfg.z[z];
  const uint32_t fixed = entrySec ? entrySec : SCHEDULE_DEFAULT_S;
  if (c.m == (uint8_t)ZoneMode::SCHEDULE) return fixed;
  if (c.m == (uint8_t)ZoneMode::CLOSED_LOOP && fallback(z)) return fixed;
  float s;
  if (!soil(z, s)) return -1;
  const uint32_t now = sys.clock.unixNow();
  float et0, rain;
  if (!now || !sys.wx.fresh() || !sys.wx.between(now, at, et0, rain)) return -1;
  const float avail = Settings::availableMm(c);
  if (avail <= 0) return -1;
  const float p = clampv(s + Settings::mmToPct(c, Settings::weatherMm(c, et0, rain)), 0.0f, 100.0f);
  if (p >= c.plant.lo) return 0;                             // moist enough then
  return (int32_t)clampv(Settings::secondsFor(c, (c.plant.hi - p) / 100.0f * avail), MIN_RUN_S, MAX_RUN_S);
}

bool Dose::fallback(uint8_t z) const {
  return z < ZONES && (sys.soil[z].fault() || _misses[z] >= RISE_MISSES_MAX);
}

uint32_t Dose::forEntry(uint8_t z, uint32_t entrySec, const char*& why) const {
  why = "";
  const ZoneCfg& c = sys.cfg.z[z];
  uint32_t fixed = entrySec ? entrySec : SCHEDULE_DEFAULT_S;
  Deficit d;

  switch ((ZoneMode)c.m) {
    case ZoneMode::CLOSED_LOOP:
      if (fallback(z)) return fixed;
      [[fallthrough]];
    case ZoneMode::OPEN_LOOP: {
      float s;
      if (!soil(z, s)) return fixed;
      if (s >= c.plant.lo) { why = "WET"; return 0; }   // moist enough
      if (!deficit(z, d) || d.sec == 0) { why = "WET"; return 0; }
      return max(d.sec, MIN_RUN_S);
    }
    default:
      return fixed;
  }
}

void Dose::runEnded(uint8_t z, float soil0) {
  if (z >= ZONES || isnan(soil0) || sys.cfg.z[z].m != (uint8_t)ZoneMode::CLOSED_LOOP) return;
  _soil0[z] = soil0;
  _checkAt[z] = millis();
  _pending[z] = true;
}

void Dose::update() {
  for (uint8_t z = 0; z < ZONES; z++) {
    if (!_pending[z] || (uint32_t)(millis() - _checkAt[z]) < RISE_CHECK_DELAY_MS) continue;
    _pending[z] = false;
    if (sys.soil[z].fault()) continue;
    float rise = sys.soil[z].percent() - _soil0[z];
    if (rise >= RISE_MIN_PCT) _misses[z] = 0;
    else if (_misses[z] < 255) _misses[z]++;
  }
}

// Cycle and soak. Water lands at the sprinkler's rate but soaks in only at the soil's infiltration
// rate; the difference stands on the surface. A burst ends before SOAK_STORE_MM has built up, and the
// pause lets that burst's water soak in. 0 = no split (the soil takes the water as fast as it comes).
uint32_t Dose::burstS(uint8_t z) const {
  if (z >= ZONES) return 0;
  const ZoneCfg& c = sys.cfg.z[z];
  float excess = Settings::rate(c) - c.soil.inf;              // mm/h that cannot soak in
  if (excess <= 0) return 0;
  uint32_t s = (uint32_t)(SOAK_STORE_MM / excess * 3600.0f);
  return max(s, BURST_MIN_S);
}

uint32_t Dose::soakS(uint8_t z, uint32_t burstSec) const {
  if (z >= ZONES) return SOAK_MIN_S;
  const ZoneCfg& c = sys.cfg.z[z];
  float mm = Settings::rate(c) * burstSec / 3600.0f;
  uint32_t s = (uint32_t)(mm / c.soil.inf * 3600.0f);
  return clampv(s, SOAK_MIN_S, SOAK_MAX_S);
}

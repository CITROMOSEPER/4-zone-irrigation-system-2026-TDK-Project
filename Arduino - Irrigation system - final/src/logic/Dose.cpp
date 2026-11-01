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
  float avail = Settings::availableMm(c);
  float mm = max(0.0f, (c.plant.hi - s) / 100.0f * avail);
  float gross = mm / (c.spr.eff / 100.0f);
  uint32_t sec = (uint32_t)lroundf(gross / c.spr.rate * 3600.0f / 10.0f) * 10;
  out.sec = min(sec, MAX_RUN_S);
  out.mm = roundf(mm * 10) / 10;
  return true;
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

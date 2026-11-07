#include "logic/WaterBalance.h"
#include "System.h"
#include "util/Nvs.h"
#include "util/TimeUtil.h"

void WaterBalance::begin() {
  if (!Nvs::load("rt", "wb", _s)) {
    for (uint8_t z = 0; z < ZONES; z++) _s.pct[z] = NAN;
    _s.lastEt0 = NAN;
    _s.ymd = 0;
  }
  for (uint8_t z = 0; z < ZONES; z++) if (isnan(_s.pct[z])) reset(z);
}

void WaterBalance::save() { Nvs::save("rt", "wb", _s); }

void WaterBalance::reset(uint8_t z) {
  if (z >= ZONES) return;
  _s.pct[z] = sys.cfg.z[z].plant.hi;     // unknown: assume the soil was filled to the target
  save();
}

void WaterBalance::dailyStep(uint32_t ymd, float tMin, float tMax) {
  if (_s.ymd == ymd) return;             // already done for this day
  WxDay w;
  bool have = sys.wx.day(ymd, w);
  float et0 = have ? w.et0 : NAN;
  if (isnan(et0)) {
    if (have && !isnan(w.tmin)) { tMin = w.tmin; tMax = w.tmax; }
    et0 = Weather::hargreavesEt0(tMin, tMax, sys.cfg.g.lat, TimeUtil::dayOfYear(ymd));
  }
  float rain = have ? w.rain : 0;
  if (isnan(et0)) et0 = 0;               // no forecast and no BME280: hold the estimate

  for (uint8_t z = 0; z < ZONES; z++) {
    const ZoneCfg& c = sys.cfg.z[z];
    if (Settings::availableMm(c) <= 0) continue;
    _s.pct[z] = clampv(_s.pct[z] + Settings::mmToPct(c, Settings::weatherMm(c, et0, rain)), 0.0f, 100.0f);
  }
  _s.lastEt0 = et0;
  _s.ymd = ymd;
  save();
}

void WaterBalance::addApplied(uint8_t z, uint32_t sec) {
  if (z >= ZONES || !sec) return;
  const ZoneCfg& c = sys.cfg.z[z];
  if (Settings::availableMm(c) <= 0) return;
  _s.pct[z] = clampv(_s.pct[z] + Settings::mmToPct(c, Settings::appliedMm(c, sec)), 0.0f, 100.0f);
  save();
}

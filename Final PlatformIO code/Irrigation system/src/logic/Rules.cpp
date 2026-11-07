#include "logic/Rules.h"
#include "System.h"

namespace Rules {

bool belowFrost() {
  float t = sys.airTempC();                      // the sensor, or the forecast while it is faulty
  return !isnan(t) && t < sys.cfg.g.frost;
}

bool belowCold(uint8_t z) {
  float t = sys.airTempC();
  return z < ZONES && !isnan(t) && t < sys.cfg.z[z].cold;
}

GBlock globalBlock() {
  if (sys.cfg.g.off) return GBlock::ALL_OFF;
  if (belowFrost()) return GBlock::FROST;
  if (sys.tank.low()) return GBlock::TANK_LOW;
  return GBlock::NONE;
}

const char* blockName(GBlock b) {
  switch (b) {
    case GBlock::ALL_OFF:  return "ALL_OFF";
    case GBlock::FROST:    return "FROST";
    case GBlock::TANK_LOW: return "TANK_LOW";
    default:               return "";
  }
}

bool rainSensorWet() { return sys.rain.wet(); }

bool forecastRain(uint32_t at) {
  const float lim = sys.cfg.g.rainmm;
  const uint32_t now = sys.clock.unixNow();
  if (!at) at = now;
  if (lim <= 0 || !now || !sys.wx.fresh() || at > now + WX_RAIN_TRUST_S) return false;
  float mm[WX_MODELS];
  bool have[WX_MODELS];
  sys.wx.rainWindow(at, 24, mm, have);
  uint8_t n = 0, wet = 0;
  for (uint8_t m = 0; m < WX_MODELS; m++) if (have[m]) { n++; if (mm[m] >= lim) wet++; }
  if (n) return wet == n;
  // no hourly rain: the day's totals, the same agreement rule
  WxDay d;
  LTime lt;
  if (!TimeUtil::toLocal(at, lt) || !sys.wx.day(lt.ymd, d)) return false;
  for (uint8_t m = 0; m < WX_MODELS; m++) if (!isnan(d.rainM[m])) { n++; if (d.rainM[m] >= lim) wet++; }
  return n ? wet == n : d.rain >= lim;
}

bool rainSkips(uint8_t z) {
  return z < ZONES && !sys.cfg.z[z].cov && (rainSensorWet() || forecastRain());
}

ZoneStatus zoneStatus(uint8_t z) {
  const ZoneCfg& c = sys.cfg.z[z];
  if (sys.water.activeZone() == (int8_t)z) return {ZSt::WATERING, ""};
  if (sys.irr.queued(z)) return {ZSt::QUEUED, ""};
  if (sys.irr.soaking(z)) return {ZSt::SOAK, ""};
  if (!c.en) return {ZSt::OFF, ""};
  if (sys.water.faulted()) return {ZSt::FAULT, ""};
  if (sys.cfg.g.off) return {ZSt::ALLOFF, ""};
  if (sys.dnd.active(z, sys.clock.unixNow())) return {ZSt::DND, ""};
  GBlock b = globalBlock();
  if (b != GBlock::NONE) return {ZSt::WAIT, blockName(b)};
  // Closed loop measures the soil, so rain needs no separate rule there
  if (c.m != (uint8_t)ZoneMode::CLOSED_LOOP && !c.cov && rainSensorWet()) return {ZSt::WAIT, "RAIN"};
  return {ZSt::ON, ""};
}

const char* stName(ZSt s) {
  switch (s) {
    case ZSt::OFF:      return "OFF";
    case ZSt::DND:      return "DND";
    case ZSt::WATERING: return "WATERING";
    case ZSt::QUEUED:   return "QUEUED";
    case ZSt::SOAK:     return "SOAK";
    case ZSt::ALLOFF:   return "ALLOFF";
    case ZSt::FAULT:    return "FAULT";
    case ZSt::WAIT:     return "WAIT";
    default:            return "ON";
  }
}

uint8_t seriousErrors() {
  uint8_t m = 0;
  if (sys.water.faulted()) m |= ERR_FAULT;
  if (sys.tank.low()) m |= ERR_TANK;
  if (!sys.clock.valid()) m |= ERR_CLOCK;
  return m;
}

}  // namespace Rules

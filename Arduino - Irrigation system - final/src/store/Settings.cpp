#include "store/Settings.h"
#include <math.h>
#include "util/Nvs.h"
#include "util/Util.h"

namespace {
const char* NS = "cfg";
const char* zoneKey(uint8_t i) { static char k[8]; snprintf(k, sizeof(k), "z%u", (unsigned)i); return k; }

float fclamp(float v, float lo, float hi, float def) { return isnan(v) ? def : clampv(v, lo, hi); }
}  // namespace

void Settings::begin() {
  if (!Nvs::load(NS, "g", g) || g.version != VERSION) {
    defaults(g);
    memset(&_gSaved, 0xFF, sizeof(_gSaved));     // force the first write
  } else {
    _gSaved = g;
  }
  clamp(g);
  saveGlobal();

  for (uint8_t i = 0; i < ZONES; i++) {
    if (!Nvs::load(NS, zoneKey(i), z[i]) || z[i].version != VERSION) {
      defaults(z[i], i);
      memset(&_zSaved[i], 0xFF, sizeof(ZoneCfg));
    } else {
      _zSaved[i] = z[i];
    }
    clamp(z[i], g.frost);
    saveZone(i);
  }
}

bool Settings::saveGlobal() {
  clamp(g);
  return Nvs::save(NS, "g", g, &_gSaved);
}

bool Settings::saveZone(uint8_t i) {
  if (i >= ZONES) return false;
  clamp(z[i], g.frost);
  return Nvs::save(NS, zoneKey(i), z[i], &_zSaved[i]);
}

void Settings::defaults(GlobalCfg& s) {
  memset(&s, 0, sizeof(s));
  s.version = VERSION;
  copyStr(s.name, sizeof(s.name), DEFAULT_NAME);
  s.frost = DEFAULT_FROST_C;
  s.cap = DEFAULT_CAP_MIN;
  s.rainmm = DEFAULT_RAIN_MM;
  s.qto = DEFAULT_QTO_MIN;
  s.lat = DEFAULT_LAT;
  s.lon = DEFAULT_LON;
  s.flow = DEFAULT_FLOW_LPM;
  s.tankE = DEFAULT_TANK_EMPTY_CM;
  s.tankF = DEFAULT_TANK_FULL_CM;
  for (auto& c : s.cal) c = SoilCal{(uint16_t)DEFAULT_SOIL_DRY, (uint16_t)DEFAULT_SOIL_WET};
  s.cmode = 0;
  s.cpw = 620;            // HUF per m3, about the Budapest tariff (edit in the UI)
  s.cpe = 70;             // HUF per kWh
  s.cpp = 30;             // small submersible pump, W
  copyStr(s.cur, sizeof(s.cur), "Ft");
  s.clog = 4 | 16;        // monthly and yearly totals in the log
  s.btn = 1;
  s.buzz = 1;
}

void Settings::defaults(ZoneCfg& s, uint8_t i) {
  memset(&s, 0, sizeof(s));
  s.version = VERSION;
  snprintf(s.name, sizeof(s.name), "Zone %u", (unsigned)(i + 1));
  s.en = 1;
  s.m = (uint8_t)ZoneMode::SCHEDULE;
  s.cov = 0;
  s.cold = DEFAULT_ZONE_COLD_C;
  // The scale model is ryegrass in loam with mist nozzles; the UI presets change these
  s.plant = PlantParams{"lawn", 0.9f, 15, 35, 50};
  s.soil  = SoilParams{"loam", 27, 12, 13};
  s.spr   = SprParams{"mist", 20, 70};
}

void Settings::clamp(GlobalCfg& s) {
  s.name[sizeof(s.name) - 1] = 0;
  if (!validHostname(s.name)) copyStr(s.name, sizeof(s.name), DEFAULT_NAME);
  s.off = s.off ? 1 : 0;
  s.frost = fclamp(s.frost, -10, 10, DEFAULT_FROST_C);
  s.cap = clampv<uint16_t>(s.cap, 5, MAX_ZONE_DAY_S / 60);
  s.rainmm = fclamp(s.rainmm, 0, 20, DEFAULT_RAIN_MM);
  s.qto = clampv<uint16_t>(s.qto, 10, 600);
  s.lat = fclamp(s.lat, -90, 90, DEFAULT_LAT);
  s.lon = fclamp(s.lon, -180, 180, DEFAULT_LON);
  s.flow = fclamp(s.flow, 0.1f, 50, DEFAULT_FLOW_LPM);
  s.tankE = clampv<uint16_t>(s.tankE, 5, 400);
  s.tankF = clampv<uint16_t>(s.tankF, 2, 400);
  if (s.tankF + 2 > s.tankE) { s.tankE = DEFAULT_TANK_EMPTY_CM; s.tankF = DEFAULT_TANK_FULL_CM; }
  for (auto& c : s.cal) {
    c.dry = clampv<uint16_t>(c.dry, 0, 4095);
    c.wet = clampv<uint16_t>(c.wet, 0, 4095);
    if (c.dry <= c.wet) c = SoilCal{(uint16_t)DEFAULT_SOIL_DRY, (uint16_t)DEFAULT_SOIL_WET};
  }
  if (s.cmode > 2) s.cmode = 0;
  s.cpw = fclamp(s.cpw, 0, 100000, 0);
  s.cpe = fclamp(s.cpe, 0, 10000, 0);
  s.cpp = clampv<uint16_t>(s.cpp, 0, 5000);
  s.cur[sizeof(s.cur) - 1] = 0;
  s.clog &= (1 | 2 | 4 | 16);
  s.btn = s.btn ? 1 : 0;
  s.buzz = s.buzz ? 1 : 0;
}

void Settings::clamp(ZoneCfg& s, float frost) {
  s.name[sizeof(s.name) - 1] = 0;
  s.en = s.en ? 1 : 0;
  if (s.m > 2) s.m = 0;
  s.cov = s.cov ? 1 : 0;
  int lo = (int)ceilf(frost);
  s.cold = (int8_t)clampv<int>(s.cold, lo, 30);   // never below the global frost limit
  s.plant.p[7] = s.soil.p[7] = s.spr.p[7] = 0;
  s.plant.kc = fclamp(s.plant.kc, 0.1f, 1.5f, 1.0f);
  s.plant.root = clampv<uint16_t>(s.plant.root, 5, 150);
  s.plant.lo = clampv<uint8_t>(s.plant.lo, 5, 90);
  s.plant.hi = clampv<uint8_t>(s.plant.hi, 10, 100);
  if (s.plant.lo >= s.plant.hi) { s.plant.lo = 35; s.plant.hi = 50; }
  s.soil.fc = clampv<uint8_t>(s.soil.fc, 5, 60);
  s.soil.wp = clampv<uint8_t>(s.soil.wp, 1, 40);
  if (s.soil.wp >= s.soil.fc) s.soil.wp = s.soil.fc - 1;
  s.soil.inf = clampv<uint8_t>(s.soil.inf, 1, 100);
  s.spr.rate = clampv<uint8_t>(s.spr.rate, 1, 100);
  s.spr.eff = clampv<uint8_t>(s.spr.eff, 30, 100);
}

float Settings::availableMm(const ZoneCfg& s) {
  return (s.soil.fc - s.soil.wp) / 100.0f * s.plant.root * 10.0f;
}

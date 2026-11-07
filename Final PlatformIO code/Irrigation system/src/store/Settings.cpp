#include "store/Settings.h"
#include <math.h>
#include "util/Nvs.h"
#include "util/Util.h"
#include "util/TimeUtil.h"

namespace {
const char* NS = "cfg";
const char* zoneKey(uint8_t i) { static char k[8]; snprintf(k, sizeof(k), "z%u", (unsigned)i); return k; }

float fclamp(float v, float lo, float hi, float def) { return isnan(v) ? def : clampv(v, lo, hi); }
}  // namespace

uint32_t Settings::_today = 0;

// Stored settings carry VERSION. A blob of another version (or none) is replaced by defaults: the
// converters for versions 2-4 (firmware 1.0-1.2.5) were removed in 1.3.2, once every controller had
// booted 1.3.x and kept its data.
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
  // An established cool-season lawn (FAO-56 turf) in loam, rotors (HydroPoint defaults), 1 m2, 30 cm of soil.
  // The UI presets change these. (100 % soil = field capacity: calibrate "wet" in soaked, drained soil.)
  s.plant = PlantParams{"lawn", 0.95f, 20, 60, 95, 0.95f, 0.95f, 20, {0, 0, 0, 0}, 0};
  s.soil  = SoilParams{"loam", 25, 12, 15, 30};
  s.spr   = SprParams{"rotor", 19.0f * DEFAULT_AREA_M2 / 60.0f, DEFAULT_AREA_M2, 80};
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
  s.plant.offEnd = s.plant.offEnd ? 1 : 0;
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
  if (s.plant.lo >= s.plant.hi) { s.plant.lo = 60; s.plant.hi = 95; }
  s.plant.kcIni = fclamp(s.plant.kcIni, 0.1f, 1.5f, s.plant.kc);
  s.plant.kcEnd = fclamp(s.plant.kcEnd, 0.1f, 1.5f, s.plant.kc);
  s.plant.rootIni = clampv<uint16_t>(s.plant.rootIni, 1, s.plant.root);
  for (auto& l : s.plant.len) l = clampv<uint16_t>(l, 0, 365);
  { uint32_t v = s.plant.planted; char b[11]; if (v) { TimeUtil::formatYmd(v, b); if (TimeUtil::parseYmd(b) != v) s.plant.planted = 0; } }
  s.soil.depth = clampv<uint16_t>(s.soil.depth, 2, 300);
  s.soil.fc = clampv<uint8_t>(s.soil.fc, 5, 60);
  s.soil.wp = clampv<uint8_t>(s.soil.wp, 1, 40);
  if (s.soil.wp >= s.soil.fc) s.soil.wp = s.soil.fc - 1;
  s.soil.inf = clampv<uint16_t>(s.soil.inf, 1, INF_MAX);
  s.spr.area = fclamp(s.spr.area, AREA_MIN, AREA_MAX, DEFAULT_AREA_M2);
  s.spr.flow = fclamp(s.spr.flow, FLOW_MIN, FLOW_MAX, 20.0f * s.spr.area / 60.0f);
  // keep the resulting rate in range: limit the flow, not the area the user measured
  s.spr.flow = clampv(s.spr.flow, RATE_MIN * s.spr.area / 60.0f, RATE_MAX * s.spr.area / 60.0f);
  s.spr.eff = clampv<uint8_t>(s.spr.eff, 30, 100);
}

float Settings::availableMm(const ZoneCfg& s) {
  return (s.soil.fc - s.soil.wp) / 100.0f * rootNow(s) * 10.0f;
}

// FAO-56 crop coefficient curve and a linear root growth over the initial and development stages
int32_t Settings::dayOf(const ZoneCfg& z, uint32_t ymd) {
  if (!z.plant.planted || !ymd) return 0;
  return TimeUtil::daysFromYmd(ymd) - TimeUtil::daysFromYmd(z.plant.planted);
}

bool Settings::seasonEnded(const ZoneCfg& z, uint32_t fromYmd, uint32_t toYmd) {
  const PlantParams& p = z.plant;
  if (!p.planted || p.len[2] + p.len[3] == 0 || !fromYmd || !toYmd) return false;   // a lawn from seed has no end
  int32_t total = (int32_t)p.len[0] + p.len[1] + p.len[2] + p.len[3];
  return dayOf(z, fromYmd) < total && dayOf(z, toYmd) >= total;
}

Settings::Growth Settings::growth(const ZoneCfg& z) {
  const PlantParams& p = z.plant;
  Growth g{ESTABLISHED, 0, p.kc, (float)p.root};
  uint32_t total = (uint32_t)p.len[0] + p.len[1] + p.len[2] + p.len[3];
  if (p.planted && total && _today) {
    int32_t d = TimeUtil::daysFromYmd(_today) - TimeUtil::daysFromYmd(p.planted);
    g.day = d;
    const float L1 = p.len[0], L2 = p.len[1], L3 = p.len[2], L4 = p.len[3];
    const float grow = L1 + L2;
    if (d < 0) { g.st = NOT_YET; g.kc = p.kcIni; g.rootCm = p.rootIni; }
    else {
      if (d < L1)                { g.st = INITIAL;     g.kc = p.kcIni; }
      else if (d < L1 + L2)      { g.st = DEVELOPMENT; g.kc = p.kcIni + (d - L1) / L2 * (p.kc - p.kcIni); }
      else if (L3 + L4 == 0)     { g.st = ESTABLISHED; g.kc = p.kc; }     // a lawn from seed: now a lawn
      else if (d < L1 + L2 + L3) { g.st = MID;         g.kc = p.kc; }
      else if (d < L1 + L2 + L3 + L4) { g.st = LATE;   g.kc = p.kc + (d - L1 - L2 - L3) / L4 * (p.kcEnd - p.kc); }
      else                       { g.st = OVER;        g.kc = p.kcEnd; }
      g.rootCm = grow > 0 && d < grow ? p.rootIni + (p.root - p.rootIni) * d / grow : p.root;
    }
  }
  if (g.rootCm > z.soil.depth) g.rootCm = z.soil.depth;  // roots cannot go deeper than the soil
  return g;
}

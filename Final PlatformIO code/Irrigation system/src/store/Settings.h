#pragma once
// Runtime settings in NVS: one blob for the global values, one per zone.
// Every value is clamped on load and before every save (Config.h ceilings).
// Version 3 (firmware 1.1): sprinkler = flow + area per zone (the mm/h follows), no global flow.
// Version 4 (firmware 1.2): plant age (FAO-56 growth stages: Kc curve and root growth), soil depth.
// Version 5 (firmware 1.2.6): plant.offEnd (turn the zone off when the season is over).
// Older blobs (2, 3, 4) are converted on the first boot, so an update keeps names, calibration and prices.
#include <Arduino.h>
#include "Config.h"

enum class ZoneMode : uint8_t { SCHEDULE = 0, CLOSED_LOOP = 1, OPEN_LOOP = 2 };

// Plant. Without a planting date (or with no stage lengths) the plant is "established": kc and root
// are used as they are. With a date and stages (FAO-56): Kc ini during the initial stage, a straight
// line to kc (Kc mid) during development, kc during mid-season, a line to Kc end in the late season;
// the roots grow from rootIni to root by the end of development. Stages with mid = late = 0 (a lawn
// from seed) end in the established state.
struct PlantParams {
  char p[8];
  float kc;               // Kc mid (or the established Kc)
  uint16_t root;          // cm, full depth
  uint8_t lo, hi;         // soil %: water below lo, fill up to hi (100 % = field capacity)
  float kcIni, kcEnd;
  uint16_t rootIni;       // cm at planting
  uint16_t len[4];        // days: initial, development, mid-season, late season
  uint32_t planted;       // YYYYMMDD, 0 = established
  uint8_t offEnd;         // 1: turn the zone off when the season is over (the day after the late stage)
};
struct SoilParams  { char p[8]; uint8_t fc, wp; uint16_t inf; uint16_t depth; };   // fc/wp %, inf mm/h, depth cm
struct SprParams   { char p[8]; float flow, area; uint8_t eff; };           // flow L/min, area m2, eff %

struct ZoneCfg {
  uint16_t version;
  char name[97];          // UTF-8, up to 24 code points
  uint8_t en;             // enabled
  uint8_t m;              // ZoneMode
  uint8_t cov;            // covered: no rain credit, rain does not block
  int8_t cold;            // minimum temperature C (>= global frost)
  PlantParams plant;
  SoilParams soil;
  SprParams spr;
};

struct SoilCal { uint16_t dry, wet; };

struct GlobalCfg {
  uint16_t version;
  char name[25];          // controller name = hostname
  uint8_t off;            // All off (= winter mode)
  float frost;            // C
  uint16_t cap;           // daily limit per zone, minutes
  float rainmm;           // forecast rain that skips watering
  uint16_t qto;           // queue timeout, minutes
  float lat, lon;
  uint16_t tankE, tankF;  // cm from the sensor: empty and full
  SoilCal cal[ZONES];
  uint8_t cmode;          // cost basis 0 off, 1 water, 2 power
  float cpw, cpe;         // price per m3, per kWh
  uint16_t cpp;           // pump power W
  char cur[17];           // currency text, up to 4 characters
  uint8_t clog;           // cost periods written to the log: 1 day, 2 week, 4 month, 16 year
  uint8_t btn, buzz;      // button short press, buzzer
};

// Limits shared by the API and the clamp
constexpr float FLOW_MIN = 0.01f, FLOW_MAX = 500.0f;    // L/min
constexpr float AREA_MIN = 0.01f, AREA_MAX = 10000.0f;  // m2
constexpr float RATE_MIN = 0.1f,  RATE_MAX = 5000.0f;   // mm/h
constexpr uint16_t INF_MAX = 2000;                      // mm/h (the scale model drains very fast)

class Settings {
public:
  GlobalCfg g;
  ZoneCfg z[ZONES];

  void begin();
  bool saveGlobal();
  bool saveZone(uint8_t i);

  static void defaults(GlobalCfg& g);
  static void defaults(ZoneCfg& z, uint8_t i);
  static void clamp(GlobalCfg& g);
  static void clamp(ZoneCfg& z, float frost);

  // Plant-available water in the root zone, mm (FAO-56: (fc - wp) x root depth, the depth as today)
  static float availableMm(const ZoneCfg& z);

  // ---- water arithmetic, shared by the water balance, the deficit and the forecast estimate ----
  // The soil % scale is the plant-available water: 0 % = wilting point, 100 % = field capacity.
  // mm of water in the root zone as a change on that scale (0 when the zone has no available water)
  static float mmToPct(const ZoneCfg& z, float mm) {
    float a = availableMm(z);
    return a > 0 ? mm / a * 100.0f : 0;
  }
  // What the weather does to the soil: -ET0 x Kc (the plant's age), + the rain that soaks in (not under cover)
  static float weatherMm(const ZoneCfg& z, float et0, float rain) {
    return -et0 * kcNow(z) + (z.cov ? 0 : rain * RAIN_EFFECTIVE);
  }
  // Net mm a run puts into the soil (the sprinkler's efficiency counted), and the run for a net amount
  static float appliedMm(const ZoneCfg& z, uint32_t sec) { return rate(z) * (sec / 3600.0f) * (z.spr.eff / 100.0f); }
  static uint32_t secondsFor(const ZoneCfg& z, float netMm) {
    return (uint32_t)lroundf(netMm / (z.spr.eff / 100.0f) / rate(z) * 3600.0f);   // 1 s steps: short runs matter
  }

  // ---- plant age ----
  enum Stage : uint8_t { ESTABLISHED, NOT_YET, INITIAL, DEVELOPMENT, MID, LATE, OVER };
  struct Growth { Stage st; int32_t day; float kc; float rootCm; };   // rootCm already capped by the soil depth
  static Growth growth(const ZoneCfg& z);      // as of 'today'
  static float kcNow(const ZoneCfg& z) { return growth(z).kc; }
  static float rootNow(const ZoneCfg& z) { return growth(z).rootCm; }
  static void setToday(uint32_t ymd) { _today = ymd; }
  // Days since planting on a date (negative before it); 0 for an established plant
  static int32_t dayOf(const ZoneCfg& z, uint32_t ymd);
  // The season (all four stages) ended between these two dates: (from, to]
  static bool seasonEnded(const ZoneCfg& z, uint32_t fromYmd, uint32_t toYmd);   // the local date (Scheduler, once a second)
  // Application rate, mm/h: flow (L/min) x 60 / area (m2)
  static float rate(const ZoneCfg& z) { return z.spr.flow * 60.0f / z.spr.area; }
  static float litres(const ZoneCfg& z, uint32_t sec) { return z.spr.flow * sec / 60.0f; }

private:
  static constexpr uint16_t VERSION = 5;   // bump when a struct changes: old data -> defaults
  static uint32_t _today;
  GlobalCfg _gSaved;
  ZoneCfg _zSaved[ZONES];
};

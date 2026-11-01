#pragma once
// Runtime settings in NVS: one blob for the global values, one per zone.
// Every value is clamped on load and before every save (Config.h ceilings).
#include <Arduino.h>
#include "Config.h"

enum class ZoneMode : uint8_t { SCHEDULE = 0, CLOSED_LOOP = 1, OPEN_LOOP = 2 };

struct PlantParams { char p[8]; float kc; uint16_t root; uint8_t lo, hi; };   // root cm, lo/hi soil %
struct SoilParams  { char p[8]; uint8_t fc, wp, inf; };                     // fc/wp %, inf mm/h
struct SprParams   { char p[8]; uint8_t rate, eff; };                       // rate mm/h, eff %

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
  float flow;             // L/min
  uint16_t tankE, tankF;  // cm from the sensor: empty and full
  SoilCal cal[ZONES];
  uint8_t cmode;          // cost basis 0 off, 1 water, 2 power
  float cpw, cpe;         // price per m3, per kWh
  uint16_t cpp;           // pump power W
  char cur[17];           // currency text, up to 4 characters
  uint8_t clog;           // cost periods written to the log: 1 day, 2 week, 4 month, 16 year
  uint8_t btn, buzz;      // button short press, buzzer
};

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

  // Plant-available water in the root zone, mm (FAO-56 style: (fc - wp) x root depth)
  static float availableMm(const ZoneCfg& z);

private:
  static constexpr uint16_t VERSION = 2;   // bump when a struct changes: old data -> defaults
  GlobalCfg _gSaved;
  ZoneCfg _zSaved[ZONES];
};

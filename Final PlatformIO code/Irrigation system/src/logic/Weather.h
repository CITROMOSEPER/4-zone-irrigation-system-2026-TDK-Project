#pragma once
// Open-Meteo forecast (no API key), two models in one call: DWD ICON and ECMWF IFS. A month of
// logging against a station 900 m away (docs/weather-api-research.md) found these and MET Norway
// the most accurate for Budapest; GFS ran 2.5 C warm and is not used.
//   daily  yesterday + 7 days: min/max temperature, ET0 and rain (mean of the models; rain per model too)
//   hourly about yesterday to 2 days ahead: air temperature (mean) and rain per model
// Rain is trusted for 1-2 days ahead only: the rain skip asks both models about the next 24 h, and
// the next-watering estimate counts rain no further than 48 h ahead (beyond ~3 days it is noise).
// The HTTPS fetch blocks for seconds, so it runs in its own FreeRTOS task; the loop only reads
// the cache (under a mutex). Failures keep the last good data.
#include <Arduino.h>

constexpr uint8_t WX_MODELS = 2;               // 0 ICON, 1 ECMWF
constexpr uint8_t WX_DAYS   = 8;
constexpr uint8_t WX_HOURS  = 96;

struct WxDay {
  uint32_t ymd;
  float tmax, tmin, et0, rain;   // C, mm; the mean of the models that have the value
  float rainM[WX_MODELS];        // rain per model, NAN = not given
};

struct WxSnapshot {
  bool ok;                       // the last fetch worked
  uint32_t t;                    // unix time of the last good fetch, 0 = never
  uint8_t n;
  WxDay d[WX_DAYS];
  uint32_t h0;                   // unix time of hourly[0]
  uint8_t nh;                    // hourly values
  int16_t hr[WX_HOURS];          // air temperature x 10, C (mean of the models)
  int16_t hp[WX_MODELS][WX_HOURS];   // rain in that hour x 10, mm; -1 = not given
};

class Weather {
public:
  void begin();
  void requestRefresh() { _refresh = true; }   // location changed

  WxSnapshot snapshot() const;
  bool day(uint32_t ymd, WxDay& out) const;    // from the cache
  bool fresh() const;                          // good data, fetched within WX_FRESH_S
  // The forecast air temperature at a unix time (hourly values, interpolated); NAN if not covered or not fresh
  float tempAt(uint32_t unixT) const;
  // Rain over [from, from + hours) per model; have[m] = false when that model does not cover the window
  void rainWindow(uint32_t from, uint8_t hours, float mm[WX_MODELS], bool have[WX_MODELS]) const;
  // ET0 and rain (mm) between two unix times, from the daily values in proportion; rain only for the
  // part within WX_RAIN_TRUST_S of 'from'. False when a day in between is not in the forecast.
  bool between(uint32_t from, uint32_t to, float& et0, float& rain) const;

  static bool parse(const char* body, WxSnapshot& s);   // an Open-Meteo answer -> days and hours
  void inject(const WxSnapshot& s);            // tests: as if this had just been fetched

  // The last attempt, for /health: when, the HTTP status (negative: HTTPClient's connection errors),
  // the answer's size, whether it parsed, and how much of the task's stack the fetch left unused
  struct Diag { uint32_t t; int code; uint32_t bytes; bool parsed; uint32_t stackLeft; uint32_t heapLargest; };
  Diag diag() const { return _diag; }

  // FAO-56 Hargreaves ET0 (mm/day) from the day's min/max temperature and the latitude
  static float hargreavesEt0(float tMin, float tMax, float latDeg, int dayOfYear);

private:
  static void taskMain(void* self);
  void loop();
  bool fetch();

  WxSnapshot _c{};
  WxSnapshot _n{};                             // the fetch builds here (not on the task's stack)
  Diag _diag{};
  mutable void* _mx = nullptr;                 // SemaphoreHandle_t
  volatile bool _refresh = true;
};

#pragma once
// Open-Meteo daily forecast (no API key): yesterday, today and tomorrow.
// The HTTPS fetch blocks for seconds, so it runs in its own FreeRTOS task; the loop only reads
// the cache (under a mutex). Failures keep the last good data.
#include <Arduino.h>

struct WxDay {
  uint32_t ymd;
  float tmax, tmin, et0, rain;   // C, mm
  int rp;                        // chance of rain %, -1 = unknown
};

struct WxSnapshot {
  bool ok;                       // the last fetch worked
  uint32_t t;                    // unix time of the last good fetch, 0 = never
  uint8_t n;
  WxDay d[3];
};

class Weather {
public:
  void begin();
  void requestRefresh() { _refresh = true; }   // location changed

  WxSnapshot snapshot() const;
  bool day(uint32_t ymd, WxDay& out) const;    // from the cache
  bool fresh() const;                          // good data, fetched within WX_FRESH_S

  // FAO-56 Hargreaves ET0 (mm/day) from the day's min/max temperature and the latitude
  static float hargreavesEt0(float tMin, float tMax, float latDeg, int dayOfYear);

private:
  static void taskMain(void* self);
  void loop();
  bool fetch();

  WxSnapshot _c{};
  mutable void* _mx = nullptr;                 // SemaphoreHandle_t
  volatile bool _refresh = true;
};

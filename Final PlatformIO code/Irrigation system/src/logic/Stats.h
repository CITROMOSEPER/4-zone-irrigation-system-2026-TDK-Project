#pragma once
// Measurements over time, for the charts.
//
//   fast     RAM, every 3 s for 3 hours: soil % (each zone), tank distance, pump/valves/fault flags
//   minute   RAM, every minute for 3 hours: soil raw, temperature, humidity, rain, Wi-Fi signal,
//            open-loop estimate and deficit (each zone)
//   quarter  flash (/stats.bin), every 15 minutes for 5 weeks: averages of soil %, estimate, tank %,
//            temperature, humidity, and the seconds each zone watered in that quarter
//
// Values are averages over their interval (the sensors are read every second), so sensor noise
// does not make the lines jitter. The RAM window is lost on a restart; the flash ring is not.
// The flash ring has a fixed slot per quarter-hour (t / 900 mod N): no index to keep, and a gap
// (controller off) simply shows as a missing record.
#include <Arduino.h>
#include "Config.h"

class WebServer;

class Stats {
public:
  void begin();
  void update();                               // every loop pass; works once a second

  void addWatered(uint8_t zone, uint32_t sec); // a run ended: counted in the current quarter

  // HTTP: CSV, streamed
  void sendFast(WebServer& s);                 // /api/stats/fast
  void sendMinute(WebServer& s);               // /api/stats/min
  void sendQuarter(WebServer& s, uint16_t days);   // /api/stats/q?days=7


  struct __attribute__((packed)) Fast { uint8_t soil[ZONES]; uint16_t tankMm; uint8_t flags; };
  struct __attribute__((packed)) Minute {
    uint16_t raw[ZONES]; int16_t temp; uint8_t hum; uint8_t rain; int8_t rssi;
    uint8_t est[ZONES]; uint16_t def[ZONES];
  };
  struct __attribute__((packed)) Quarter {
    uint32_t t; uint8_t soil[ZONES], est[ZONES]; uint8_t tank, hum; int16_t temp; uint16_t w[ZONES];
  };
  // flags of a fast sample
  enum : uint8_t { F_PUMP = 1, F_VALVE0 = 2, F_FAULT = 32, F_TANKLOW = 64, F_RAIN = 128 };

private:
  struct Acc { float sum[ZONES]; uint16_t n[ZONES]; float tSum; uint16_t tN; float hSum; uint16_t hN; float tankSum; uint16_t tankN; };
  void sample1s();
  void pushFast();
  void pushMinute();
  void writeQuarter(uint32_t qStart);
  static void addTo(Acc& a, const float* soil, float tankCm, float tankPct, float t, float h);
  static uint8_t pct2(float v) { return isnan(v) ? 255 : (uint8_t)lroundf(constrain(v, 0.0f, 100.0f) * 2); }

  Fast _fast[STAT_FAST_N];
  Minute _min[STAT_MIN_N];
  uint16_t _fHead = 0, _fN = 0, _mHead = 0, _mN = 0;
  uint32_t _fLastMs = 0, _mLastMs = 0;         // millis of the newest sample
  Acc _af{}, _aq{};                            // fast (3 s) and quarter accumulators
  uint32_t _w[ZONES] = {};                     // seconds watered this quarter
  uint32_t _t1 = 0, _secs = 0;
  uint32_t _qStart = 0;                        // unix start of the quarter being collected
  bool _fileOk = false;
};

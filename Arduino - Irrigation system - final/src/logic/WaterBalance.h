#pragma once
// Open loop: estimates each zone's soil moisture with no sensor, as a daily water balance
// (FAO-56 style, simplified to one bucket):
//   every local midnight:  soil -= ET0 x Kc        (ET0 from Open-Meteo, or Hargreaves from the BME280)
//                          soil += rain x 0.8      (not for covered zones)
//   after every run:       soil += applied water x efficiency
// The estimate is a % of plant-available water, the same scale as lo/hi and the deficit formula.
#include <Arduino.h>
#include "Config.h"

class WaterBalance {
public:
  void begin();
  float estimate(uint8_t zone) const { return zone < ZONES ? _s.pct[zone] : NAN; }

  void dailyStep(uint32_t endedYmd, float tMin, float tMax);   // called at local midnight
  void addApplied(uint8_t zone, uint32_t pumpSeconds);
  void reset(uint8_t zone);                                    // back to the zone's target

  float lastEt0() const { return _s.lastEt0; }                 // mm, for the health text

private:
  struct Stored { float pct[ZONES]; float lastEt0; uint32_t ymd; };
  void save();
  Stored _s{};
};

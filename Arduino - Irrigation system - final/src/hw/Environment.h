#pragma once
// BME280: temperature, humidity, pressure. Getters return NAN while faulted.
// Also keeps the local day's minimum and maximum temperature (Hargreaves ET0 fallback).
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include "Config.h"
#include "PINS.h"
#include "util/Util.h"

class Environment {
public:
  void begin() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    init();
    _last = millis() - ENV_PERIOD_MS;   // first update() reads at once
  }

  void update() {
    if (!elapsed(_last, ENV_PERIOD_MS)) return;
    _last = millis();
    if (!_ok) {                         // faulted: try to start it again
      init();
      if (!_ok) return;
    }
    float t = _bme.readTemperature();
    float h = _bme.readHumidity();
    float p = _bme.readPressure() / 100.0f;
    bool good = !isnan(t) && !isnan(h) && !isnan(p) && t > -40 && t < 85 && h >= 0 && h <= 100;
    if (good) {
      _t = t; _h = h; _p = p;
      _fails = 0;
      if (isnan(_tMin) || t < _tMin) _tMin = t;
      if (isnan(_tMax) || t > _tMax) _tMax = t;
    } else if (++_fails >= ENV_FAULT_COUNT) {
      _ok = false;
    }
  }

  bool valid() const { return _ok; }
  float temperatureC() const { return _ok ? _t : NAN; }
  float humidity() const { return _ok ? _h : NAN; }
  float pressureHpa() const { return _ok ? _p : NAN; }

  // The extremes since the last call (NAN if none); called at local midnight
  void takeDayExtremes(float& tMin, float& tMax) {
    tMin = _tMin;
    tMax = _tMax;
    _tMin = _tMax = NAN;
  }

private:
  void init() {
    _ok = _bme.begin(0x76, &Wire) || _bme.begin(0x77, &Wire);
    if (_ok) {
      // 1x oversampling and 1 s standby: little self-heating, no blocking wait
      _bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                       Adafruit_BME280::SAMPLING_X1, Adafruit_BME280::SAMPLING_X1, Adafruit_BME280::SAMPLING_X1,
                       Adafruit_BME280::FILTER_OFF, Adafruit_BME280::STANDBY_MS_1000);
    }
    _fails = 0;
  }

  Adafruit_BME280 _bme;
  bool _ok = false;
  uint8_t _fails = 0;
  float _t = NAN, _h = NAN, _p = NAN, _tMin = NAN, _tMax = NAN;
  uint32_t _last = 0;
};

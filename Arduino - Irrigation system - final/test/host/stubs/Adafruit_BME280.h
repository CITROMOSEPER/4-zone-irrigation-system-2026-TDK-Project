#pragma once
#include <Wire.h>
namespace host { extern bool bmeOk; extern float bmeT, bmeH, bmeP; }
class Adafruit_BME280 {
public:
  enum sensor_mode { MODE_NORMAL };
  enum sensor_sampling { SAMPLING_X1 };
  enum sensor_filter { FILTER_OFF };
  enum standby_duration { STANDBY_MS_1000 };
  bool begin(uint8_t, TwoWire*) { return host::bmeOk; }
  void setSampling(sensor_mode, sensor_sampling, sensor_sampling, sensor_sampling, sensor_filter, standby_duration) {}
  float readTemperature() { return host::bmeOk ? host::bmeT : NAN; }
  float readHumidity() { return host::bmeOk ? host::bmeH : NAN; }
  float readPressure() { return host::bmeOk ? host::bmeP * 100 : NAN; }
};

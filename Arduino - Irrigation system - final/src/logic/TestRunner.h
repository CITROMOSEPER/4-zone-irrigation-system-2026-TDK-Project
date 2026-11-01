#pragma once
// Output test from the Health page. Never while a zone waters.
//   valve z:  open 60 s (several at once)
//   pump:     with valves open, runs (after the valve-open delay) until 2 s before the last one closes;
//             with z and no valve open, opens valve z for 60 s and runs with it;
//             alone (no z, no valve) for 10 s against closed valves
//   led 3 s, buzzer two beeps (ignores the sound setting)
// Stopping the last open valve also stops a pump that was running with it.
// WaterController's TEST state keeps the tank-low stop and the hard caps.
#include <Arduino.h>
#include "Config.h"

struct TestAnswer { const char* r; uint32_t sec; };

class TestRunner {
public:
  void update();

  TestAnswer start(const char* o, int z);      // o: valve pump led buzz
  void stop(const char* o, int z);             // o: "" = everything
  bool active() const;

  // Running tests for /api/state: output, zone (valves) and end (unix)
  struct Item { const char* o; uint8_t z; uint32_t u; };
  uint8_t list(Item* out, uint8_t max) const;

private:
  struct Out { bool on; uint32_t untilMs; };
  uint32_t lastValveEnd() const;               // millis, 0 = no valve test
  void stopPumpIfNoValve();

  Out _valve[ZONES] = {}, _pump{}, _led{}, _buzz{};
  bool _pumpAlone = false;
  uint32_t _valvesOpenSince = 0;
  bool _anyValve = false;
};

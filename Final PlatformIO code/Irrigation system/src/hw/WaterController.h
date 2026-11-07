#pragma once
// WaterController: the only code allowed to switch the pump and valves.
//
// Interlocks: one zone at a time; valve opens, then the pump starts; pump stops, then the valve
// closes; tank-low stop; min/max run time; per-zone daily cap; every output has its own hard
// on-time cap (latched fault); everything off at boot; stray outputs forced off while idle.
//
// TEST state (output test from the Health page): only allowed while idle. The TestRunner chooses
// which valves and the pump are on; the tank-low stop and the hard caps still apply.
#include <Arduino.h>
#include "Config.h"
#include "hw/TimedOutput.h"
#include "hw/TankSensor.h"

enum class StartResult : uint8_t { OK, BUSY, BAD_ZONE, TANK_LOW, TOO_SHORT, DAILY_LIMIT, FAULT };
enum class StopReason  : uint8_t { NONE, DONE, USER, DND, TANK_LOW, FAULT };

const char* startName(StartResult r);
const char* stopName(StopReason r);   // the log's result column: DONE, USER, DND, TANK_LOW, FAULT

// What a finished run did; taken once by Irrigation
struct RunReport {
  uint8_t zone;
  uint32_t pumpS;       // seconds the pump actually ran
  StopReason reason;
};

class WaterController {
public:
  void begin(TankSensor* tank);
  void update();                                   // every loop pass

  void setDailyCapS(uint32_t s) { _capS = min(s, MAX_ZONE_DAY_S); }

  StartResult start(uint8_t zone, uint32_t seconds);
  // Change the running zone's length: it now runs 'seconds' more (pump time), within MAX_RUN_S and
  // today's budget. Nothing switches. False when no run is in progress.
  bool adjust(uint32_t seconds);
  void stop(StopReason reason);                    // pump off now, valve after VALVE_CLOSE_DELAY_MS

  bool takeReport(RunReport& out);                 // true once per finished run

  // ---- status ----
  bool busy() const { return _state != State::IDLE && _state != State::TEST; }
  bool running() const { return _state == State::VALVE_OPEN || _state == State::RUNNING; }
  int8_t activeZone() const { return busy() ? (int8_t)_zone : -1; }
  uint32_t runElapsedS() const { return running() && _state == State::RUNNING ? (millis() - _pumpOnAt) / 1000UL : 0; }   // pump time
  uint32_t runTotalS() const { return busy() ? _runMs / 1000UL : 0; }
  bool pumpOn() const { return _pump.isOn(); }
  bool valveOn(uint8_t z) const { return z < ZONES && _valve[z].isOn(); }

  // ---- daily budget ----
  uint32_t secondsToday(uint8_t z) const { return z < ZONES ? _today[z] : 0; }
  uint32_t remainingToday(uint8_t z) const { return z < ZONES && _today[z] < _capS ? _capS - _today[z] : 0; }
  void setSecondsToday(uint8_t z, uint32_t s) { if (z < ZONES) _today[z] = s; }
  void resetDaily() { for (auto& t : _today) t = 0; }

  // ---- faults (latched hard caps) ----
  bool faulted() const;
  bool clearFaults();                              // true if something was latched

  // ---- output test ----
  // Allowed only while idle or already testing. valveMask bit z = valve z on.
  // The pump may run with no valve open only when pumpAlone is set.
  bool testSet(uint8_t valveMask, bool pump, bool pumpAlone);
  bool testing() const { return _state == State::TEST; }

private:
  enum class State : uint8_t { IDLE, VALVE_OPEN, RUNNING, STOPPING, TEST };

  void allOff();
  void finish();

  TankSensor* _tank = nullptr;
  TimedOutput _pump;
  TimedOutput _valve[ZONES];
  State _state = State::IDLE;
  uint8_t _zone = 0;
  StopReason _reason = StopReason::NONE;
  uint32_t _t0 = 0, _tStop = 0, _runMs = 0, _pumpOnAt = 0, _pumpMs = 0;
  uint32_t _today[ZONES] = {};
  uint32_t _capS = DEFAULT_CAP_MIN * 60UL;
  bool _reportReady = false;
  RunReport _report{};
};

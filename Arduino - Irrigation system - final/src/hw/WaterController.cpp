#include "hw/WaterController.h"
#include "PINS.h"
#include "util/Util.h"

const char* startName(StartResult r) {
  switch (r) {
    case StartResult::OK:          return "OK";
    case StartResult::BUSY:        return "BUSY";
    case StartResult::BAD_ZONE:    return "BAD_ZONE";
    case StartResult::TANK_LOW:    return "TANK_LOW";
    case StartResult::TOO_SHORT:   return "TOO_SHORT";
    case StartResult::DAILY_LIMIT: return "DAILY_LIMIT";
    default:                       return "FAULT";
  }
}

const char* stopName(StopReason r) {
  switch (r) {
    case StopReason::DONE:     return "DONE";
    case StopReason::USER:     return "USER";
    case StopReason::DND:      return "DND";
    case StopReason::TANK_LOW: return "TANK_LOW";
    case StopReason::FAULT:    return "FAULT";
    default:                   return "NONE";
  }
}

void WaterController::begin(TankSensor* tank) {
  _tank = tank;
  const uint32_t capMs = MAX_RUN_S * 1000UL + HARD_CAP_MARGIN_MS;
  _pump.begin(PIN_PUMP, capMs);                    // pump first
  for (uint8_t i = 0; i < ZONES; i++) _valve[i].begin(PIN_VALVE[i], capMs);
}

StartResult WaterController::start(uint8_t zone, uint32_t seconds) {
  if (zone >= ZONES) return StartResult::BAD_ZONE;
  if (_state != State::IDLE) return StartResult::BUSY;
  if (_tank->low()) return StartResult::TANK_LOW;
  if (faulted()) return StartResult::FAULT;
  if (seconds < MIN_RUN_S) return StartResult::TOO_SHORT;
  uint32_t left = remainingToday(zone);
  if (left < MIN_RUN_S) return StartResult::DAILY_LIMIT;

  seconds = min(seconds, MAX_RUN_S);
  seconds = min(seconds, left);                    // trim to what is left of today's budget
  if (!_valve[zone].on()) return StartResult::FAULT;

  _zone = zone;
  _runMs = seconds * 1000UL;
  _t0 = millis();
  _pumpOnAt = 0;
  _pumpMs = 0;
  _reason = StopReason::NONE;
  _state = State::VALVE_OPEN;
  return StartResult::OK;
}

void WaterController::stop(StopReason reason) {
  if (_state != State::VALVE_OPEN && _state != State::RUNNING) return;
  if (_pump.isOn()) _pumpMs = millis() - _pumpOnAt;
  _pump.off();
  _reason = reason;
  _tStop = millis();
  _state = State::STOPPING;
}

void WaterController::finish() {
  _valve[_zone].off();
  uint32_t pumpS = (_pumpMs + 500) / 1000;
  _today[_zone] += pumpS;
  _report = RunReport{_zone, pumpS, _reason};
  _reportReady = true;
  _state = State::IDLE;
}

bool WaterController::takeReport(RunReport& out) {
  if (!_reportReady) return false;
  out = _report;
  _reportReady = false;
  return true;
}

void WaterController::update() {
  _pump.update();
  for (auto& v : _valve) v.update();

  // A tripped hard cap ends the run
  if (running() && (_pump.fault() || _valve[_zone].fault())) stop(StopReason::FAULT);

  switch (_state) {
    case State::IDLE:
      allOff();                                    // nothing may be on while idle
      break;

    case State::VALVE_OPEN:
      if (_tank->low()) stop(StopReason::TANK_LOW);
      else if (elapsed(_t0, PUMP_START_DELAY_MS)) {
        if (_pump.on()) { _pumpOnAt = millis(); _state = State::RUNNING; }
        else stop(StopReason::FAULT);
      }
      break;

    case State::RUNNING:
      if (_tank->low()) stop(StopReason::TANK_LOW);
      else if (elapsed(_pumpOnAt, _runMs)) stop(StopReason::DONE);   // run length = pump time
      break;

    case State::STOPPING:
      if (elapsed(_tStop, VALVE_CLOSE_DELAY_MS)) finish();
      break;

    case State::TEST:
      if (_tank->low() && _pump.isOn()) _pump.off();
      if (faulted()) { allOff(); _state = State::IDLE; }
      break;
  }
}

void WaterController::allOff() {
  if (_pump.isOn()) _pump.off();
  for (auto& v : _valve) if (v.isOn()) v.off();
}

bool WaterController::faulted() const {
  if (_pump.fault()) return true;
  for (const auto& v : _valve) if (v.fault()) return true;
  return false;
}

bool WaterController::clearFaults() {
  bool any = faulted();
  _pump.clearFault();
  for (auto& v : _valve) v.clearFault();
  return any;
}

bool WaterController::testSet(uint8_t valveMask, bool pump, bool pumpAlone) {
  if (_state != State::IDLE && _state != State::TEST) return false;
  if (!valveMask && !pump) {
    allOff();
    _state = State::IDLE;
    return true;
  }
  _state = State::TEST;
  // Pump first when stopping: never pump against valves that are closing
  bool pumpWanted = pump && (valveMask || pumpAlone) && !_tank->low();
  if (!pumpWanted && _pump.isOn()) _pump.off();
  // With the pump on, a valve only closes while another stays open (pumpWanted implies that,
  // or a pump-alone test the user chose)
  for (uint8_t z = 0; z < ZONES; z++) {
    bool want = valveMask & (1 << z);
    if (want && !_valve[z].isOn()) _valve[z].on();
    else if (!want && _valve[z].isOn()) _valve[z].off();
  }
  if (pumpWanted && !_pump.isOn()) _pump.on();
  return true;
}

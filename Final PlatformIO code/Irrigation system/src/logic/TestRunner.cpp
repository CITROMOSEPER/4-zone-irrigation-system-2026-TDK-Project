#include "logic/TestRunner.h"
#include "System.h"

namespace {
constexpr uint32_t VALVE_MS = 60000, PUMP_ALONE_MS = 3000, LED_MS = 3000, BUZZ_MS = 2000, PUMP_LEAD_MS = 2000;
bool expired(uint32_t untilMs) { return (int32_t)(millis() - untilMs) >= 0; }
}  // namespace

bool TestRunner::active() const {
  if (_pump.on || _led.on || _buzz.on) return true;
  for (const auto& v : _valve) if (v.on) return true;
  return false;
}

uint32_t TestRunner::lastValveEnd() const {
  uint32_t e = 0;
  const uint32_t now = millis();
  for (const auto& v : _valve) if (v.on && (e == 0 || (int32_t)(v.untilMs - e) > 0)) e = v.untilMs;
  return e && (int32_t)(e - now) > 0 ? e : 0;
}

void TestRunner::stopPumpIfNoValve() {
  if (!_pump.on || _pumpAlone) return;
  uint32_t e = lastValveEnd();
  if (!e) _pump.on = false;                                  // never pump against closed valves
  else if ((int32_t)(_pump.untilMs - (e - PUMP_LEAD_MS)) > 0) _pump.untilMs = e - PUMP_LEAD_MS;
}

TestAnswer TestRunner::start(const char* o, int z) {
  if (sys.water.busy()) return {"TEST_BUSY", 0};
  bool water = !strcmp(o, "valve") || !strcmp(o, "pump");
  if (water && sys.water.faulted()) return {"FAULT", 0};
  if (!strcmp(o, "pump") && sys.tank.low()) return {"TANK_LOW", 0};
  const uint32_t now = millis();

  if (!strcmp(o, "valve")) {
    if (z < 1 || z > ZONES) return {nullptr, 0};
    _valve[z - 1] = Out{true, now + VALVE_MS};
    return {"OK", VALVE_MS / 1000};
  }
  if (!strcmp(o, "pump")) {
    if (z && (z < 1 || z > ZONES)) return {nullptr, 0};
    _pump.on = false;
    if (z) _valve[z - 1] = Out{true, now + VALVE_MS};
    uint32_t e = lastValveEnd();
    if (e) {
      uint32_t end = e - PUMP_LEAD_MS;
      if ((int32_t)(end - now) <= 0) return {"OK", 0};
      _pump = Out{true, end};
      _pumpAlone = false;
      return {"OK", (end - now) / 1000};
    }
    _pump = Out{true, now + PUMP_ALONE_MS};
    _pumpAlone = true;
    return {"OK", PUMP_ALONE_MS / 1000};
  }
  if (!strcmp(o, "led")) {
    _led = Out{true, now + LED_MS};
    return {"OK", LED_MS / 1000};
  }
  if (!strcmp(o, "buzz")) {
    _buzz = Out{true, now + BUZZ_MS};
    sys.fb.testBuzzer();
    return {"OK", BUZZ_MS / 1000};
  }
  return {nullptr, 0};
}

void TestRunner::stop(const char* o, int z) {
  bool all = !o || !*o;
  if (all || !strcmp(o, "valve")) {
    for (uint8_t i = 0; i < ZONES; i++) if (all || i + 1 == z) _valve[i].on = false;
  }
  if (all || !strcmp(o, "pump")) _pump.on = false;
  if (all || !strcmp(o, "led")) _led.on = false;
  if (all || !strcmp(o, "buzz")) _buzz.on = false;
  stopPumpIfNoValve();
  update();                                                 // apply at once
}

void TestRunner::update() {
  const uint32_t now = millis();
  for (auto& v : _valve) if (v.on && expired(v.untilMs)) v.on = false;
  if (_pump.on && expired(_pump.untilMs)) _pump.on = false;
  if (_led.on && expired(_led.untilMs)) _led.on = false;
  if (_buzz.on && expired(_buzz.untilMs)) _buzz.on = false;
  stopPumpIfNoValve();

  if (sys.water.faulted() || sys.water.busy()) {            // a fault ends every test
    for (auto& v : _valve) v.on = false;
    _pump.on = false;
  }
  if (sys.tank.low()) _pump.on = false;

  uint8_t mask = 0;
  for (uint8_t i = 0; i < ZONES; i++) if (_valve[i].on) mask |= 1 << i;
  if (mask && !_anyValve) _valvesOpenSince = now;
  _anyValve = mask != 0;
  // The valve opens first; the pump follows after the usual delay
  bool pump = _pump.on && (_pumpAlone || (mask && now - _valvesOpenSince >= PUMP_START_DELAY_MS));

  sys.fb.testLed(_led.on);
  if (mask || pump) sys.water.testSet(mask, pump, _pumpAlone);
  else if (sys.water.testing()) sys.water.testSet(0, false, false);
}

uint8_t TestRunner::list(Item* out, uint8_t max) const {
  uint8_t n = 0;
  const uint32_t now = millis(), t = sys.clock.unixNow();
  auto add = [&](const char* o, uint8_t z, const Out& x) {
    if (!x.on || n >= max) return;
    uint32_t left = (int32_t)(x.untilMs - now) > 0 ? (x.untilMs - now + 999) / 1000 : 0;
    out[n++] = Item{o, z, t + left};
  };
  for (uint8_t i = 0; i < ZONES; i++) add("valve", i + 1, _valve[i]);
  add("pump", 0, _pump);
  add("led", 0, _led);
  add("buzz", 0, _buzz);
  return n;
}

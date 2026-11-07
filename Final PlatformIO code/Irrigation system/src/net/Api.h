#pragma once
// HTTP API v2 (the controller's own API, see docs/API.md): the contract with the web UI.
// POST bodies are form-encoded; answers are {"ok":1} or {"e":"<what>"} unless noted.
// Handlers run in the main loop between two passes, so nothing races with them; they only
// REQUEST things (watering always goes through Irrigation -> WaterController).
// Only in station mode: the setup AP is an open network.
//
//   GET  /api/state /api/zone?id= /api/calendar /api/settings /api/weather /api/log?m=YYYY-MM /api/log/months
//        /api/stats/fast /api/stats/min /api/stats/q?days=   /log.csv
//   POST /api/zone /api/sched /api/dsch /api/dnd /api/water /api/stop /api/global /api/settings
//        /api/cal /api/test /api/cost/reset /api/clock /api/faults/clear /api/wifi/reset /api/factory /api/reboot
#include <Arduino.h>
#include <WebServer.h>
#include <ArduinoJson.h>

class Api {
public:
  void routes(WebServer& s);

  // JSON builders (also used by the host test)
  void buildState(JsonDocument& d);
  void buildZone(JsonDocument& d, uint8_t zone);
  void buildCalendar(JsonDocument& d);
  void buildSettings(JsonDocument& d);
  void buildWeather(JsonDocument& d);

private:
  // ---- handlers ----
  void postZone();
  void postSched();
  void postDsch();
  void postDnd();
  void postWater();
  void postGlobal();
  void postSettings();
  void postCal();
  void postTest();
  void postClock();

  // ---- helpers ----
  bool guard();                                // station mode only
  bool has(const char* k) { return _s->hasArg(k); }
  String str(const char* k) { return _s->arg(k); }
  long num(const char* k) { return _s->arg(k).toInt(); }
  float fnum(const char* k) { String v = _s->arg(k); v.replace(',', '.'); return v.toFloat(); }
  void send(JsonDocument& d);
  void ok();
  void err(const char* what);

  using Builder = void (Api::*)(JsonDocument&);
  void get(Builder b);

  WebServer* _s = nullptr;
};

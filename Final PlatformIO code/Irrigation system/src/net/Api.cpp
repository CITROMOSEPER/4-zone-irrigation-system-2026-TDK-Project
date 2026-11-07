// API v2 routes and the POST handlers
#include "net/Api.h"
#include <WiFi.h>
#include "System.h"
#include "util/TimeUtil.h"

// ---------------- plumbing ----------------
bool Api::guard() {
  if (sys.net.mode() == WifiPortal::Mode::STA) return true;
  _s->send(404, "text/plain", "not found");
  return false;
}

void Api::send(JsonDocument& d) {
  String out;
  serializeJson(d, out);
  _s->sendHeader("Cache-Control", "no-store");
  _s->send(200, "application/json", out);
}

void Api::ok() {
  _s->sendHeader("Cache-Control", "no-store");
  _s->send(200, "application/json", "{\"ok\":1}");
}

void Api::err(const char* what) {
  JsonDocument d;
  d["e"] = what;
  send(d);
}

void Api::get(Builder b) {
  JsonDocument d;
  (this->*b)(d);
  send(d);
}

void Api::routes(WebServer& s) {
  _s = &s;
  auto on = [this, &s](const char* path, HTTPMethod m, std::function<void()> fn) {
    s.on(path, m, [this, fn]() { if (guard()) fn(); });
  };

  on("/api/state",    HTTP_GET, [this]() { get(&Api::buildState); });
  on("/api/calendar", HTTP_GET, [this]() { get(&Api::buildCalendar); });
  on("/api/settings", HTTP_GET, [this]() { get(&Api::buildSettings); });
  on("/api/weather",  HTTP_GET, [this]() { get(&Api::buildWeather); });
  on("/api/zone",     HTTP_GET, [this]() {
    long id = num("id");
    if (id < 1 || id > ZONES) { err("id"); return; }
    JsonDocument d;
    buildZone(d, (uint8_t)(id - 1));
    send(d);
  });
  on("/api/log", HTTP_GET, [this]() { sys.log.sendMonth(*_s, str("m").c_str()); });
  on("/api/log/months", HTTP_GET, [this]() { sys.log.sendMonths(*_s); });
  on("/log.csv", HTTP_GET, [this]() { sys.log.sendAll(*_s); });
  on("/api/stats/fast", HTTP_GET, [this]() { sys.stats.sendFast(*_s); });
  on("/api/stats/min", HTTP_GET, [this]() { sys.stats.sendMinute(*_s); });
  on("/api/stats/q", HTTP_GET, [this]() { sys.stats.sendQuarter(*_s, has("days") ? (uint16_t)num("days") : 7); });

  on("/api/zone",     HTTP_POST, [this]() { postZone(); });
  on("/api/sched",    HTTP_POST, [this]() { postSched(); });
  on("/api/dsch",     HTTP_POST, [this]() { postDsch(); });
  on("/api/dnd",      HTTP_POST, [this]() { postDnd(); });
  on("/api/water",    HTTP_POST, [this]() { postWater(); });
  on("/api/global",   HTTP_POST, [this]() { postGlobal(); });
  on("/api/settings", HTTP_POST, [this]() { postSettings(); });
  on("/api/cal",      HTTP_POST, [this]() { postCal(); });
  on("/api/test",     HTTP_POST, [this]() { postTest(); });
  on("/api/clock",    HTTP_POST, [this]() { postClock(); });
  on("/api/stop",     HTTP_POST, [this]() { sys.irr.stopAll(StopReason::USER); ok(); });
  on("/api/faults/clear", HTTP_POST, [this]() { sys.water.clearFaults(); ok(); });
  on("/api/cost/reset",   HTTP_POST, [this]() { sys.cost.reset(); ok(); });
  on("/api/wifi/reset",   HTTP_POST, [this]() { ok(); sys.net.resetToSetup(); });
  on("/api/factory",      HTTP_POST, [this]() { ok(); sys.factoryReset(); });
  on("/api/reboot",       HTTP_POST, [this]() { ok(); sys.reboot(); });
  on("/api/log/clear",    HTTP_POST, [this]() { sys.log.clear(); ok(); });
}

// ---------------- zones ----------------
void Api::postZone() {
  long id = num("id");
  if (id < 1 || id > ZONES) { err("id"); return; }
  const uint8_t z = id - 1;
  ZoneCfg c = sys.cfg.z[z];                       // edit a copy; apply only if everything is valid
  const uint8_t oldMode = c.m, oldEn = c.en;

  if (has("name")) {
    String v = str("name");
    v.trim();
    char buf[sizeof(c.name)];
    if (utf8Copy(buf, sizeof(buf), v.c_str(), 24) > 0) copyStr(c.name, sizeof(c.name), buf);
  }
  if (has("en")) c.en = num("en") ? 1 : 0;
  if (has("m")) c.m = (uint8_t)clampv<long>(num("m"), 0, 2);
  if (has("cold")) {
    long v = num("cold");
    if (v < (long)ceilf(sys.cfg.g.frost) || v > 30) { err("cold"); return; }
    c.cold = (int8_t)v;
  }
  if (has("cov")) c.cov = num("cov") ? 1 : 0;

  // plant / soil / sprinkler: "<group>.<field>"; a preset id is at most 7 characters
  if (has("plant.p")) copyStr(c.plant.p, sizeof(c.plant.p), str("plant.p").c_str());
  if (has("plant.kc")) c.plant.kc = clampv(fnum("plant.kc"), 0.1f, 1.5f);
  if (has("plant.root")) c.plant.root = (uint16_t)clampv<long>(lroundf(fnum("plant.root")), 5, 150);
  if (has("plant.lo")) c.plant.lo = (uint8_t)clampv<long>(lroundf(fnum("plant.lo")), 5, 90);
  if (has("plant.hi")) c.plant.hi = (uint8_t)clampv<long>(lroundf(fnum("plant.hi")), 10, 100);
  // plant age: Kc at planting / at the end, roots at planting, stage lengths "init,dev,mid,late", planting date
  if (has("plant.kci")) c.plant.kcIni = clampv(fnum("plant.kci"), 0.1f, 1.5f);
  if (has("plant.kce")) c.plant.kcEnd = clampv(fnum("plant.kce"), 0.1f, 1.5f);
  if (has("plant.oe")) c.plant.offEnd = num("plant.oe") ? 1 : 0;
  if (has("plant.ri")) c.plant.rootIni = (uint16_t)clampv<long>(lroundf(fnum("plant.ri")), 1, 150);
  if (has("plant.l")) {
    String v = str("plant.l");
    long a[4] = {0, 0, 0, 0};
    sscanf(v.c_str(), "%ld,%ld,%ld,%ld", &a[0], &a[1], &a[2], &a[3]);
    for (int i = 0; i < 4; i++) c.plant.len[i] = (uint16_t)clampv<long>(a[i], 0, 365);
  }
  if (has("plant.pd")) {
    String v = str("plant.pd");
    c.plant.planted = v.length() ? TimeUtil::parseYmd(v.c_str()) : 0;
    if (v.length() && !c.plant.planted) { err("date"); return; }
  }
  if (has("soil.p")) copyStr(c.soil.p, sizeof(c.soil.p), str("soil.p").c_str());
  if (has("soil.fc")) c.soil.fc = (uint8_t)clampv<long>(lroundf(fnum("soil.fc")), 5, 60);
  if (has("soil.wp")) c.soil.wp = (uint8_t)clampv<long>(lroundf(fnum("soil.wp")), 1, 40);
  if (has("soil.inf")) c.soil.inf = (uint16_t)clampv<long>(lroundf(fnum("soil.inf")), 1, INF_MAX);
  if (has("soil.depth")) c.soil.depth = (uint16_t)clampv<long>(lroundf(fnum("soil.depth")), 2, 300);
  if (has("spr.p")) copyStr(c.spr.p, sizeof(c.spr.p), str("spr.p").c_str());
  // Sprinkler: area, then flow OR rate (the rate sets the flow for this area). Area alone keeps the flow.
  if (has("spr.area")) c.spr.area = clampv(fnum("spr.area"), AREA_MIN, AREA_MAX);
  if (has("spr.flow")) c.spr.flow = clampv(fnum("spr.flow"), FLOW_MIN, FLOW_MAX);
  else if (has("spr.rate")) c.spr.flow = clampv(fnum("spr.rate"), RATE_MIN, RATE_MAX) * c.spr.area / 60.0f;
  if (has("spr.eff")) c.spr.eff = (uint8_t)clampv<long>(lroundf(fnum("spr.eff")), 30, 100);
  if (c.plant.lo >= c.plant.hi) { err("band"); return; }
  if (c.soil.wp >= c.soil.fc) { err("soil"); return; }

  sys.cfg.z[z] = c;
  if (!sys.cfg.saveZone(z)) { err("save"); return; }

  if (oldEn && !c.en) sys.irr.stopZone(z, StopReason::USER);
  if (c.m == (uint8_t)ZoneMode::OPEN_LOOP && oldMode != c.m) sys.wb.reset(z);

  // Back to Schedule: entries without a length get 15 min
  int fill = 0;
  if (c.m == (uint8_t)ZoneMode::SCHEDULE && oldMode != c.m) {
    for (uint8_t i = 0; i < sys.cal.w[z].n; i++) {
      if (!sys.cal.w[z].e[i].sec) { sys.cal.w[z].e[i].sec = SCHEDULE_DEFAULT_S; fill++; }
    }
    if (fill) sys.cal.saveWaterings(z);
  }
  JsonDocument d;
  d["ok"] = 1;
  if (fill) d["fill"] = fill;
  send(d);
}

void Api::postSched() {
  long id = num("id");
  if (id < 1 || id > ZONES) { err("id"); return; }
  const uint8_t z = id - 1;
  const uint16_t sid = (uint16_t)num("sid");
  if (num("del")) { sys.cal.delWatering(z, sid); ok(); return; }

  Watering e{};
  e.sid = sid;
  e.d = (uint8_t)(num("d") & 127);
  long s = num("s"), sec = num("sec");
  if (!has("s") || s < 0 || s >= 1440) { err("bad"); return; }
  e.s = (uint16_t)s;
  e.sec = sec <= 0 ? 0 : (uint16_t)clampv<long>(sec, MIN_RUN_S, MAX_RUN_S);
  if (!e.d) {
    e.dt = TimeUtil::parseYmd(str("dt").c_str());
    if (!e.dt) { err("bad"); return; }
  }
  if (const Watering* old = sys.cal.findWatering(z, sid)) e.fired = old->fired;
  sys.sched.markPastToday(e);                      // a new time earlier today does not fire now
  if (!sys.cal.putWatering(z, e)) { err("full"); return; }
  ok();
}

void Api::postDsch() {
  long id = num("id");
  if (id < 0 || id > ZONES) { err("id"); return; }
  const uint8_t list = (uint8_t)id;               // 0 = all zones
  const uint16_t sid = (uint16_t)num("sid");
  if (num("del")) { sys.cal.delDnd(list, sid); ok(); return; }

  DndWindow w{};
  w.sid = sid;
  w.d = (uint8_t)(num("d") & 127);
  long s = num("s"), e = num("e");
  if (s < 0 || s >= 1440 || e < 0 || e > 1440) { err("bad"); return; }
  w.s = (uint16_t)s;
  w.e = (uint16_t)e;
  if (!w.d) {
    w.dt = TimeUtil::parseYmd(str("dt").c_str());
    w.de = has("de") ? TimeUtil::parseYmd(str("de").c_str()) : w.dt;
    if (!w.dt || !w.de || TimeUtil::localToUnix(w.de, w.e) <= TimeUtil::localToUnix(w.dt, w.s)) { err("bad"); return; }
  } else if (w.s == w.e) {
    err("bad");
    return;
  }
  if (!sys.cal.putDnd(list, w)) { err("full"); return; }
  sys.irr.dndChanged(true);                       // a window that covers a run now stops it
  ok();
}

void Api::postDnd() {
  long id = num("id");
  if (id < 0 || id > ZONES) { err("id"); return; }
  uint32_t until = strtoul(str("until").c_str(), nullptr, 10);
  sys.dnd.set((uint8_t)id, until);
  if (until) sys.irr.dndChanged(true);
  ok();
}

// ---------------- actions ----------------
void Api::postWater() {
  long id = num("id");
  long sec = num("sec");
  WaterAnswer a = sys.irr.waterNow(id >= 1 && id <= ZONES ? id - 1 : 255, sec > 0 ? (uint32_t)sec : 0,
                                   num("def") != 0, num("dnd") != 0, num("force") != 0);
  JsonDocument d;
  d["r"] = a.r;
  if (a.k) {
    char k[2] = {a.k, 0};
    d["k"] = k;
    if (a.u) d["u"] = a.u;
  }
  if (!strcmp(a.r, "COLD")) {
    d["c"] = a.c;
    d["lim"] = a.lim;
  }
  send(d);
}

void Api::postGlobal() {
  if (has("off")) sys.setAllOff(num("off") != 0);
  ok();
}

void Api::postClock() {
  uint32_t t = strtoul(str("t").c_str(), nullptr, 10);
  if (t < RTC_MIN_VALID_UNIX || t > DND_FOREVER) { err("time"); return; }
  int32_t diff = (int32_t)(t - sys.clock.unixNow());
  if (!sys.clock.rtcOk() || diff > 1 || diff < -1) sys.clock.setUnix(t);
  ok();
}

void Api::postTest() {
  String o = str("o");
  int z = (int)num("z");
  if (o == "stop") {
    sys.test.stop(str("t").c_str(), z);
    JsonDocument d;
    d["r"] = "OK";
    send(d);
    return;
  }
  TestAnswer a = sys.test.start(o.c_str(), z);
  if (!a.r) { err(o == "valve" || o == "pump" ? "z" : "o"); return; }
  JsonDocument d;
  d["r"] = a.r;
  if (!strcmp(a.r, "OK")) d["sec"] = a.sec;
  send(d);
}

// ---------------- settings ----------------
void Api::postSettings() {
  GlobalCfg g = sys.cfg.g;                         // edit a copy
  const float oldLat = g.lat, oldLon = g.lon;

  if (has("name")) {
    String v = str("name");
    v.trim();
    if (!validHostname(v.c_str())) { err("host"); return; }
    copyStr(g.name, sizeof(g.name), v.c_str());
  }
  if (has("frost")) g.frost = clampv(fnum("frost"), -10.0f, 10.0f);
  if (has("cap")) g.cap = (uint16_t)clampv<long>(num("cap"), 5, MAX_ZONE_DAY_S / 60);
  if (has("rainmm")) g.rainmm = clampv(fnum("rainmm"), 0.0f, 20.0f);
  if (has("qto")) g.qto = (uint16_t)clampv<long>(num("qto"), 10, 600);
  if (has("lat")) g.lat = clampv(fnum("lat"), -90.0f, 90.0f);
  if (has("lon")) g.lon = clampv(fnum("lon"), -180.0f, 180.0f);
  if (has("tankE")) g.tankE = (uint16_t)clampv<long>(num("tankE"), 5, 400);
  if (has("tankF")) g.tankF = (uint16_t)clampv<long>(num("tankF"), 2, 400);
  if (g.tankF + 2 > g.tankE) { err("tank"); return; }
  if (has("cmode")) g.cmode = (uint8_t)clampv<long>(num("cmode"), 0, 2);
  if (has("cpw")) g.cpw = clampv(fnum("cpw"), 0.0f, 100000.0f);
  if (has("cpe")) g.cpe = clampv(fnum("cpe"), 0.0f, 10000.0f);
  if (has("cpp")) g.cpp = (uint16_t)clampv<long>(num("cpp"), 0, 5000);
  if (has("cur")) {
    String v = str("cur");
    v.trim();
    char buf[sizeof(g.cur)];
    if (utf8Copy(buf, sizeof(buf), v.c_str(), 4) > 0) copyStr(g.cur, sizeof(g.cur), buf);
  }
  if (has("clog")) g.clog = (uint8_t)(num("clog") & (1 | 2 | 4 | 16));
  if (has("btn")) g.btn = num("btn") ? 1 : 0;
  if (has("buzz")) g.buzz = num("buzz") ? 1 : 0;

  const bool renamed = strcmp(g.name, sys.cfg.g.name) != 0;
  sys.cfg.g = g;
  if (!sys.cfg.saveGlobal()) { err("save"); return; }

  // A higher frost limit raises the zone minimums that would fall below it
  if (has("frost")) {
    for (uint8_t z = 0; z < ZONES; z++) sys.cfg.saveZone(z);   // saveZone clamps cold >= frost
  }
  if (num("cr")) sys.cost.reset();
  if (renamed) sys.net.setHostname(g.name);
  if (g.lat != oldLat || g.lon != oldLon) sys.wx.requestRefresh();
  sys.applySettings();
  ok();
}

void Api::postCal() {
  long s = num("s");
  if (s < 1 || s > ZONES) { err("s"); return; }
  const uint8_t z = s - 1;
  SoilCal c = sys.cfg.g.cal[z];
  String which = has("set") ? str("set") : has("dry") ? String("dry") : has("wet") ? String("wet") : String("");
  if (which != "dry" && which != "wet") { err("set"); return; }

  long v;
  if (has("set")) {
    if (sys.soil[z].fault()) { err("raw"); return; }
    v = sys.soil[z].raw();
  } else {
    v = num(which.c_str());
  }
  if (v < 0 || v > 4095) { err("raw"); return; }
  if (which == "dry") c.dry = (uint16_t)v; else c.wet = (uint16_t)v;
  if (c.dry <= c.wet) { err("order"); return; }

  sys.cfg.g.cal[z] = c;
  if (!sys.cfg.saveGlobal()) { err("save"); return; }
  sys.applySettings();
  ok();
}

// JSON answers of the GET routes (field names are the UI's, see docs/API.md)
#include "net/Api.h"
#include <WiFi.h>
#include <LittleFS.h>
#include <nvs.h>
#include "System.h"
#include "util/TimeUtil.h"

namespace {
// A float field, or null when unknown
void setNum(JsonObject o, const char* k, float v, int dec) {
  if (isnan(v)) o[k] = nullptr;
  else o[k] = roundTo(v, dec);
}

void ymdField(JsonObject o, const char* k, uint32_t ymd) {
  char b[11];
  TimeUtil::formatYmd(ymd, b);
  o[k] = b;
}

void addWatering(JsonArray a, const Watering& e) {
  JsonObject o = a.add<JsonObject>();
  o["sid"] = e.sid;
  o["d"] = e.d;
  o["s"] = e.s;
  o["sec"] = e.sec;
  if (!e.d) ymdField(o, "dt", e.dt);
}

void addDnd(JsonArray a, const DndWindow& e) {
  JsonObject o = a.add<JsonObject>();
  o["sid"] = e.sid;
  o["d"] = e.d;
  o["s"] = e.s;
  o["e"] = e.e;
  if (!e.d) { ymdField(o, "dt", e.dt); ymdField(o, "de", e.de ? e.de : e.dt); }
}

void addWaterings(JsonArray a, uint8_t z) { for (uint8_t i = 0; i < sys.cal.w[z].n; i++) addWatering(a, sys.cal.w[z].e[i]); }
void addDnds(JsonArray a, uint8_t list) { for (uint8_t i = 0; i < sys.cal.dnd[list].n; i++) addDnd(a, sys.cal.dnd[list].e[i]); }
}  // namespace

void Api::buildState(JsonDocument& d) {
  const GlobalCfg& g = sys.cfg.g;
  const uint32_t now = sys.clock.unixNow();
  char lt[20];
  TimeUtil::formatLocal(now, lt);

  d["name"] = g.name;
  d["ver"] = FW_VERSION;
  d["hw"] = HW_NAME;
  d["t"] = now;
  d["ok"] = sys.clock.valid() ? 1 : 0;
  d["rtc"] = sys.clock.rtcOk() ? 1 : 0;                // 0 with ok = 1: running on the internet / phone time
  d["lt"] = lt;
  d["up"] = millis() / 1000;

  JsonObject w = d["wifi"].to<JsonObject>();
  w["ssid"] = WiFi.SSID();
  w["rssi"] = WiFi.RSSI();

  JsonObject env = d["env"].to<JsonObject>();
  env["ok"] = sys.env.valid() ? 1 : 0;
  setNum(env, "c", sys.env.temperatureC(), 1);
  setNum(env, "h", sys.env.humidity(), 0);
  if (!sys.env.valid()) {                              // the forecast temperature used instead
    bool web; float w = sys.airTempC(&web);
    if (web) setNum(env, "w", w, 1);
  }

  JsonObject tank = d["tank"].to<JsonObject>();
  tank["low"] = sys.tank.low() ? 1 : 0;
  if (sys.tank.noEcho()) tank["ne"] = 1;               // low because the sensor has no echo (not the water level)
  setNum(tank, "cm", sys.tank.distanceCm(), 1);
  setNum(tank, "raw", sys.tank.rawCm(), 1);           // the last ping, unfiltered: for calibrating
  int pct = sys.tank.percent();
  if (pct < 0) tank["pct"] = nullptr; else tank["pct"] = pct;
  d["rain"] = sys.rain.wet() ? 1 : 0;

  JsonObject go = d["g"].to<JsonObject>();
  go["off"] = g.off;
  go["frost"] = roundTo(g.frost, 1);
  go["dnd"] = sys.dnd.until(0);
  go["blk"] = Rules::blockName(Rules::globalBlock());

  JsonArray tst = d["tst"].to<JsonArray>();
  TestRunner::Item items[ZONES + 3];
  uint8_t n = sys.test.list(items, ZONES + 3);
  for (uint8_t i = 0; i < n; i++) {
    JsonObject o = tst.add<JsonObject>();
    o["o"] = items[i].o;
    o["z"] = items[i].z;
    o["u"] = items[i].u;
  }

  d["pump"] = sys.water.pumpOn() ? 1 : 0;
  d["az"] = sys.water.activeZone();
  d["el"] = sys.water.runElapsedS();
  d["tot"] = sys.water.runTotalS();
  d["flt"] = sys.water.faulted() ? 1 : 0;

  // Memory and storage: the lowest free heap since boot and the largest free block are what tell
  // whether the controller is safe; flash use of the log and settings
  JsonObject mem = d["mem"].to<JsonObject>();
  mem["free"] = ESP.getFreeHeap();
  mem["min"] = ESP.getMinFreeHeap();
  mem["blk"] = ESP.getMaxAllocHeap();
  mem["fs"] = (uint32_t)LittleFS.usedBytes();
  mem["fsT"] = (uint32_t)LittleFS.totalBytes();
  nvs_stats_t ns;
  if (nvs_get_stats(NULL, &ns) == ESP_OK) { mem["nvs"] = ns.used_entries; mem["nvsT"] = ns.total_entries; }

  JsonArray zs = d["z"].to<JsonArray>();
  for (uint8_t z = 0; z < ZONES; z++) {
    const ZoneCfg& c = sys.cfg.z[z];
    JsonObject o = zs.add<JsonObject>();
    Rules::ZoneStatus st = Rules::zoneStatus(z);
    o["name"] = c.name;
    o["en"] = c.en;
    o["m"] = c.m;
    o["st"] = Rules::stName(st.st);
    o["why"] = st.why;
    uint32_t zd = sys.dnd.until(z + 1);
    o["dnd"] = zd > now ? zd : 0;
    o["cov"] = c.cov;
    float s;
    if (sys.dose.soil(z, s)) o["soil"] = (int)lroundf(s); else o["soil"] = nullptr;
    if (sys.soil[z].fault()) o["raw"] = nullptr; else o["raw"] = sys.soil[z].raw();   // live, for calibrating
    o["sf"] = (sys.soil[z].fault() || (c.m == (uint8_t)ZoneMode::CLOSED_LOOP && sys.dose.fallback(z))) ? 1 : 0;
    o["lo"] = c.plant.lo;
    o["hi"] = c.plant.hi;
    // plant age today: stage, day since planting, Kc, root depth used (capped by the soil depth)
    Settings::Growth gr = Settings::growth(c);
    static const char* const ST[] = {"EST", "NOT_YET", "INIT", "DEV", "MID", "LATE", "OVER"};
    JsonObject go2 = o["gr"].to<JsonObject>();
    go2["st"] = ST[gr.st];
    if (gr.st != Settings::ESTABLISHED || c.plant.planted) go2["d"] = gr.day;
    go2["kc"] = roundTo(gr.kc, 2);
    go2["r"] = roundTo(gr.rootCm, 0);
    uint32_t su = sys.irr.soakUntil(z);
    if (su) o["su"] = su;                            // soaking: the next burst at this time

    const LastRun& l = sys.log.last(z);
    if (l.valid) {
      JsonObject lo = o["last"].to<JsonObject>();
      lo["t"] = l.t;
      lo["sec"] = l.sec;
      lo["r"] = l.r;
    } else {
      o["last"] = nullptr;
    }

    NextRun nx = c.en ? sys.sched.next(z) : NextRun{false, 0, 0, "", -1};
    if (nx.valid) {
      JsonObject no = o["next"].to<JsonObject>();
      no["t"] = nx.t;
      no["sec"] = nx.sec;
      if (nx.sec < 0 && nx.est >= 0) no["est"] = nx.est;   // loop modes: expected length from the forecast
      if (*nx.x) no["x"] = nx.x;
    } else {
      o["next"] = nullptr;
    }

    Deficit df;
    if (sys.dose.deficit(z, df)) {
      JsonObject dd = o["def"].to<JsonObject>();
      dd["sec"] = df.sec;
      dd["mm"] = df.mm;
    } else {
      o["def"] = nullptr;
    }
  }
}

void Api::buildZone(JsonDocument& d, uint8_t z) {
  const ZoneCfg& c = sys.cfg.z[z];
  d["id"] = z + 1;
  d["name"] = c.name;
  d["en"] = c.en;
  d["m"] = c.m;
  d["cold"] = c.cold;
  d["cov"] = c.cov;

  JsonObject p = d["plant"].to<JsonObject>();
  p["p"] = c.plant.p;
  p["kc"] = roundTo(c.plant.kc, 2);
  p["root"] = c.plant.root;
  p["lo"] = c.plant.lo;
  p["hi"] = c.plant.hi;
  p["kci"] = roundTo(c.plant.kcIni, 2);
  p["oe"] = c.plant.offEnd;
  p["kce"] = roundTo(c.plant.kcEnd, 2);
  p["ri"] = c.plant.rootIni;
  JsonArray pl = p["l"].to<JsonArray>();
  for (uint16_t l : c.plant.len) pl.add(l);
  if (c.plant.planted) ymdField(p, "pd", c.plant.planted); else p["pd"] = "";
  JsonObject so = d["soil"].to<JsonObject>();
  so["p"] = c.soil.p;
  so["fc"] = c.soil.fc;
  so["wp"] = c.soil.wp;
  so["inf"] = c.soil.inf;
  so["depth"] = c.soil.depth;
  JsonObject sp = d["spr"].to<JsonObject>();
  sp["p"] = c.spr.p;
  sp["flow"] = roundTo(c.spr.flow, 2);
  sp["area"] = roundTo(c.spr.area, 2);
  sp["rate"] = roundTo(Settings::rate(c), 1);            // follows from flow and area
  sp["eff"] = c.spr.eff;

  addWaterings(d["sch"].to<JsonArray>(), z);
  addDnds(d["dsch"].to<JsonArray>(), z + 1);
  addDnds(d["gdsch"].to<JsonArray>(), 0);

  JsonArray h = d["hist"].to<JsonArray>();
  for (uint8_t i = 0; i < sys.log.histCount(z); i++) {
    char row[100];
    copyStr(row, sizeof(row), sys.log.hist(z, i));
    char* f[9];
    uint8_t n = splitCsv(row, f, 9);
    JsonArray r = h.add<JsonArray>();
    for (uint8_t k = 0; k < n; k++) r.add(f[k]);
  }
}

void Api::buildCalendar(JsonDocument& d) {
  JsonArray zs = d["z"].to<JsonArray>();
  for (uint8_t z = 0; z < ZONES; z++) {
    JsonObject o = zs.add<JsonObject>();
    o["id"] = z + 1;
    o["name"] = sys.cfg.z[z].name;
    o["m"] = sys.cfg.z[z].m;
    addWaterings(o["sch"].to<JsonArray>(), z);
    addDnds(o["dsch"].to<JsonArray>(), z + 1);
  }
  addDnds(d["g"].to<JsonArray>(), 0);
}

void Api::buildSettings(JsonDocument& d) {
  const GlobalCfg& g = sys.cfg.g;
  d["name"] = g.name;
  d["frost"] = roundTo(g.frost, 1);
  d["cap"] = g.cap;
  d["rainmm"] = roundTo(g.rainmm, 1);
  d["qto"] = g.qto;
  d["lat"] = roundTo(g.lat, 3);
  d["lon"] = roundTo(g.lon, 3);
  d["tankE"] = g.tankE;
  d["tankF"] = g.tankF;

  JsonArray cal = d["soilCal"].to<JsonArray>();
  for (uint8_t z = 0; z < ZONES; z++) {
    JsonObject o = cal.add<JsonObject>();
    o["dry"] = g.cal[z].dry;
    o["wet"] = g.cal[z].wet;
  }

  d["cmode"] = g.cmode;
  d["cpw"] = roundTo(g.cpw, 2);
  d["cpe"] = roundTo(g.cpe, 2);
  d["cpp"] = g.cpp;
  d["cur"] = g.cur;
  d["clog"] = g.clog;
  d["btn"] = g.btn;
  d["buzz"] = g.buzz;

  static const char* const KEYS[CP_N] = {"d", "w", "m", "y"};
  JsonObject tot = d["tot"].to<JsonObject>();
  for (uint8_t p = 0; p < CP_N; p++) {
    const CostCounter& c = sys.cost.counter(p);
    JsonObject o = tot[KEYS[p]].to<JsonObject>();
    o["sec"] = c.sec;
    o["l"] = roundTo(c.litres, 2);
    o["f"] = c.f;
  }
}

void Api::buildWeather(JsonDocument& d) {
  WxSnapshot s = sys.wx.snapshot();
  LTime now;
  uint32_t today = TimeUtil::toLocal(sys.clock.unixNow(), now) ? now.ymd : 0;
  d["ok"] = s.ok ? 1 : 0;
  d["t"] = s.t;
  d["lat"] = roundTo(sys.cfg.g.lat, 3);
  d["lon"] = roundTo(sys.cfg.g.lon, 3);
  JsonArray a = d["d"].to<JsonArray>();
  for (uint8_t i = 0; i < s.n && a.size() < 2; i++) {
    const WxDay& w = s.d[i];
    if (w.ymd < today) continue;                     // yesterday is only for the water balance
    JsonObject o = a.add<JsonObject>();
    ymdField(o, "date", w.ymd);
    setNum(o, "tmax", w.tmax, 1);
    setNum(o, "tmin", w.tmin, 1);
    setNum(o, "et0", w.et0, 1);
    setNum(o, "rain", w.rain, 1);
  }
}

// Host tests: the whole firmware runs against the stub hardware (test/host/stubs).
// Time is simulated: host::ms drives millis() and the RTC.
#include <time.h>
#include <string>
#include <Preferences.h>
#include "System.h"
#include "util/Nvs.h"
#include "logic/Rules.h"
#include "logic/Weather.h"

namespace host {
extern uint32_t ms;
extern int pinLevel[40], analogValue[40];
extern bool bmeOk;
extern float bmeT;
extern uint32_t rtcBase, rtcBaseMs, rtcGlitch;
extern uint8_t rtcGlitchReads;
extern bool rtcValid, rtcBroken;
extern unsigned long pulseUs;
std::map<std::string, std::string>& files();
std::map<std::string, std::vector<uint8_t>>& nvs();
}  // namespace host

static int fails = 0, checks = 0;
namespace show {
template <typename T> std::string str(const T& v) { return std::to_string(v); }
inline std::string str(const std::string& v) { return v; }
}  // namespace show
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_EQ(a, b) do { checks++; auto _a = (a); auto _b = (b); if (!(_a == _b)) { fails++; \
  printf("FAIL %s:%d  %s == %s  (%s vs %s)\n", __FILE__, __LINE__, #a, #b, show::str(_a).c_str(), show::str(_b).c_str()); } } while (0)

static WebServer srv(80);

static void tick(uint32_t msTotal) {
  for (uint32_t t = 0; t < msTotal; t += 10) {
    host::ms += 10;
    sys.loop();
  }
}

// Jump the clock forward without running every 10 ms (long waits); runs a few passes after
static void jump(uint32_t msTotal) {
  host::ms += msTotal;
  tick(2000);
}

static JsonDocument call(HTTPMethod m, const std::string& path, std::map<std::string, std::string> args = {}) {
  std::string body = srv.request(m, path, args);
  JsonDocument d;
  deserializeJson(d, body);
  return d;
}
static JsonDocument post(const std::string& path, std::map<std::string, std::string> args = {}) { return call(HTTP_POST, path, args); }
static JsonDocument state() { return call(HTTP_GET, "/api/state"); }

static bool on(uint8_t pin) { return host::pinLevel[pin] == HIGH; }
static std::string logAll() {                     // every month file, oldest first
  std::string all;
  for (auto& kv : host::files()) if (kv.first.rfind("/log/", 0) == 0) all += kv.second;
  return all;
}
static bool logHas(const char* s) { return logAll().find(s) != std::string::npos; }
static int logCount(const char* s) {
  std::string a = logAll();
  int n = 0;
  for (size_t p = a.find(s); p != std::string::npos; p = a.find(s, p + 1)) n++;
  return n;
}
static unsigned long usFor(float cm) { return (unsigned long)(cm * 2 / ((331.3f + 0.606f * 15) / 10000.0f)); }   // 15 C air
static int rawFor(float pct) { return (int)(2800 - (2800 - 1200) * pct / 100.0f); }   // default calibration

static uint32_t utc(int y, int mo, int d, int h, int mi) {
  tm t{};
  t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi;
  return (uint32_t)timegm(&t);
}

// ---------------------------------------------------------------- unit tests
static void testTime() {
  TimeUtil::begin();
  CHECK_EQ(TimeUtil::parseYmd("2026-02-30"), 0u);
  CHECK_EQ(TimeUtil::parseYmd("2026-10-30"), 20261030u);
  CHECK_EQ((int)TimeUtil::weekday(20261030), 4);                 // Friday (0 = Monday)
  CHECK_EQ(TimeUtil::addDays(20261231, 1), 20270101u);
  CHECK_EQ(TimeUtil::addDays(20240301, -1), 20240229u);          // leap year
  CHECK_EQ(TimeUtil::localToUnix(20261030, 600), utc(2026, 10, 30, 9, 0));   // CET = UTC+1
  CHECK_EQ(TimeUtil::localToUnix(20260715, 600), utc(2026, 7, 15, 8, 0));    // CEST = UTC+2
  CHECK_EQ(TimeUtil::periodStart(TimeUtil::Period::WEEK, utc(2026, 10, 30, 9, 0)), utc(2026, 10, 25, 23, 0));   // Monday 26 Oct 00:00 local
  CHECK_EQ(TimeUtil::periodStart(TimeUtil::Period::MONTH, utc(2026, 10, 30, 9, 0)), utc(2026, 9, 30, 22, 0));   // 1 Oct 00:00 CEST
  LTime l;
  CHECK(TimeUtil::toLocal(utc(2026, 10, 30, 9, 30), l) && l.mod == 630 && l.wd == 4);
}

static void testDndWindows() {
  DndList l{};
  l.n = 2;
  l.e[0] = DndWindow{1, (uint8_t)(1 << 4), 1320, 360, 0, 0};             // Fridays 22:00 - 06:00 next day
  l.e[1] = DndWindow{2, 0, 720, 600, 20261102, 20261103};               // one-off: 2 Nov 12:00 - 3 Nov 10:00
  CHECK(Dnd::inWindows(l, TimeUtil::localToUnix(20261030, 23 * 60)));   // Fri 23:00
  CHECK(Dnd::inWindows(l, TimeUtil::localToUnix(20261031, 2 * 60)));    // Sat 02:00, from Friday's window
  CHECK(!Dnd::inWindows(l, TimeUtil::localToUnix(20261031, 7 * 60)));   // Sat 07:00
  CHECK(!Dnd::inWindows(l, TimeUtil::localToUnix(20261030, 21 * 60)));  // Fri 21:00
  CHECK(Dnd::inWindows(l, TimeUtil::localToUnix(20261103, 9 * 60)));    // inside the one-off
  CHECK(!Dnd::inWindows(l, TimeUtil::localToUnix(20261103, 10 * 60)));  // its end is exclusive
}

static void testUtil() {
  char b[97];
  CHECK_EQ(utf8Copy(b, sizeof(b), "Herb bed \xF0\x9F\x8C\xB5", 24), (size_t)10);   // emoji counts as one
  CHECK_EQ(utf8Copy(b, sizeof(b), "abcdefghijklmnopqrstuvwxyz", 24), (size_t)24);
  CHECK(validHostname("kert-1") && !validHostname("-kert") && !validHostname("kért") && !validHostname(""));
  CHECK(fabsf(Weather::hargreavesEt0(6, 14, 47.5f, 303) - 1.2f) < 0.6f);   // late October, Budapest: about 1 mm/day
}

// ---------------------------------------------------------------- system tests
static void boot() {
  host::rtcBase = utc(2026, 10, 30, 9, 0);    // Friday 10:00 local
  host::rtcBaseMs = host::ms;
  host::rtcValid = true;
  for (uint8_t z = 0; z < ZONES; z++) host::analogValue[PIN_SOIL[z]] = rawFor(46);
  host::pinLevel[PIN_RAIN_OUT] = HIGH;          // rain sensor dry (pull-up)
  Preferences p;
  p.begin("wifi");
  p.putString("ssid", "TestNet");
  p.putString("pass", "password1");
  p.end();
  sys.begin();
  sys.api.routes(srv);
  // The tests below were written for these zone values (lawn 0.9 / 15 cm / 35-50 %, loam 27/12/13,
  // 20 mm/h at 70 % on 1 m2); the 1.2 defaults are the researched presets, tested in testGrowth
  for (uint8_t z = 0; z < ZONES; z++) {
    ZoneCfg& c = sys.cfg.z[z];
    c.plant = PlantParams{"lawn", 0.9f, 15, 35, 50, 0.9f, 0.9f, 15, {0, 0, 0, 0}, 0};
    c.soil = SoilParams{"loam", 27, 12, 13, 30};
    c.spr = SprParams{"mist", 20.0f / 60.0f, 1.0f, 70};
    sys.cfg.saveZone(z);
  }
  tick(3000);                                 // sensors settle, Wi-Fi connects
}

static void testBoot() {
  CHECK(sys.net.mode() == WifiPortal::Mode::STA);
  CHECK(!on(PIN_PUMP));
  for (uint8_t z = 0; z < ZONES; z++) CHECK(!on(PIN_VALVE[z]));
  JsonDocument s = state();
  CHECK_EQ(std::string(s["name"] | ""), std::string("irrigation"));
  CHECK_EQ((int)s["z"].size(), 4);
  CHECK(abs(s["z"][0]["raw"].as<int>() - rawFor(46)) < 40);                // the raw reading, live in the state (calibration)
  CHECK(!call(HTTP_GET, "/api/settings")["soilCal"][0]["raw"].is<int>());
  CHECK_EQ((int)s["ok"], 1);
  CHECK_EQ((int)s["tank"]["low"], 0);
  CHECK_EQ((int)s["tank"]["pct"], 66);                       // 30 cm in a 72..8 cm tank
  CHECK_EQ((int)s["z"][0]["soil"], 46);
  CHECK_EQ(std::string(s["z"][0]["st"] | ""), std::string("ON"));
  CHECK(s["z"][0]["last"].isNull());
}

static void testWaterNow() {
  JsonDocument r = post("/api/water", {{"id", "1"}, {"sec", "60"}});
  CHECK_EQ(std::string(r["r"] | ""), std::string("OK"));
  tick(200);
  CHECK(on(PIN_VALVE[0]) && !on(PIN_PUMP));                   // valve first
  tick(1500);
  CHECK(on(PIN_PUMP));                                        // pump 1.5 s later
  CHECK_EQ(std::string(state()["z"][0]["st"] | ""), std::string("WATERING"));

  // a second zone while busy is queued
  r = post("/api/water", {{"id", "2"}, {"sec", "60"}});
  CHECK_EQ(std::string(r["r"] | ""), std::string("BUSY"));
  CHECK_EQ(std::string(state()["z"][1]["st"] | ""), std::string("QUEUED"));

  tick(60000);
  CHECK(!on(PIN_PUMP) && on(PIN_VALVE[0]));                  // pump stops first
  tick(1600);
  CHECK(!on(PIN_VALVE[0]));
  CHECK(logHas(",1,MANUAL,46,46,60,0.33,DONE"));          // 20 mm/h on 1 m2 = 0.33 L/min
  CHECK_EQ((int)state()["z"][0]["last"]["sec"], 60);
  CHECK_EQ(std::string(state()["z"][0]["last"]["r"] | ""), std::string("DONE"));
  tick(2000);
  CHECK(on(PIN_VALVE[1]));                                    // zone 2 follows
  post("/api/stop");
  tick(2000);
  CHECK(!on(PIN_VALVE[1]) && !on(PIN_PUMP));
  CHECK(logHas(",2,MANUAL,46,46,"));
  CHECK(logHas("USER"));
  CHECK(call(HTTP_GET, "/api/settings")["tot"]["d"]["sec"].as<int>() >= 60);
}

static void testDndAndCold() {
  uint32_t now = sys.clock.unixNow();
  CHECK(post("/api/dnd", {{"id", "3"}, {"until", std::to_string(now + 3600)}})["ok"] == 1);
  JsonDocument r = post("/api/water", {{"id", "3"}, {"sec", "60"}});
  CHECK_EQ(std::string(r["r"] | ""), std::string("DND"));
  CHECK_EQ(std::string(r["k"] | ""), std::string("z"));
  r = post("/api/water", {{"id", "3"}, {"sec", "60"}, {"dnd", "1"}});
  CHECK_EQ(std::string(r["r"] | ""), std::string("OK"));
  CHECK_EQ(sys.dnd.until(3), 0u);                             // watering cleared the zone's DND
  tick(2000);
  // a DND set now stops the run
  post("/api/dnd", {{"id", "0"}, {"until", std::to_string(now + 600)}});
  tick(2000);
  CHECK(!on(PIN_VALVE[2]));
  CHECK(logHas(",3,MANUAL,") && logHas(",DND\n"));
  post("/api/dnd", {{"id", "0"}, {"until", "0"}});

  host::bmeT = 3;                                             // below the zone limit (4), above frost (2)
  tick(6000);
  r = post("/api/water", {{"id", "1"}, {"sec", "60"}});
  CHECK_EQ(std::string(r["r"] | ""), std::string("COLD"));
  CHECK_EQ((int)r["lim"], 4);
  r = post("/api/water", {{"id", "1"}, {"sec", "60"}, {"force", "1"}});
  CHECK_EQ(std::string(r["r"] | ""), std::string("OK"));
  post("/api/stop");
  host::bmeT = 1;                                             // frost
  tick(6000);
  CHECK_EQ(std::string(post("/api/water", {{"id", "1"}, {"sec", "60"}})["r"] | ""), std::string("FROST"));
  CHECK_EQ(std::string(state()["z"][0]["why"] | ""), std::string("FROST"));
  host::bmeT = 15;
  tick(6000);
}

static void testZoneSettings() {
  JsonDocument r = post("/api/zone", {{"id", "2"}, {"name", "Back lawn \xF0\x9F\x8C\xB1"}, {"m", "1"}});
  CHECK(r["ok"] == 1);
  CHECK_EQ(std::string(state()["z"][1]["name"] | ""), std::string("Back lawn \xF0\x9F\x8C\xB1"));
  CHECK_EQ(std::string(post("/api/zone", {{"id", "2"}, {"plant.lo", "60"}, {"plant.hi", "50"}})["e"] | ""), std::string("band"));
  CHECK_EQ(std::string(post("/api/zone", {{"id", "2"}, {"cold", "1"}})["e"] | ""), std::string("cold"));   // below frost
  CHECK_EQ(std::string(post("/api/settings", {{"name", "kert kert"}})["e"] | ""), std::string("host"));
  CHECK(post("/api/settings", {{"name", "kert"}})["ok"] == 1);
  CHECK_EQ(std::string(post("/api/cal", {{"s", "1"}, {"dry", "1000"}})["e"] | ""), std::string("order"));
  CHECK(post("/api/cal", {{"s", "1"}, {"dry", "2900"}})["ok"] == 1);
  CHECK(post("/api/cal", {{"s", "1"}, {"dry", "2800"}})["ok"] == 1);

  // deficit, zone 2 at 33 %: available (27-12)% x 15 cm = 22.5 mm; (50-33)% of it = 3.8 mm; / 70 % / 20 mm/h = 984 s
  host::analogValue[PIN_SOIL[1]] = rawFor(33);
  tick(1100);
  JsonDocument s = state();
  CHECK_EQ((int)s["z"][1]["soil"], 33);
  CHECK_EQ((int)s["z"][1]["def"]["sec"], 984);                // 1 s steps
  CHECK(fabs(s["z"][1]["def"]["mm"].as<float>() - 3.8f) < 0.01);
}

static void testSchedule() {
  LTime l;
  TimeUtil::toLocal(sys.clock.unixNow(), l);
  uint16_t at = l.mod + 2;
  // zone 4, Schedule mode, every day in 2 minutes, 120 s
  CHECK(post("/api/sched", {{"id", "4"}, {"sid", "0"}, {"d", "127"}, {"s", std::to_string(at)}, {"sec", "120"}})["ok"] == 1);
  // zone 2, closed loop, same minute: soil 33 % < lo 35 % -> the deficit dose (984 s), queued behind zone 4
  CHECK(post("/api/sched", {{"id", "2"}, {"sid", "0"}, {"d", "127"}, {"s", std::to_string(at)}, {"sec", "0"}})["ok"] == 1);
  // zone 1 with a DND window over that minute -> SKIP_DND
  post("/api/dsch", {{"id", "1"}, {"sid", "0"}, {"d", "127"}, {"s", std::to_string(at - 1)}, {"e", std::to_string(at + 30)}});
  post("/api/sched", {{"id", "1"}, {"sid", "0"}, {"d", "127"}, {"s", std::to_string(at)}, {"sec", "300"}});

  JsonDocument z4 = state()["z"][3];
  CHECK(!z4["next"].isNull());
  CHECK_EQ((int)z4["next"]["sec"], 120);
  CHECK_EQ(std::string(state()["z"][0]["next"]["x"] | ""), std::string("DND"));

  jump(130000);
  CHECK(logHas(",1,SCHEDULE,46,,0,,SKIP_DND"));
  CHECK_EQ((int)sys.water.activeZone(), 1);                   // same minute: zone order, zone 2 first
  CHECK_EQ(sys.water.runTotalS(), 984u);                      // closed loop: the deficit dose
  CHECK_EQ(std::string(state()["z"][3]["st"] | ""), std::string("QUEUED"));
  tick(990000);
  CHECK(logHas(",2,SCHEDULE,33,33,984,5.47,DONE"));
  CHECK_EQ((int)sys.water.activeZone(), 3);                   // then zone 4
  tick(125000);
  CHECK(logHas(",4,SCHEDULE,46,46,120,0.67,DONE"));
  CHECK(sys.irr.idle());
  // the next day's occurrence is predicted, not today's again
  CHECK(state()["z"][3]["next"]["t"].as<uint32_t>() > sys.clock.unixNow() + 23 * 3600);
}

static void press(uint32_t holdMs) {
  host::pinLevel[PIN_BUTTON] = HIGH;
  tick(holdMs);
  host::pinLevel[PIN_BUTTON] = LOW;
  tick(100);
}

static void testButton() {
  // short press: every deficit zone, driest first (zone 2 at 33 % before zone 1 at 46 %)
  press(200);
  CHECK_EQ((int)sys.water.activeZone(), 1);
  CHECK(sys.irr.queued(0));
  CHECK_EQ(sys.water.runTotalS(), 900u);                      // 984 s capped to 15 min
  // short press while watering: stop everything, nothing starts
  press(200);
  tick(2000);
  CHECK(sys.irr.idle());
  CHECK_EQ((int)sys.cfg.g.off, 0);
  // long press: winter mode on, whatever was going on
  press(3200);
  CHECK_EQ((int)sys.cfg.g.off, 1);
  CHECK(logHas(",0,ALERT,WINTER_ON,,,,I"));
  CHECK_EQ(std::string(state()["z"][0]["st"] | ""), std::string("ALLOFF"));
  CHECK_EQ(std::string(post("/api/water", {{"id", "1"}, {"sec", "60"}})["r"] | ""), std::string("ALL_OFF"));
  // short press in winter mode: refused (low notes), winter stays on
  host::pinLevel[PIN_BUTTON] = HIGH;
  tick(200);
  host::pinLevel[PIN_BUTTON] = LOW;
  tick(60);
  CHECK(sys.fb.toneNow() == 1568);                            // G6: the refused "bonk"
  tick(400);
  CHECK_EQ((int)sys.cfg.g.off, 1);
  CHECK(!sys.water.busy());
  // long press again: winter mode off
  press(3200);
  CHECK_EQ((int)sys.cfg.g.off, 0);
  CHECK(logHas(",0,ALERT,WINTER_OFF,,,,I"));
  // the "Distance now" is the last ping, not a median
  host::pulseUs = usFor(50.0f);
  tick(250);
  CHECK(fabs(state()["tank"]["raw"].as<float>() - 50.0f) < 0.2f);
  CHECK(!call(HTTP_GET, "/api/settings")["tankNow"].is<float>());            // only in the state now
  host::pulseUs = 1750;
  tick(1000);
  // long press while watering: stops at press-down, winter on at 3 s
  press(200);
  CHECK(sys.water.busy());
  host::pinLevel[PIN_BUTTON] = HIGH;
  tick(500);
  CHECK(!sys.water.running());
  tick(2800);
  host::pinLevel[PIN_BUTTON] = LOW;
  tick(2000);
  CHECK_EQ((int)sys.cfg.g.off, 1);
  CHECK(sys.irr.idle());
  press(3200);                                                // and off again for the next tests
  CHECK_EQ((int)sys.cfg.g.off, 0);
}

static void testOutputTest() {
  JsonDocument r = post("/api/test", {{"o", "valve"}, {"z", "1"}});
  CHECK_EQ((int)r["sec"], 60);
  tick(100);
  CHECK(on(PIN_VALVE[0]) && !on(PIN_PUMP));
  r = post("/api/test", {{"o", "pump"}});
  CHECK(r["sec"].as<int>() >= 57 && r["sec"].as<int>() <= 58);
  tick(1600);
  CHECK(on(PIN_PUMP));
  CHECK_EQ((int)state()["tst"].size(), 2);
  // a zone may not water during a test: it waits in the queue
  CHECK_EQ(std::string(post("/api/water", {{"id", "3"}, {"sec", "60"}})["r"] | ""), std::string("BUSY"));
  post("/api/stop");                                          // clear that queue entry
  post("/api/test", {{"o", "stop"}, {"t", "valve"}, {"z", "1"}});
  tick(100);
  CHECK(!on(PIN_PUMP) && !on(PIN_VALVE[0]));                 // the pump never runs on closed valves
  r = post("/api/test", {{"o", "pump"}});                     // alone, 3 s
  CHECK_EQ((int)r["sec"], 3);
  tick(200);
  CHECK(on(PIN_PUMP));
  tick(3000);
  CHECK(!on(PIN_PUMP));
  CHECK(!sys.water.testing());
  // buzzer test: every note, low to high (a passive buzzer needs a tone, not DC)
  CHECK_EQ((int)post("/api/test", {{"o", "buzz"}})["sec"], 2);
  tick(100);
  CHECK_EQ((int)sys.fb.toneNow(), 1319);                      // E6 first
  tick(1000);
  CHECK_EQ((int)sys.fb.toneNow(), 4186);                      // C8 last
  tick(900);
  CHECK_EQ((int)sys.fb.toneNow(), 0);
}

static void testTankLow() {
  JsonDocument r = post("/api/water", {{"id", "4"}, {"sec", "300"}});   // zone 1 has a DND window now
  CHECK_EQ(std::string(r["r"] | ""), std::string("OK"));
  tick(3000);
  CHECK(on(PIN_PUMP));
  // the sensor misses echoes now and then: short dropouts change nothing
  for (int i = 0; i < 20; i++) { host::pulseUs = 0; tick(400); host::pulseUs = 1750; tick(400); }
  CHECK(on(PIN_PUMP));
  CHECK(!sys.tank.low());
  host::pulseUs = 0;                                          // no echo for 5 s -> low (fail-safe)
  tick(4000);
  CHECK(on(PIN_PUMP));
  tick(2000);
  CHECK(!on(PIN_PUMP));
  CHECK_EQ((int)state()["tank"]["ne"], 1);
  tick(2000);
  CHECK(logHas(",TANK_LOW\n"));
  CHECK_EQ(std::string(post("/api/water", {{"id", "4"}, {"sec", "60"}})["r"] | ""), std::string("TANK_LOW"));
  tick(6000);
  CHECK(logHas(",0,ALERT,TANK_LOW,,,,W"));
  host::pulseUs = 1750;
  tick(8000);
  CHECK(logHas(",0,ALERT,TANK_OK,,,,I"));
  CHECK(!sys.tank.low());
  // hysteresis: low below 10 %, OK again only above 15 %
  host::pulseUs = usFor(72 - 0.64f * 12);                     // 12 %: still OK
  tick(3000);
  CHECK(!sys.tank.low());
  host::pulseUs = usFor(72 - 0.64f * 8);                      // 8 %: low
  tick(3000);
  CHECK(sys.tank.low());
  host::pulseUs = usFor(72 - 0.64f * 12);                     // 12 %: stays low
  tick(3000);
  CHECK(sys.tank.low());
  host::pulseUs = usFor(72 - 0.64f * 17);                     // 17 %: OK
  tick(3000);
  CHECK(!sys.tank.low());
  // the shown level does not flicker on +-1 mm of noise
  host::pulseUs = usFor(40.0f);
  tick(4000);
  int p0 = (int)state()["tank"]["pct"];
  bool steady = true;
  for (int i = 0; i < 40; i++) {
    host::pulseUs = usFor(i % 2 ? 40.1f : 39.9f);
    tick(200);
    if ((int)state()["tank"]["pct"] != p0) steady = false;
  }
  CHECK(steady);
  // two bad echoes in a row (a reflection from the wall) do not move the level: median of 5
  host::pulseUs = usFor(40.0f);
  tick(2000);
  float c0 = sys.tank.filteredCm();
  host::pulseUs = usFor(15.0f);
  tick(400);                                                  // two pings
  host::pulseUs = usFor(40.0f);
  tick(400);
  CHECK(fabsf(sys.tank.filteredCm() - c0) < 0.3f);
  tick(1200);                                                 // the bad pings leave the window
  // a real change shows after three pings (600 ms), not after two
  host::pulseUs = usFor(30.0f);
  tick(390);                                                  // at most two pings
  CHECK(fabsf(sys.tank.filteredCm() - c0) < 0.3f);
  tick(400);
  CHECK(fabsf(sys.tank.filteredCm() - 30.0f) < 0.3f);
  host::pulseUs = 1750;
  tick(4000);
}

static void testCostRollover() {
  post("/api/settings", {{"clog", "1"}, {"cmode", "1"}, {"cpw", "1000"}});
  // to 00:05 tomorrow
  LTime l;
  TimeUtil::toLocal(sys.clock.unixNow(), l);
  jump((1440 - l.mod + 5) * 60000UL);
  CHECK(logHas(",0,COST,D,"));
  CHECK_EQ(sys.cost.counter(CP_DAY).sec, 0u);
  CHECK_EQ(sys.water.secondsToday(0), 0u);                    // daily budget reset at midnight
}

static void testLogApi() {
  std::string body = srv.request(HTTP_GET, "/api/log");
  CHECK(body.rfind("utc,local,zone,mode,soil0,soil1,sec,litres,result\n", 0) == 0);
  CHECK(body.find("utc,local", 10) == std::string::npos);    // the file's own header is not repeated
  JsonDocument z = call(HTTP_GET, "/api/zone", {{"id", "1"}});
  CHECK(z["hist"].size() > 0 && z["hist"][0].size() == 9);
  CHECK(std::string(z["hist"][0][8] | "").find('\n') == std::string::npos);
  CHECK_EQ(std::string(call(HTTP_GET, "/api/zone", {{"id", "9"}})["e"] | ""), std::string("id"));
  JsonDocument w = call(HTTP_GET, "/api/weather");
  CHECK_EQ((int)w["t"], 0);                                   // no forecast fetched on the host
  CHECK(w["d"].size() == 0 || w["d"][0]["rp"].isNull());

  // A new month starts a new file; the API lists both, newest first
  LTime l;
  TimeUtil::toLocal(sys.clock.unixNow(), l);
  CHECK_EQ(l.mo, 10);
  jump(2UL * 86400 * 1000);                                   // into November
  post("/api/water", {{"id", "4"}, {"sec", "5"}});
  tick(9000);
  JsonDocument m = call(HTTP_GET, "/api/log/months");
  CHECK_EQ((int)m["m"].size(), 2);
  CHECK_EQ(std::string(m["m"][0]["k"] | ""), std::string("2026-11"));
  CHECK_EQ(std::string(m["m"][1]["k"] | ""), std::string("2026-10"));
  CHECK((int)m["m"][1]["b"] > 100);
  std::string nov = srv.request(HTTP_GET, "/api/log", {{"m", "2026-11"}});
  std::string oct = srv.request(HTTP_GET, "/api/log", {{"m", "2026-10"}});
  CHECK(nov.find(",4,MANUAL,") != std::string::npos && nov.find("2026-10-") == std::string::npos);
  CHECK(oct.find("2026-10-30") != std::string::npos && oct.find("2026-11-") == std::string::npos);
  std::string all = srv.request(HTTP_GET, "/log.csv");
  CHECK(all.find("utc,local", 10) == std::string::npos);     // one header for all months
  CHECK(all.find("2026-10-30") < all.find("2026-11-0"));      // oldest first

  // Budget: months over the limit are deleted, the oldest first, never the current one
  for (int y = 2024; y <= 2025; y++)
    for (int mo = 1; mo <= 12; mo++) {
      char k[32];
      snprintf(k, sizeof(k), "/log/%04d-%02d.csv", y, mo);
      host::files()[k] = std::string("utc,local,zone,mode,soil0,soil1,sec,litres,result\n") + std::string(30000, 'x') + "\n";
    }
  sys.log.begin();                                            // as after a reboot
  CHECK(!host::files().count("/log/2024-01.csv"));
  CHECK(host::files().count("/log/2025-12.csv") && host::files().count("/log/2026-11.csv"));
  CHECK(sys.log.totalBytes() <= LOG_TOTAL_MAX);
  for (auto it = host::files().begin(); it != host::files().end();)   // clean up the filler
    it = it->first.rfind("/log/202", 0) == 0 && it->first < "/log/2026" ? host::files().erase(it) : std::next(it);
  sys.log.begin();
}


// Water now on the zone that is running changes its length; nothing switches
static void testAdjust() {
  int rows0 = logCount(",1,MANUAL,");
  CHECK_EQ(std::string(post("/api/water", {{"id", "1"}, {"sec", "60"}})["r"] | ""), std::string("OK"));
  tick(4000);
  CHECK(on(PIN_PUMP) && on(PIN_VALVE[0]));
  CHECK_EQ(std::string(post("/api/water", {{"id", "1"}, {"sec", "10"}})["r"] | ""), std::string("OK"));
  bool steady = true;
  for (int i = 0; i < 90; i++) { tick(100); if (!on(PIN_PUMP) || !on(PIN_VALVE[0])) steady = false; }
  CHECK(steady);                                              // no stop and restart
  CHECK(sys.water.runTotalS() >= 12 && sys.water.runTotalS() <= 13);   // 2.5 s done + 10 s more
  // settings and calendar edits during a run do not stop it
  post("/api/zone", {{"id", "1"}, {"name", "Front"}, {"plant.kc", "0,85"}});
  post("/api/settings", {{"cap", "100"}, {"frost", "1"}});
  post("/api/sched", {{"id", "1"}, {"sid", "0"}, {"d", "1"}, {"s", "300"}, {"sec", "20"}});
  post("/api/cal", {{"s", "2"}, {"wet", "1250"}});
  tick(100);
  CHECK(on(PIN_PUMP) && on(PIN_VALVE[0]));
  CHECK(fabsf(sys.cfg.z[0].plant.kc - 0.85f) < 0.001f);      // decimal comma accepted
  tick(6000);
  CHECK(!on(PIN_VALVE[0]));
  CHECK_EQ(logCount(",1,MANUAL,"), rows0 + 1);              // one row for the adjusted run
  // the old way (stop, then a new length) must not lose the new run in the queue (1 Nov bug)
  post("/api/water", {{"id", "1"}, {"sec", "60"}});
  tick(3000);
  post("/api/stop");
  CHECK_EQ(std::string(post("/api/water", {{"id", "1"}, {"sec", "20"}})["r"] | ""), std::string("OK"));   // runs once the valve closed
  tick(3000);
  CHECK(sys.water.activeZone() == 0 && sys.water.runTotalS() == 20);
  post("/api/stop");
  tick(2000);
}

// Cycle and soak: a deficit faster than the soil takes it is split into bursts
static void testSoak() {
  // zone 3: the scale model, 0.89 L/min on 0.04 m2 = 1335 mm/h; infiltration 100 mm/h; soil 33 %
  CHECK(post("/api/zone", {{"id", "3"}, {"spr.area", "0,04"}, {"spr.flow", "0.89"}, {"soil.inf", "100"}})["ok"] == 1);
  JsonDocument z = call(HTTP_GET, "/api/zone", {{"id", "3"}});
  CHECK(fabs(z["spr"]["rate"].as<float>() - 1335) < 1);
  host::analogValue[PIN_SOIL[2]] = rawFor(33);
  tick(1100);
  // deficit 3.8 mm / 70 % = 5.5 mm at 1335 mm/h = 15 s; bursts of 2 mm / 1235 mm/h = 5 s
  CHECK_EQ((int)state()["z"][2]["def"]["sec"], 15);
  CHECK_EQ(std::string(post("/api/water", {{"id", "3"}, {"def", "1"}})["r"] | ""), std::string("OK"));
  tick(2000);
  CHECK_EQ(sys.water.runTotalS(), 5u);
  tick(7000);
  CHECK(!sys.water.busy());
  JsonDocument s = state();
  CHECK_EQ(std::string(s["z"][2]["st"] | ""), std::string("SOAK"));
  CHECK(s["z"][2]["su"].as<uint32_t>() > sys.clock.unixNow() + 50);   // 1.85 mm at 100 mm/h = 67 s
  CHECK(!sys.irr.idle());
  tick(70000);                                                // second burst
  CHECK(sys.water.activeZone() == 2);
  tick(80000);                                                // third
  tick(10000);
  CHECK(sys.irr.idle());
  CHECK_EQ(logCount(",3,DEFICIT,"), 3);
  // a manual Water now runs as asked, in one piece
  post("/api/water", {{"id", "3"}, {"sec", "12"}});
  tick(2000);
  CHECK_EQ(sys.water.runTotalS(), 12u);
  // the button stops a soaking zone too
  post("/api/stop");
  tick(2000);
  post("/api/water", {{"id", "3"}, {"def", "1"}});
  tick(9000);
  CHECK(sys.irr.soaking(2));
  press(200);
  CHECK(sys.irr.idle() && !sys.irr.soaking(2));
  post("/api/zone", {{"id", "3"}, {"spr.p", "mist"}, {"spr.area", "1"}, {"spr.rate", "20"}, {"soil.inf", "13"}});
  host::analogValue[PIN_SOIL[2]] = rawFor(46);
  tick(2000);
}

static int lines(const std::string& b) { return (int)std::count(b.begin(), b.end(), '\n'); }

static void testStats() {
  std::string f = srv.request(HTTP_GET, "/api/stats/fast");
  CHECK(f.rfind("t0=", 0) == 0 && f.find(",step=3,") != std::string::npos);
  CHECK(lines(f) - 1 > 100);
  CHECK(f.find("\n46,46,46,46,") != std::string::npos);     // soil % of the four zones
  std::string m = srv.request(HTTP_GET, "/api/stats/min");
  CHECK(lines(m) - 1 > 5);
  CHECK(m.find(",15.0,60,0,") != std::string::npos);         // temperature, humidity, rain
  std::string q = srv.request(HTTP_GET, "/api/stats/q", {{"days", "7"}});
  CHECK(lines(q) - 1 >= 3);
  // the quarter rows carry watered seconds: zone 3's three bursts are in one of them
  bool w = false;
  size_t p = 0;
  while ((p = q.find('\n', p)) != std::string::npos) {
    size_t e = q.find('\n', p + 1);
    std::string row = q.substr(p + 1, e == std::string::npos ? std::string::npos : e - p - 1);
    char* fl[17];
    std::vector<char> b(row.begin(), row.end());
    b.push_back(0);
    if (splitCsv(b.data(), fl, 17) == 16 && atoi(fl[14]) >= 10) w = true;
    p = e == std::string::npos ? e : p + 1;
    if (e == std::string::npos) break;
  }
  CHECK(w);
}

// Health readings and the removed values
static void testHealthAndRemovals() {
  JsonDocument s = state();
  CHECK(s["mem"]["min"].as<int>() > 0 && s["mem"]["blk"].as<int>() > 0);
  CHECK(s["mem"]["fsT"].as<int>() > 0 && s["mem"]["nvsT"].as<int>() > 0);
  CHECK(s["env"]["p"].isNull());                              // no air pressure
  JsonDocument g = call(HTTP_GET, "/api/settings");
  CHECK(g["tot"]["s"].isNull() && !g["tot"]["y"].isNull());   // no season
  CHECK(g["flow"].isNull());                                  // flow is per zone now
  CHECK(g["tot"]["d"]["l"].as<float>() >= 0);
  // Wi-Fi: a weak signal is logged only when it stays unusable
  host::wifiRssi = -82;
  jump(400000);
  CHECK(!logHas(",0,ALERT,WIFI,"));
  host::wifiRssi = -90;
  tick(5000);
  CHECK(!logHas(",0,ALERT,WIFI,"));
  for (int i = 0; i < 310; i++) tick(1000);
  CHECK(logHas(",0,ALERT,WIFI,,,,W"));
  host::wifiRssi = -60;
  for (int i = 0; i < 310; i++) tick(1000);
  CHECK(logHas(",0,ALERT,WIFI_OK,,,,I"));
}

static void testFactoryReset() {
  post("/api/water", {{"id", "4"}, {"sec", "300"}});
  tick(3000);
  CHECK(on(PIN_PUMP));
  CHECK(post("/api/factory")["ok"] == 1);
  tick(100);
  CHECK(!ESP.restarted && !on(PIN_PUMP));                    // pump stops first; waits for the valve
  tick(2000);
  CHECK(ESP.restarted);
  CHECK(!on(PIN_VALVE[3]));
  CHECK(logHas(",4,MANUAL,"));                               // the stopped run was logged before the erase
  int cfgKeys = 0, wifiKeys = 0;
  for (auto& kv : host::nvs()) {
    if (kv.first.rfind("cfg/", 0) == 0 || kv.first.rfind("cal/", 0) == 0 || kv.first.rfind("rt/", 0) == 0) cfgKeys++;
    if (kv.first.rfind("wifi/", 0) == 0) wifiKeys++;
  }
  CHECK_EQ(cfgKeys, 0);
  CHECK(wifiKeys > 0);                                        // Wi-Fi kept
  CHECK(!logAll().empty());                                   // log kept
  CHECK(post("/api/log/clear")["ok"] == 1);                   // delete history
  CHECK(logAll().empty() && call(HTTP_GET, "/api/log/months")["m"].size() == 0);
  Settings fresh;
  fresh.begin();                                              // what the next boot loads
  CHECK_EQ(std::string(fresh.g.name), std::string("irrigation"));
  CHECK_EQ(std::string(fresh.z[1].name), std::string("Zone 2"));
}


// Plant age (FAO-56): the Kc curve, root growth, the soil depth cap, and the API
static void testGrowth() {
  ZoneCfg c;
  Settings::defaults(c, 0);
  CHECK(fabsf(c.plant.kc - 0.95f) < 0.001f && c.plant.lo == 60 && c.plant.hi == 95);   // cool-season turf
  // lettuce, planted 1 April: 20 / 30 / 15 / 10 days, Kc 0.7 -> 1.00 -> 0.95, roots 10 -> 30 cm
  c.plant = PlantParams{"lettuce", 1.0f, 30, 70, 95, 0.7f, 0.95f, 10, {20, 30, 15, 10}, 20260401};
  c.soil.depth = 50;
  Settings::setToday(20260325);
  CHECK(Settings::growth(c).st == Settings::NOT_YET);
  Settings::setToday(20260411);                               // day 10: initial
  Settings::Growth g = Settings::growth(c);
  CHECK(g.st == Settings::INITIAL && g.day == 10 && fabsf(g.kc - 0.7f) < 0.001f && fabsf(g.rootCm - 14) < 0.01f);
  Settings::setToday(20260506);                               // day 35: half way through development
  g = Settings::growth(c);
  CHECK(g.st == Settings::DEVELOPMENT && fabsf(g.kc - 0.85f) < 0.001f && fabsf(g.rootCm - 24) < 0.01f);
  Settings::setToday(20260526);                               // day 55: mid-season
  g = Settings::growth(c);
  CHECK(g.st == Settings::MID && fabsf(g.kc - 1.0f) < 0.001f && fabsf(g.rootCm - 30) < 0.01f);
  Settings::setToday(20260610);                               // day 70: late, half way
  g = Settings::growth(c);
  CHECK(g.st == Settings::LATE && fabsf(g.kc - 0.975f) < 0.001f);
  Settings::setToday(20260620);
  CHECK(Settings::growth(c).st == Settings::OVER);
  // the soil depth caps the roots (the scale model has 10 cm of soil)
  c.soil.depth = 10;
  CHECK(fabsf(Settings::growth(c).rootCm - 10) < 0.01f);
  CHECK(fabsf(Settings::availableMm(c) - (25 - 12) / 100.0f * 10 * 10) < 0.01f);
  // a lawn from seed (mid = late = 0) is an established lawn after its development stage
  c.plant = PlantParams{"lawnsd", 0.95f, 20, 60, 95, 0.9f, 0.95f, 3, {14, 30, 0, 0}, 20261101};
  c.soil.depth = 30;
  Settings::setToday(20261105);
  g = Settings::growth(c);
  CHECK(g.st == Settings::INITIAL && g.rootCm < 5);
  Settings::setToday(20261220);
  g = Settings::growth(c);
  CHECK(g.st == Settings::ESTABLISHED && fabsf(g.kc - 0.95f) < 0.001f && fabsf(g.rootCm - 20) < 0.01f);
  Settings::setToday(0);
}

static void testGrowthApi() {
  LTime l;
  TimeUtil::toLocal(sys.clock.unixNow(), l);
  char pd[11];
  TimeUtil::formatYmd(TimeUtil::addDays(l.ymd, -10), pd);     // planted 10 days ago
  CHECK(post("/api/zone", {{"id", "4"}, {"plant.p", "lettuce"}, {"plant.kc", "1"}, {"plant.kci", "0,7"}, {"plant.kce", "0.95"},
                           {"plant.root", "30"}, {"plant.ri", "10"}, {"plant.l", "20,30,15,10"}, {"plant.pd", pd},
                           {"soil.depth", "20"}})["ok"] == 1);
  JsonDocument z = call(HTTP_GET, "/api/zone", {{"id", "4"}});
  CHECK_EQ(std::string(z["plant"]["pd"] | ""), std::string(pd));
  CHECK((int)z["plant"]["l"][1] == 30 && (int)z["soil"]["depth"] == 20);
  JsonDocument st = state();
  JsonObject g = st["z"][3]["gr"];
  CHECK_EQ(std::string(g["st"] | ""), std::string("INIT"));
  CHECK((int)g["d"] == 10 && fabs(g["kc"].as<float>() - 0.7) < 0.01 && (int)g["r"] == 14);
  CHECK_EQ(std::string(post("/api/zone", {{"id", "4"}, {"plant.pd", "2026-02-30"}})["e"] | ""), std::string("date"));
  CHECK(post("/api/zone", {{"id", "4"}, {"plant.pd", ""}, {"plant.p", "lawn"}, {"plant.kc", "0.9"}, {"plant.root", "15"}, {"soil.depth", "30"}})["ok"] == 1);
  CHECK_EQ(std::string(state()["z"][3]["gr"]["st"] | ""), std::string("EST"));
}




// The app's actions sound like the button's: winter mode on/off, Water now start and end
static void testUiSounds() {
  tick(3000);
  CHECK(post("/api/global", {{"off", "1"}})["ok"] == 1);
  tick(20);
  CHECK_EQ((int)sys.fb.toneNow(), 4186);                       // C8: winter on starts high
  tick(1500);
  CHECK(post("/api/global", {{"off", "0"}})["ok"] == 1);
  tick(20);
  CHECK_EQ((int)sys.fb.toneNow(), 2093);                       // C7: winter off starts low
  tick(1500);
  CHECK_EQ(std::string(post("/api/water", {{"id", "1"}, {"sec", "5"}})["r"] | ""), std::string("OK"));
  tick(20);
  CHECK_EQ((int)sys.fb.toneNow(), 2093);                       // start: C7 -> G7
  bool endTune = false;                                        // the valve opens first, so about 7 s in all
  for (int i = 0; i < 1200 && !endTune; i++) { tick(10); endTune = sys.irr.idle() && sys.fb.toneNow() == 3136; }   // end: G7 -> C7
  CHECK(endTune);
  tick(1000);
  // winter on while watering: the winter sound only, no "finished" after it
  post("/api/water", {{"id", "1"}, {"sec", "60"}});
  tick(1000);
  post("/api/global", {{"off", "1"}});
  tick(20);
  CHECK_EQ((int)sys.fb.toneNow(), 4186);
  bool endAfter = false;                                       // the winter notes are C8 G7 E7 C7; then silence
  tick(1500);
  for (int i = 0; i < 300; i++) { tick(10); if (sys.fb.toneNow()) endAfter = true; }
  CHECK(!endAfter);
  post("/api/global", {{"off", "0"}});
  tick(2000);
}

// No valid RTC time: the system clock (SNTP or the phone) runs the controller; a faulty BME280:
// the forecast's hourly temperature protects against frost
// "Turn the zone off when the season is over": once, on the day after the late stage
static void testSeasonEnd() {
  ZoneCfg& c = sys.cfg.z[3];
  PlantParams keep = c.plant;
  LTime l; TimeUtil::toLocal(sys.clock.unixNow(), l);
  CHECK(post("/api/zone", {{"id", "4"}, {"plant.l", "1,1,1,1"}, {"plant.pd", ""}, {"plant.oe", "1"}})["ok"] == 1);
  char pd[11]; TimeUtil::formatYmd(TimeUtil::addDays(l.ymd, -3), pd);
  CHECK(post("/api/zone", {{"id", "4"}, {"plant.pd", pd}})["ok"] == 1);
  CHECK_EQ((int)call(HTTP_GET, "/api/zone", {{"id", "4"}})["plant"]["oe"], 1);
  CHECK(Settings::growth(c).st == Settings::LATE && c.en == 1);
  jump(24UL * 3600 * 1000);
  CHECK(Settings::growth(c).st == Settings::OVER);
  CHECK_EQ((int)c.en, 0);
  CHECK(logHas(",4,ALERT,SEASON_END,,,,I"));
  // turned on again by hand: it stays on (the change happens only once)
  CHECK(post("/api/zone", {{"id", "4"}, {"en", "1"}})["ok"] == 1);
  jump(24UL * 3600 * 1000);
  CHECK_EQ((int)c.en, 1);
  c.plant = keep;
  sys.cfg.saveZone(3);
}

static void testReboot() {
  ESP.restarted = false;
  post("/api/water", {{"id", "1"}, {"sec", "60"}});
  tick(3000);
  CHECK(sys.water.busy());
  CHECK(post("/api/reboot")["ok"] == 1);
  tick(3000);
  CHECK(!sys.water.busy());
  CHECK(ESP.restarted);
  CHECK(logHas(",0,ALERT,REBOOT,,,,I"));
  ESP.restarted = false;
}

// ---------------------------------------------------------------- power cuts, start reasons, fallback rows
static std::string lastRow(const char* has) {
  std::string all = logAll(), last;
  size_t i = 0;
  while (i < all.size()) {
    size_t j = all.find('\n', i); if (j == std::string::npos) j = all.size();
    std::string l = all.substr(i, j - i);
    if (l.find(has) != std::string::npos) last = l;
    i = j + 1;
  }
  return last;
}
// What a power cut leaves behind: the flash record, not the RAM one (the run ends here only to clear RAM)
static void powerCut() {
  std::vector<uint8_t> saved = host::nvs()["rt/run"];
  CHECK(!saved.empty());
  post("/api/stop");
  tick(3000);
  host::nvs()["rt/run"] = saved;
}

static void testPowerCut() {
  CHECK(logHas(",0,ALERT,POWER_ON,,,,I"));                       // the boot said why it started

  // 5 minutes on zone 2, the power goes 70 s in (the flash record is from 60 s), back at once
  post("/api/water", {{"id", "2"}, {"sec", "300"}});
  tick(3000 + 70000);
  CHECK(sys.water.activeZone() == 1);
  powerCut();
  CHECK(host::nvs().count("rt/run"));
  sys.irr.begin();
  std::string cut = lastRow(",FAULT");
  CHECK(cut.find(",2,MANUAL,") != std::string::npos);
  CHECK(cut.find(",60,") != std::string::npos);                  // counted up to the last save
  CHECK(!host::nvs().count("rt/run"));
  tick(3000);
  CHECK(sys.water.activeZone() == 1);                            // the rest is watered
  tick(245000);
  CHECK(!sys.water.busy());
  std::string res = lastRow(",RESUMED,");
  CHECK(res.find(",2,RESUMED,") != std::string::npos && res.find(",DONE") != std::string::npos);
  CHECK(res.find(",240,") != std::string::npos);                 // 300 - 60

  // the resumed run is cut too: logged, not resumed again (a pump that browns out would loop)
  post("/api/water", {{"id", "2"}, {"sec", "300"}});
  tick(3000 + 65000);
  powerCut();
  sys.irr.begin();
  tick(3000);
  CHECK(sys.water.activeZone() == 1);
  tick(65000);
  powerCut();
  sys.irr.begin();
  CHECK(lastRow(",FAULT").find(",2,RESUMED,") != std::string::npos);
  tick(3000);
  CHECK(!sys.water.busy());

  // a long outage: logged, not resumed
  post("/api/water", {{"id", "2"}, {"sec", "300"}});
  tick(3000 + 65000);
  powerCut();
  host::ms += (RESUME_WINDOW_S + 60) * 1000UL;
  sys.irr.begin();
  tick(3000);
  CHECK(!sys.water.busy());
  CHECK(lastRow(",FAULT").find(",2,MANUAL,") != std::string::npos);
}

static void testNoRise() {
  // a closed-loop zone whose soil does not move after three runs waters by the calendar: logged
  sys.cfg.z[3].m = (uint8_t)ZoneMode::CLOSED_LOOP;
  for (int i = 0; i < 3; i++) {
    post("/api/water", {{"id", "4"}, {"sec", "5"}});
    tick(12000);
    jump(RISE_CHECK_DELAY_MS + 5000);
  }
  CHECK(sys.dose.noRise(3));
  tick(2000);
  CHECK(logHas(",4,ALERT,NORISE,,,,W"));
  // the soil rises after the next run: back to normal
  post("/api/water", {{"id", "4"}, {"sec", "5"}});
  tick(12000);
  host::analogValue[PIN_SOIL[3]] = rawFor(60);
  jump(RISE_CHECK_DELAY_MS + 5000);
  CHECK(!sys.dose.noRise(3));
  CHECK(logHas(",4,ALERT,NORISE_OK,,,,I"));
  host::analogValue[PIN_SOIL[3]] = rawFor(46);
  sys.cfg.z[3].m = (uint8_t)ZoneMode::SCHEDULE;
  tick(3000);
}

static void testFallbacks() {
  uint32_t now = sys.clock.unixNow();
  // RTC module broken: it loses its time and ignores writes
  host::rtcValid = false; host::rtcBroken = true;
  tick(4000);                                                     // a few bad reads are ridden out first
  CHECK(!sys.clock.valid());
  CHECK_EQ((int)state()["ok"], 0);
  CHECK(post("/api/clock", {{"t", std::to_string(now + 2)}})["ok"] == 1);   // the phone's time
  tick(1200);
  CHECK(sys.clock.valid() && !sys.clock.rtcOk());
  CHECK(sys.clock.unixNow() >= now + 2 && sys.clock.unixNow() < now + 10);
  tick(6000);
  CHECK(logHas(",0,ALERT,CLOCK_SYS,,,,I"));                    // the log says what the time comes from
  JsonDocument st = state();
  CHECK_EQ((int)st["ok"], 1);
  CHECK_EQ((int)st["rtc"], 0);
  // the module is replaced: the next SNTP check (or a phone set) writes it again
  host::rtcBroken = false;
  CHECK(post("/api/clock", {{"t", std::to_string(sys.clock.unixNow())}})["ok"] == 1);
  tick(1200);
  CHECK(sys.clock.rtcOk());
  CHECK_EQ((int)state()["rtc"], 1);
  tick(6000);
  CHECK(logHas(",0,ALERT,CLOCK_SYS_OK,,,,I"));

  // forecast: yesterday, today and tomorrow, hourly from yesterday 00:00
  LTime l; TimeUtil::toLocal(sys.clock.unixNow(), l);
  uint32_t y0 = TimeUtil::addDays(l.ymd, -1);
  char d0[11], d1[11], d2[11];
  TimeUtil::formatYmd(y0, d0); TimeUtil::formatYmd(l.ymd, d1); TimeUtil::formatYmd(TimeUtil::addDays(l.ymd, 1), d2);
  std::string js = std::string("{\"daily\":{\"time\":[\"") + d0 + "\",\"" + d1 + "\",\"" + d2 +
    "\"],\"temperature_2m_max\":[9,8,7],\"temperature_2m_min\":[1,-3,0],\"et0_fao_evapotranspiration\":[1,1,1],\"precipitation_sum\":[0,0,0]},"
    "\"hourly\":{\"time\":[\"" + d0 + "T00:00\"],\"temperature_2m\":[";
  for (int i = 0; i < 72; i++) { char b[16]; snprintf(b, sizeof(b), "%s%.1f", i ? "," : "", i >= 24 && i < 48 ? -2.0f + 0.5f * (i - 24) : 5.0f); js += b; }
  js += "]}}";
  WxSnapshot w{};
  CHECK(Weather::parse(js.c_str(), w));
  CHECK_EQ((int)w.n, 3);
  CHECK_EQ((int)w.nh, 72);
  CHECK_EQ(w.h0, TimeUtil::localToUnix(y0, 0));
  sys.wx.inject(w);
  float expect = -2.0f + 0.5f * (l.h + l.mi / 60.0f);              // today's hourly line, interpolated
  CHECK(fabsf(sys.wx.tempAt(sys.clock.unixNow()) - expect) < 0.1f);
  // sensor fine: its own reading counts
  bool web = true;
  CHECK(fabsf(sys.airTempC(&web) - host::bmeT) < 0.1f && !web);
  // sensor faulty: the forecast counts, also for frost
  host::bmeOk = false;
  tick(40000);
  CHECK(!sys.env.valid());
  CHECK(fabsf(sys.airTempC(&web) - expect) < 0.2f && web);
  st = state();
  CHECK(fabsf(st["env"]["w"].as<float>() - expect) < 0.2f);
  float frost = sys.cfg.g.frost;
  sys.cfg.g.frost = expect + 1;                                    // the forecast is below the limit
  CHECK(Rules::belowFrost());
  sys.cfg.g.frost = expect - 1;
  CHECK(!Rules::belowFrost());
  sys.cfg.g.frost = frost;
  tick(20000);
  CHECK(logHas(",0,ALERT,ENV_WEB,,,,I"));                      // temperature from the forecast: logged
  host::bmeOk = true;
  tick(12000);
  CHECK(sys.env.valid());
  CHECK(!state()["env"]["w"].is<float>());
  tick(35000);
  CHECK(logHas(",0,ALERT,ENV_WEB_OK,,,,I"));
  jump(700000);                                                // the injected forecast is fresh: the WX row clears
  CHECK(logHas(",0,ALERT,WX_OK,,,,I"));
}

// ---------------------------------------------------------------- setup portal: join while the AP stays up
static WifiPortal* wp = nullptr;
static void ptick(uint32_t msTotal) { for (uint32_t t = 0; t < msTotal; t += 10) { host::ms += 10; wp->update(); } }
static JsonDocument preq(HTTPMethod m, const std::string& path, std::map<std::string, std::string> args = {}) {
  std::string body = wp->server().request(m, path, args);
  JsonDocument d;
  deserializeJson(d, body);
  return d;
}
static std::string pst() { return std::string(preq(HTTP_GET, "/status")["s"] | ""); }
static std::string nvsSsid() { Preferences p; p.begin("wifi"); String s = p.getString("ssid", ""); p.end(); return s.c_str(); }
static void portalUp() {                 // "Change WiFi network" from the UI, then the reboot
  Preferences p; p.begin("wifi"); p.putUChar("setup", 1); p.end();
  host::wifiStatus = WL_DISCONNECTED;
  host::apClients = 1;
  ESP.restarted = false;
  wp = new WifiPortal();
  wp->begin(false, "kert");
  ptick(500);
}

static void testSetupJoin() {
  host::scanNets = {{"Home", -50, 6, false}, {"Cafe", -70, 11, true}, {"Home", -80, 1, false}};
  portalUp();
  CHECK(wp->mode() == WifiPortal::Mode::AP);
  CHECK_EQ((int)host::apChannel, 6);                     // the strongest network's channel: no move later
  JsonDocument sc = preq(HTTP_GET, "/scan");
  CHECK_EQ((int)sc["n"].size(), 2);                       // the mesh's two access points: one row
  CHECK_EQ(std::string(sc["j"]["s"] | ""), std::string("idle"));
  CHECK(std::string(sc["a"] | "").rfind("Irrigation-Setup-", 0) == 0);

  // wrong password: reported, nothing saved, the old network kept
  int b0 = host::beginCount;
  CHECK_EQ((int)preq(HTTP_POST, "/save", {{"ssid", "Home"}, {"pass", "wrongpass"}})["ok"], 1);
  CHECK(wp->mode() == WifiPortal::Mode::AP);              // no reboot
  ptick(400);
  CHECK_EQ(host::beginCount, b0 + 1);
  CHECK_EQ(host::beginSsid, std::string("Home"));
  CHECK_EQ(host::beginChannel, 6);                        // straight to its channel: no sweep that drops the phone
  CHECK_EQ(pst(), std::string("conn"));
  CHECK_EQ(std::string(preq(HTTP_POST, "/save", {{"ssid", "Cafe"}, {"pass", ""}})["e"] | ""), std::string("busy"));
  CHECK_EQ(std::string(wp->server().request(HTTP_GET, "/rescan")), std::string("busy"));
  host::wifiDisconnect(15);
  ptick(1200);
  CHECK_EQ(host::beginCount, b0 + 2);                     // one more try
  CHECK_EQ(pst(), std::string("conn"));
  host::wifiDisconnect(15);
  ptick(50);
  JsonDocument st = preq(HTTP_GET, "/status");
  CHECK_EQ(std::string(st["s"] | ""), std::string("fail"));
  CHECK_EQ(std::string(st["e"] | ""), std::string("pass"));
  CHECK_EQ(nvsSsid(), std::string("TestNet"));

  // right password: the IP shows while the AP is still up; saved
  preq(HTTP_POST, "/save", {{"ssid", "Home"}, {"pass", "password1"}});
  ptick(400);
  host::wifiIp = IPAddress(192, 168, 0, 23);
  host::wifiStatus = WL_CONNECTED;
  ptick(50);
  st = preq(HTTP_GET, "/status");
  CHECK_EQ(std::string(st["s"] | ""), std::string("ok"));
  CHECK_EQ(std::string(st["ip"] | ""), std::string("192.168.0.23"));
  CHECK_EQ(std::string(st["h"] | ""), std::string("kert"));
  CHECK_EQ(nvsSsid(), std::string("Home"));
  CHECK(wp->mode() == WifiPortal::Mode::AP);
  CHECK_EQ(std::string(preq(HTTP_GET, "/scan")["j"]["ip"] | ""), std::string("192.168.0.23"));   // a reopened page sees it
  ptick(120000);                                          // a phone on the AP: it waits
  CHECK(wp->mode() == WifiPortal::Mode::AP);

  // Finish: the AP closes, same connection, the app is served
  CHECK_EQ((int)preq(HTTP_POST, "/done")["ok"], 1);
  ptick(1000);
  CHECK(wp->mode() == WifiPortal::Mode::STA);
  CHECK(host::wifiMode == WIFI_STA);
  CHECK(wp->connected());
  wp->server().request(HTTP_GET, "/status");
  CHECK_EQ(wp->server().status, 404);                     // setup routes are gone
  CHECK(!ESP.restarted);
}

static void testSetupJoinOther() {
  // network not found
  host::scanNets = {{"Home", -50, 13, false}};
  portalUp();
  CHECK_EQ((int)host::apChannel, 1);                     // 12-13: not every phone, so 1
  preq(HTTP_POST, "/save", {{"ssid", "Typed by hand"}, {"pass", "password1"}});
  ptick(400);
  CHECK_EQ(host::beginChannel, 0);                        // unknown channel: let the SDK look everywhere
  host::wifiDisconnect(201); ptick(1200); host::wifiDisconnect(201); ptick(50);
  CHECK_EQ(std::string(preq(HTTP_GET, "/status")["e"] | ""), std::string("nf"));

  // no answer at all: gives up after two 20 s tries
  preq(HTTP_POST, "/save", {{"ssid", "Home"}, {"pass", "password1"}});
  ptick(400 + 20000);
  CHECK_EQ(pst(), std::string("conn"));
  ptick(1000 + 20000);
  CHECK_EQ(std::string(preq(HTTP_GET, "/status")["e"] | ""), std::string("to"));

  // joined, nobody presses Finish: closes a minute after the phone has left
  preq(HTTP_POST, "/save", {{"ssid", "Home"}, {"pass", "password1"}});
  ptick(400);
  host::wifiStatus = WL_CONNECTED;
  ptick(50);
  CHECK_EQ(pst(), std::string("ok"));
  host::apClients = 0;
  ptick(59000);
  CHECK(wp->mode() == WifiPortal::Mode::AP);
  ptick(2000);
  CHECK(wp->mode() == WifiPortal::Mode::STA);
  host::apClients = 0;
  host::scanNets.clear();
  host::wifiIp = IPAddress(192, 168, 1, 50);
}

// ---------------------------------------------------------------- forecast: two models, rain rule, estimate
// An Open-Meteo answer for ICON + ECMWF: 8 days from yesterday, 96 hours from yesterday 00:00
static std::string wxJson(uint32_t y0, float et0, std::vector<float> dayI, std::vector<float> dayE,
                          std::map<int, float> hourI, std::map<int, float> hourE) {
  const char* M[2] = {"icon_seamless", "ecmwf_ifs025"};
  std::string j = "{\"daily\":{\"time\":[";
  for (int i = 0; i < 8; i++) { char d[11]; TimeUtil::formatYmd(TimeUtil::addDays(y0, i), d); j += std::string(i ? ",\"" : "\"") + d + "\""; }
  j += "]";
  for (int m = 0; m < 2; m++) {
    std::vector<float>& rain = m ? dayE : dayI;
    char b[64];
    j += std::string(",\"temperature_2m_max_") + M[m] + "\":[20,20,20,20,20,20,20,20]";
    j += std::string(",\"temperature_2m_min_") + M[m] + "\":[10,10,10,10,10,10,10,10]";
    j += std::string(",\"et0_fao_evapotranspiration_") + M[m] + "\":[";
    for (int i = 0; i < 8; i++) { snprintf(b, sizeof(b), "%s%.1f", i ? "," : "", et0); j += b; }
    j += std::string("],\"precipitation_sum_") + M[m] + "\":[";
    for (int i = 0; i < 8; i++) { snprintf(b, sizeof(b), "%s%.1f", i ? "," : "", i < (int)rain.size() ? rain[i] : 0.0f); j += b; }
    j += "]";
  }
  char d0[11]; TimeUtil::formatYmd(y0, d0);
  j += std::string("},\"hourly\":{\"time\":[\"") + d0 + "T00:00\"]";
  for (int m = 0; m < 2; m++) {
    std::map<int, float>& hr = m ? hourE : hourI;
    j += std::string(",\"temperature_2m_") + M[m] + "\":[";
    for (int i = 0; i < 96; i++) j += i ? ",15" : "15";
    j += std::string("],\"precipitation_") + M[m] + "\":[";
    for (int i = 0; i < 96; i++) { char b[16]; snprintf(b, sizeof(b), "%s%.1f", i ? "," : "", hr.count(i) ? hr[i] : 0.0f); j += b; }
    j += "]";
  }
  return j + "}}";
}

static void testForecast() {
  const uint32_t now = sys.clock.unixNow();
  LTime l; TimeUtil::toLocal(now, l);
  const uint32_t y0 = TimeUtil::addDays(l.ymd, -1), h0 = TimeUtil::localToUnix(y0, 0);
  const int hNow = (int)((now - h0) / 3600);
  WxSnapshot w{};

  // both models are read; the daily values are their mean, the rain per model is kept
  CHECK(Weather::parse(wxJson(y0, 3, {0, 4, 2}, {0, 2, 0}, {}, {}).c_str(), w));
  CHECK_EQ((int)w.n, 8);
  CHECK_EQ((int)w.nh, 96);
  CHECK(fabsf(w.d[1].rain - 3) < 0.01f && fabsf(w.d[1].rainM[0] - 4) < 0.01f && fabsf(w.d[1].rainM[1] - 2) < 0.01f);
  CHECK(fabsf(w.d[2].rain - 1) < 0.01f);
  CHECK(fabsf(w.d[5].et0 - 3) < 0.01f);
  {   // the real answer (Budapest, 7 Nov 2026, saved from api.open-meteo.com)
    FILE* f = fopen("test/host/real-wx.json", "rb");
    CHECK(f != nullptr);
    if (f) {
      std::string js; char b[512]; size_t n;
      while ((n = fread(b, 1, sizeof(b), f)) > 0) js.append(b, n);
      fclose(f);
      WxSnapshot r{};
      CHECK(Weather::parse(js.c_str(), r));
      CHECK_EQ((int)r.n, 8);
      CHECK_EQ((int)r.nh, 96);
      CHECK_EQ(r.h0, TimeUtil::localToUnix(20261106, 21 * 60));
      CHECK(fabsf(r.d[4].rainM[0] - 12.5f) < 0.01f && fabsf(r.d[4].rainM[1] - 14.0f) < 0.01f);
      CHECK(fabsf(r.d[1].et0 - 1.055f) < 0.01f);
      CHECK_EQ((int)r.hp[1][53], 11);                     // ECMWF 3-hourly rain, spread over the hours
      printf("real answer: %d days, %d hours, %u bytes\n", r.n, r.nh, (unsigned)js.size());
    }
  }
  WxSnapshot bad{};
  CHECK(!Weather::parse("{\"daily\":{\"time\":[\"2026-11-07\"],\"temperature_2m_max_gfs\":[9]}}", bad));   // nothing usable: failed

  // rain skip (2 mm): the next 24 h, both models must say so
  const float lim = sys.cfg.g.rainmm;
  sys.cfg.g.rainmm = 2;
  std::map<int, float> wet = {{hNow + 2, 1.5f}, {hNow + 5, 1.5f}};          // 3 mm soon
  std::map<int, float> dry = {{hNow + 2, 1.0f}};                             // 1 mm
  CHECK(Weather::parse(wxJson(y0, 3, {}, {}, wet, wet).c_str(), w = WxSnapshot{})); sys.wx.inject(w);
  CHECK(Rules::forecastRain());
  CHECK(Weather::parse(wxJson(y0, 3, {}, {}, wet, dry).c_str(), w = WxSnapshot{})); sys.wx.inject(w);
  CHECK(!Rules::forecastRain());                                           // the models disagree: water
  std::map<int, float> later = {{hNow + 30, 5.0f}};                         // tomorrow evening, not within 24 h
  CHECK(Weather::parse(wxJson(y0, 3, {}, {}, later, later).c_str(), w = WxSnapshot{})); sys.wx.inject(w);
  CHECK(!Rules::forecastRain());
  CHECK(Rules::forecastRain(now + 20 * 3600));                              // ...but for a run tomorrow, yes
  CHECK(!Rules::forecastRain(now + 60 * 3600));                             // beyond 48 h: not trusted

  // the next automatic watering's length, for a weather-based zone that is full now
  ZoneCfg& c = sys.cfg.z[0];
  const uint8_t m0 = c.m;
  c.m = (uint8_t)ZoneMode::OPEN_LOOP;
  sys.wb.reset(0);
  CHECK(Weather::parse(wxJson(y0, 4, {}, {}, {}, {}).c_str(), w = WxSnapshot{})); sys.wx.inject(w);
  int32_t e1 = sys.dose.estimate(0, now + 2 * 86400, 0), e3 = sys.dose.estimate(0, now + 4 * 86400, 0);
  CHECK(e1 > 0 && e3 > e1);                                                 // drier the longer it waits
  CHECK_EQ(sys.dose.estimate(0, now + 3600, 0), 0);                         // in an hour: still moist enough
  CHECK_EQ(sys.dose.estimate(0, now + 9 * 86400, 0), -1);                   // past the forecast: unknown
  // rain within 48 h counts; rain further ahead does not change the estimate
  CHECK(Weather::parse(wxJson(y0, 4, {0, 20, 20}, {0, 20, 20}, {}, {}).c_str(), w = WxSnapshot{})); sys.wx.inject(w);
  CHECK_EQ(sys.dose.estimate(0, now + 30 * 3600, 0), 0);
  CHECK(Weather::parse(wxJson(y0, 4, {0, 0, 0, 0, 0, 20, 20}, {0, 0, 0, 0, 0, 20, 20}, {}, {}).c_str(), w = WxSnapshot{})); sys.wx.inject(w);
  CHECK_EQ(sys.dose.estimate(0, now + 4 * 86400, 0), e3);
  // a fixed-schedule zone: its own length
  c.m = (uint8_t)ZoneMode::SCHEDULE;
  CHECK_EQ(sys.dose.estimate(0, now + 86400, 420), 420);
  c.m = m0;
  sys.cfg.g.rainmm = lim;
}

// ---------------------------------------------------------------- clock: DS1302 misreads
static void testClockGlitch() {
  const uint32_t t0 = sys.clock.unixNow();
  CHECK(t0 > 0 && sys.clock.rtcOk());
  // seen in the field: the right day and time, in 2090. One read, two reads: ignored
  host::rtcGlitch = 2019665843u - 1794086233u + 300; host::rtcGlitchReads = 2;
  tick(3000);
  uint32_t t1 = sys.clock.unixNow();
  CHECK(t1 >= t0 + 2 && t1 <= t0 + 4);
  CHECK(sys.clock.glitches() >= 1);
  // a plausible but wrong read (3 days ahead), once: ignored too
  host::rtcGlitch = 3 * 86400; host::rtcGlitchReads = 1;
  tick(2000);
  CHECK(sys.clock.unixNow() < t0 + 10);
  // somebody really set the RTC (an hour ahead): believed after three agreeing reads
  host::rtcBase += 3600;
  tick(1500);
  CHECK(sys.clock.unixNow() < t0 + 100);
  tick(3000);
  CHECK(sys.clock.unixNow() > t0 + 3600);
  host::rtcBase -= 3600;
  tick(5000);
  CHECK(sys.clock.unixNow() < t0 + 100);
  CHECK(logAll().find(",ALERT,RTC,") == std::string::npos);   // nothing of it reached the log as a clock fault
}

// what a misread used to leave behind is repaired: runs marked done in 2090, a cost period in 2090, a 2090 log month
static void testRepairFuture() {
  const uint32_t now = sys.clock.unixNow();
  const uint32_t far = now + 64UL * 365 * 86400;
  LTime f; TimeUtil::toLocal(far, f);
  WaterList& l = sys.cal.w[0];
  CHECK(l.n > 0);
  if (!l.n) return;
  l.e[0].fired = Scheduler::occKey(f.ymd, 600);
  sys.log.alert(0, "TEST", 'I');                                   // a row in this month...
  host::sysBase = far; host::sysBaseMs = host::ms;                 // ...and one dated 2090
  sys.log.alert(0, "FUTURE", 'I');
  host::sysBase = 0;
  CHECK(call(HTTP_GET, "/api/log/months")["m"][0]["k"].as<std::string>().rfind("2090", 0) == 0);
  sys.sched.begin();                                               // as after a reboot: checked at once
  tick(1500);
  LTime n; TimeUtil::toLocal(sys.clock.unixNow(), n);
  CHECK(l.e[0].fired <= Scheduler::occKey(n.ymd, n.mod));
  CHECK(call(HTTP_GET, "/api/log/months")["m"][0]["k"].as<std::string>().rfind("2090", 0) != 0);
  CHECK(!logHas(",ALERT,FUTURE,"));
}

// Settings of an unknown version (or none) become the defaults; nothing else is touched
static void testSettingsVersion() {
  auto saved = host::nvs();
  GlobalCfg g{};
  g.version = 4;                                              // e.g. firmware 1.2.x, whose converter is gone
  strcpy(g.name, "regi");
  Nvs::save("cfg", "g", g);
  Settings s;
  s.begin();
  CHECK_EQ(std::string(s.g.name), std::string(DEFAULT_NAME));
  CHECK(s.z[0].spr.flow > 0 && s.z[0].soil.depth > 0);
  host::nvs() = saved;
}

int main() {
  testTime();
  testSettingsVersion();
  testDndWindows();
  testGrowth();
  testUtil();
  boot();
  testBoot();
  testClockGlitch();
  testWaterNow();
  testDndAndCold();
  testZoneSettings();
  testSchedule();
  testButton();
  testOutputTest();
  testTankLow();
  testCostRollover();
  testGrowthApi();
  testAdjust();
  testSoak();
  testLogApi();
  testStats();
  testHealthAndRemovals();
  testUiSounds();
  testFallbacks();
  testRepairFuture();
  testForecast();
  testPowerCut();
  testNoRise();
  testSeasonEnd();
  testReboot();
  testFactoryReset();
  testSetupJoin();
  testSetupJoinOther();
  printf("%d checks, %d failed\n", checks, fails);
  if (fails) {
    printf("---- log ----\n%s", logAll().c_str());
  }
  return fails ? 1 : 0;
}

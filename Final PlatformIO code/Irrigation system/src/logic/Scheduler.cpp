#include "logic/Scheduler.h"
#include "System.h"
#include "util/Nvs.h"
#include "util/TimeUtil.h"

uint32_t Scheduler::occKey(uint32_t ymd, uint16_t minute) {
  return (uint32_t)TimeUtil::daysFromYmd(ymd) * 1440UL + minute;
}

bool Scheduler::occursOn(const Watering& e, uint32_t ymd, uint8_t wd) {
  return e.d ? (e.d & (1 << wd)) != 0 : e.dt == ymd;
}

void Scheduler::markPastToday(Watering& e) const {
  LTime l;
  if (!TimeUtil::toLocal(sys.clock.unixNow(), l)) return;
  if (occursOn(e, l.ymd, l.wd) && e.s <= l.mod) e.fired = max(e.fired, occKey(l.ymd, e.s));
}

void Scheduler::begin() {
  _repaired = false;                                         // checked again at the first valid time
  uint32_t stored = 0;
  Nvs::load("rt", "day", stored);
  _ymd = stored;
}

void Scheduler::update() {
  if (!elapsed(_tick, 1000)) return;
  _tick = millis();

  LTime now;
  if (!sys.clock.valid() || !TimeUtil::toLocal(sys.clock.unixNow(), now)) return;   // no clock, no schedule
  Settings::setToday(now.ymd);                               // the plants' age counts in local days

  if (!_repaired || now.ymd != _ymd) { _repaired = true; repairFuture(now); }
  if (now.ymd != _ymd) {
    uint32_t prev = _ymd;
    _ymd = now.ymd;
    Nvs::save("rt", "day", _ymd);
    newDay(prev, now.ymd);
  }

  const uint16_t qto = sys.cfg.g.qto;
  for (uint8_t z = 0; z < ZONES; z++) {
    WaterList& l = sys.cal.w[z];
    bool changed = false;
    for (uint8_t i = 0; i < l.n; i++) {
      Watering& e = l.e[i];
      if (!occursOn(e, now.ymd, now.wd) || e.s > now.mod) continue;
      uint32_t key = occKey(now.ymd, e.s);
      if (e.fired >= key) continue;
      e.fired = key;
      changed = true;
      if (!sys.cfg.z[z].en) continue;                       // disabled zones are not watered, not logged
      if (now.mod - e.s > qto) {                             // the controller was off at that time
        sys.log.skip(z, "SCHEDULE", NAN, "WINDOW_MISSED");
        continue;
      }
      fire(z, e);
    }
    if (changed) sys.cal.saveWaterings(z);
  }
}

void Scheduler::fire(uint8_t z, const Watering& e) {
  const ZoneCfg& c = sys.cfg.z[z];
  float soil;
  if (!sys.dose.soil(z, soil)) soil = NAN;
  const uint32_t now = sys.clock.unixNow();

  if (sys.dnd.active(z, now)) { sys.log.skip(z, "SCHEDULE", soil, "DND"); return; }
  if (sys.cfg.g.off) return;                               // winter: one WINTER_ON row in the log, not one per entry
  if (c.m != (uint8_t)ZoneMode::CLOSED_LOOP && Rules::rainSkips(z)) { sys.log.skip(z, "SCHEDULE", soil, "RAIN"); return; }

  const char* why;
  uint32_t sec = sys.dose.forEntry(z, e.sec, why);
  if (!sec) { sys.log.skip(z, "SCHEDULE", soil, why); return; }

  if (!sys.irr.enqueue(WaterRequest{z, sec, Src::SCHEDULE, false, false, true})) {
    sys.log.skip(z, "SCHEDULE", soil, "QUEUE_FULL");
  }
}

// A clock error (a DS1302 misread before 1.3.2's filter) may have left dates in the future: calendar
// entries marked done until then, cost periods, log months. Checked at boot and every day.
void Scheduler::repairFuture(const LTime& now) {
  const uint32_t key = occKey(now.ymd, now.mod);
  for (uint8_t z = 0; z < ZONES; z++) {
    WaterList& l = sys.cal.w[z];
    bool ch = false;
    for (uint8_t i = 0; i < l.n; i++) {
      if (l.e[i].fired > key) { l.e[i].fired = key; ch = true; }   // done up to now; the next ones fire again
    }
    if (ch) { sys.cal.saveWaterings(z); Serial.printf("SCHED: zone %u had runs marked done in the future\n", z + 1); }
  }
  const uint32_t t = sys.clock.unixNow();
  sys.cost.repairFuture(t);
  sys.log.dropFuture(t);
}

void Scheduler::newDay(uint32_t prevYmd, uint32_t ymd) {
  sys.water.resetDaily();
  sys.irr.persistToday();
  float tMin, tMax;
  sys.env.takeDayExtremes(tMin, tMax);
  if (prevYmd && TimeUtil::addDays(prevYmd, 1) == ymd) sys.wb.dailyStep(prevYmd, tMin, tMax);
  sys.cal.prune(ymd);
  // a crop whose season ended since the last day: turn its zone off if asked to (once, at the change)
  for (uint8_t z = 0; z < ZONES; z++) {
    ZoneCfg& c = sys.cfg.z[z];
    if (c.en && c.plant.offEnd && Settings::seasonEnded(c, prevYmd, ymd)) {
      sys.irr.stopZone(z, StopReason::USER);
      c.en = 0;
      sys.cfg.saveZone(z);
      sys.log.alert(z + 1, "SEASON_END", 'I');
    }
  }
  Serial.printf("SCHED: new day %lu\n", (unsigned long)ymd);
}

NextRun Scheduler::next(uint8_t z) const {
  NextRun r{false, 0, 0, "", -1};
  uint32_t entrySec = 0;
  LTime now;
  const uint32_t t0 = sys.clock.unixNow();
  if (z >= ZONES || !TimeUtil::toLocal(t0, now)) return r;
  const WaterList& l = sys.cal.w[z];
  const ZoneCfg& c = sys.cfg.z[z];

  for (int dd = 0; dd < 8 && !r.valid; dd++) {
    uint32_t ymd = TimeUtil::addDays(now.ymd, dd);
    uint8_t wd = TimeUtil::weekday(ymd);
    int best = -1;
    for (uint8_t i = 0; i < l.n; i++) {
      const Watering& e = l.e[i];
      if (!occursOn(e, ymd, wd) || (dd == 0 && e.s <= now.mod)) continue;
      if (best < 0 || e.s < l.e[best].s) best = i;
    }
    if (best < 0) continue;
    const Watering& e = l.e[best];
    r.valid = true;
    r.t = TimeUtil::localToUnix(ymd, e.s);
    r.sec = c.m == (uint8_t)ZoneMode::SCHEDULE ? (int32_t)(e.sec ? e.sec : SCHEDULE_DEFAULT_S) : -1;
    entrySec = e.sec;
  }
  if (!r.valid) return r;

  if (sys.cfg.g.off) r.x = "ALL_OFF";
  else if (sys.dnd.active(z, r.t)) r.x = "DND";
  else if (c.m != (uint8_t)ZoneMode::CLOSED_LOOP && !c.cov &&
           ((r.t - t0 < 6UL * 3600 && Rules::rainSensorWet()) || Rules::forecastRain(r.t))) r.x = "RAIN";   // rain then, not now
  if (!*r.x && r.sec < 0) r.est = sys.dose.estimate(z, r.t, entrySec);
  return r;
}

#include "logic/Irrigation.h"
#include <esp_attr.h>
#include "System.h"
#include "util/Nvs.h"
#include "util/TimeUtil.h"

namespace {
// The active run, kept in RAM that survives a reset (not a power cut): a reboot mid-run
// still gets its log row (result FAULT) and its seconds counted.
struct RunRec {
  uint32_t magic;
  uint8_t zone, src;
  float soil0;
  uint32_t start, last;
};
constexpr uint32_t REC_MAGIC = 0x52554E31;   // "RUN1"
RTC_NOINIT_ATTR RunRec rec;

struct Today { uint32_t ymd; uint32_t sec[ZONES]; };

uint32_t todayYmd() {
  LTime l;
  return TimeUtil::toLocal(sys.clock.unixNow(), l) ? l.ymd : 0;
}
}  // namespace

const char* srcName(Src s) {
  switch (s) {
    case Src::MANUAL:  return "MANUAL";
    case Src::DEFICIT: return "DEFICIT";
    default:           return "SCHEDULE";
  }
}

void Irrigation::begin() {
  Today t;
  uint32_t ymd = todayYmd();
  if (Nvs::load("rt", "today", t) && ymd && t.ymd == ymd) {
    for (uint8_t z = 0; z < ZONES; z++) sys.water.setSecondsToday(z, t.sec[z]);
  }
  if (rec.magic == REC_MAGIC && rec.zone < ZONES) {          // reset during a run
    uint32_t sec = rec.last > rec.start ? rec.last - rec.start : 0;
    sys.log.run(rec.zone, srcName((Src)rec.src), rec.soil0, NAN, sec, "FAULT");
    sys.cost.add(sec);
    sys.water.setSecondsToday(rec.zone, sys.water.secondsToday(rec.zone) + sec);
    persistToday();
  }
  rec.magic = 0;
}

void Irrigation::persistToday() {
  Today t{};
  t.ymd = todayYmd();
  for (uint8_t z = 0; z < ZONES; z++) t.sec[z] = sys.water.secondsToday(z);
  Nvs::save("rt", "today", t);
}

// ---------------- loop ----------------
void Irrigation::update() {
  RunReport rep;
  if (sys.water.takeReport(rep)) onRunEnded(rep);

  if (_act.on && sys.water.running()) {
    uint32_t now = sys.clock.unixNow();
    if (rec.magic == REC_MAGIC) rec.last = now;
    // A DND window that starts during a run stops it (unless this run overrode DND)
    if (!_act.dndOverride && sys.dnd.active(_act.zone, now)) sys.water.stop(StopReason::DND);
  }

  processQueue();
  cycleMaybeEnded();
}

void Irrigation::onRunEnded(const RunReport& rep) {
  float soil0 = _act.on ? _act.soil0 : NAN, soil1;
  if (!sys.dose.soil(rep.zone, soil1)) soil1 = NAN;
  Src src = _act.on ? _act.src : Src::MANUAL;

  sys.log.run(rep.zone, srcName(src), soil0, soil1, rep.pumpS, stopName(rep.reason));
  sys.cost.add(rep.pumpS);
  sys.wb.addApplied(rep.zone, rep.pumpS);
  sys.dose.runEnded(rep.zone, soil0);
  persistToday();
  _act.on = false;
  rec.magic = 0;

  if (rep.reason != StopReason::DONE) clearQueue(stopName(rep.reason));
}

void Irrigation::cycleMaybeEnded() {
  if (_cycle && idle()) {
    _cycle = false;
    sys.fb.cycleEnd();
  }
}

// ---------------- queue ----------------
bool Irrigation::enqueue(const WaterRequest& r) {
  for (uint8_t i = 0; i < _n; i++) {                        // one entry per zone: the newest wins
    if (_q[i].q.zone == r.zone) { _q[i] = Item{r, millis(), ""}; return true; }
  }
  if (_n >= QMAX) return false;
  _q[_n++] = Item{r, millis(), ""};
  return true;
}

bool Irrigation::queued(uint8_t z) const {
  for (uint8_t i = 0; i < _n; i++) if (_q[i].q.zone == z) return true;
  return false;
}

bool Irrigation::idle() const {
  return !sys.water.busy() && !sys.test.active() && _n == 0;
}

void Irrigation::drop(uint8_t i, const char* reason) {
  const WaterRequest& q = _q[i].q;
  if (q.src == Src::SCHEDULE) {                             // the experiment needs every planned run accounted for
    float s;
    if (!sys.dose.soil(q.zone, s)) s = NAN;
    sys.log.skip(q.zone, "SCHEDULE", s, reason);
  }
  for (uint8_t j = i + 1; j < _n; j++) _q[j - 1] = _q[j];
  _n--;
}

void Irrigation::clearQueue(const char* reason) {
  while (_n) drop(0, reason);
}

void Irrigation::processQueue() {
  if (!_n || sys.water.busy() || sys.test.active()) return;

  GBlock g = Rules::globalBlock();
  if (g == GBlock::ALL_OFF) { clearQueue("ALL_OFF"); return; }
  if (sys.water.faulted()) { clearQueue("FAULT"); return; }

  const uint32_t qtoMs = sys.cfg.g.qto * 60000UL;
  for (uint8_t i = 0; i < _n; i++) {
    Item& it = _q[i];
    if ((uint32_t)(millis() - it.enqMs) > qtoMs) {           // waited too long
      drop(i--, it.wait[0] ? it.wait : "QUEUE_TIMEOUT");
      continue;
    }
    if (g != GBlock::NONE) { it.wait = Rules::blockName(g); continue; }

    bool dropIt = false;
    if (tryStart(it, dropIt)) {
      for (uint8_t j = i + 1; j < _n; j++) _q[j - 1] = _q[j];
      _n--;
      return;
    }
    if (dropIt) drop(i--, it.wait);
  }
}

bool Irrigation::tryStart(Item& it, bool& dropIt) {
  const WaterRequest& q = it.q;
  const uint8_t z = q.zone;
  const uint32_t now = sys.clock.unixNow();

  if (!sys.cfg.z[z].en) { it.wait = "OFF"; dropIt = true; return false; }
  if (!q.dndOverride && sys.dnd.active(z, now)) { it.wait = "DND"; return false; }
  if (!q.coldOverride && Rules::belowCold(z)) { it.wait = "COLD"; return false; }

  float soil0;
  if (!sys.dose.soil(z, soil0)) soil0 = NAN;
  StartResult r = sys.water.start(z, q.sec);
  switch (r) {
    case StartResult::OK:
      _act = Active{true, z, q.src, soil0, q.dndOverride, q.beep};
      rec = RunRec{REC_MAGIC, z, (uint8_t)q.src, soil0, now, now};
      if (q.beep && !_cycle) { _cycle = true; sys.fb.cycleStart(); }
      return true;
    case StartResult::TANK_LOW:
    case StartResult::BUSY:
      it.wait = startName(r);
      return false;
    default:                                                 // DAILY_LIMIT, FAULT, TOO_SHORT: waiting will not help
      it.wait = startName(r);
      dropIt = true;
      return false;
  }
}

// ---------------- actions ----------------
WaterAnswer Irrigation::waterNow(uint8_t z, uint32_t sec, bool deficit, bool dndOverride, bool force) {
  WaterAnswer a;
  if (z >= ZONES) { a.r = "BAD_ZONE"; return a; }
  Src src = Src::MANUAL;
  if (deficit) {
    Deficit d;
    sec = sys.dose.deficit(z, d) ? d.sec : 0;
    src = Src::DEFICIT;
  }

  if (sys.water.faulted())   { a.r = "FAULT"; return a; }
  if (sys.cfg.g.off)         { a.r = "ALL_OFF"; return a; }
  if (Rules::belowFrost())   { a.r = "FROST"; return a; }
  if (sys.tank.low())        { a.r = "TANK_LOW"; return a; }
  if (!sys.cfg.z[z].en)      { a.r = "OFF"; return a; }
  if (sec == 0)              { a.r = "OK"; return a; }       // no deficit: nothing to do
  sec = clampv(sec, MIN_RUN_S, MAX_RUN_S);
  if (sys.water.remainingToday(z) < MIN_RUN_S) { a.r = "DAILY_LIMIT"; return a; }   // otherwise trimmed

  // DND vs Water now: the last action wins, but the UI asks first
  const uint32_t now = sys.clock.unixNow();
  if (!dndOverride) {
    char k = sys.dnd.kind(z, now);
    if (k) {
      a.r = "DND";
      a.k = k;
      a.u = k == 'z' ? sys.dnd.until(z + 1) : k == 'g' ? sys.dnd.until(0) : 0;
      return a;
    }
  } else if (sys.dnd.until(z + 1) > now) {
    sys.dnd.set(z + 1, 0);                                   // watering turns this zone's own DND off
  }

  if (!force && Rules::belowCold(z)) {
    a.r = "COLD";
    a.c = roundTo(sys.env.temperatureC(), 1);
    a.lim = sys.cfg.z[z].cold;
    return a;
  }

  bool wasIdle = idle();
  if (!enqueue(WaterRequest{z, sec, src, dndOverride, force, false})) { a.r = "BUSY"; return a; }
  if (wasIdle) processQueue();
  a.r = sys.water.activeZone() == (int8_t)z ? "OK" : "BUSY";
  return a;
}

void Irrigation::stopAll(StopReason why) {
  clearQueue(stopName(why));
  if (sys.water.running()) sys.water.stop(why);
}

void Irrigation::stopZone(uint8_t z, StopReason why) {
  for (uint8_t i = 0; i < _n; i++) if (_q[i].q.zone == z) drop(i--, stopName(why));
  if (sys.water.activeZone() == (int8_t)z && sys.water.running()) sys.water.stop(why);
}

void Irrigation::dndChanged(bool explicitSet) {
  const uint32_t now = sys.clock.unixNow();
  if (_act.on && sys.water.running() && sys.dnd.active(_act.zone, now) && (explicitSet || !_act.dndOverride)) {
    sys.water.stop(StopReason::DND);
  }
  for (uint8_t i = 0; i < _n; i++) {
    const WaterRequest& q = _q[i].q;
    if (sys.dnd.active(q.zone, now) && (explicitSet || !q.dndOverride)) drop(i--, "DND");
  }
}

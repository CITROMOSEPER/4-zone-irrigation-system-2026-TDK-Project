#include "logic/Irrigation.h"
#include <esp_attr.h>
#include "System.h"
#include "util/Nvs.h"
#include "util/TimeUtil.h"

namespace {
// The active run, in two places: RAM that survives a reset (exact to the second) and flash, written
// at the start and every RUN_SAVE_S (a power cut clears that RAM). The next boot logs it and may
// water the rest.
struct RunRec {
  uint32_t magic;
  uint8_t zone, src, pad0, pad1;
  float soil0;
  uint32_t start, last;
  uint32_t plan;                // seconds of the whole dose when this burst started (burst + rest)
};
constexpr uint32_t REC_MAGIC = 0x52554E32;   // "RUN2"
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
    case Src::RESUME:  return "RESUMED";
    default:           return "SCHEDULE";
  }
}

void Irrigation::begin() {
  Today t;
  uint32_t ymd = todayYmd();
  if (Nvs::load("rt", "today", t) && ymd && t.ymd == ymd) {
    for (uint8_t z = 0; z < ZONES; z++) sys.water.setSecondsToday(z, t.sec[z]);
  }
  RunRec r = rec;                                            // a reset: RAM still has it, to the second
  if (r.magic != REC_MAGIC && !(Nvs::load("rt", "run", r) && r.magic == REC_MAGIC)) r.magic = 0;   // a power cut: flash
  if (r.magic == REC_MAGIC && r.zone < ZONES) {
    uint32_t sec = r.last > r.start ? r.last - r.start : 0;
    sys.log.run(r.zone, srcName((Src)r.src), r.soil0, NAN, sec, "FAULT");
    sys.cost.add(r.zone, sec);
    sys.wb.addApplied(r.zone, sec);
    sys.water.setSecondsToday(r.zone, sys.water.secondsToday(r.zone) + sec);
    persistToday();

    // The rest, once: not after a run that was itself a resume (a pump that browns the supply out
    // would reboot forever), not after a long outage, not without a trustworthy clock
    uint32_t now = sys.clock.unixNow();
    uint32_t rest = r.plan > sec ? r.plan - sec : 0;
    if ((Src)r.src != Src::RESUME && sys.clock.valid() && now >= r.last && now - r.last <= RESUME_WINDOW_S &&
        rest >= MIN_RUN_S && sys.cfg.z[r.zone].en) {
      _resume = WaterRequest{r.zone, rest, Src::RESUME, false, false, true, r.soil0};
      _resumePending = true;
      Serial.printf("IRR: zone %u was cut off, %lu s left: resuming in %lu s\n", (unsigned)r.zone + 1,
                    (unsigned long)rest, (unsigned long)(RESUME_DELAY_MS / 1000));
    }
  }
  rec.magic = 0;
  Nvs::erase("rt", "run");
}

void Irrigation::saveRun(bool now) {
  if (rec.magic != REC_MAGIC) return;
  if (!now && rec.last - _runSavedAt < RUN_SAVE_S) return;
  _runSavedAt = rec.last;
  Nvs::save("rt", "run", rec);
}

// Rain is checked now (the queue does not); the queue checks the rest: frost, tank, DND, cold, the daily limit
void Irrigation::resumeMaybe() {
  if (!_resumePending || millis() < RESUME_DELAY_MS) return;
  _resumePending = false;
  const uint8_t z = _resume.zone;
  if (Rules::rainSkips(z)) { sys.log.skip(z, srcName(Src::RESUME), NAN, "RAIN"); return; }
  if (!enqueue(_resume)) sys.log.skip(z, srcName(Src::RESUME), NAN, "QUEUE_FULL");
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
    if (rec.magic == REC_MAGIC) { rec.last = now; saveRun(false); }
    // A DND window that starts during a run stops it (unless this run overrode DND)
    if (!_act.dndOverride && sys.dnd.active(_act.zone, now)) sys.water.stop(StopReason::DND);
  }

  resumeMaybe();
  processSoaks();
  processQueue();
  cycleMaybeEnded();
}

void Irrigation::onRunEnded(const RunReport& rep) {
  float soil0 = _act.on ? _act.soil0 : NAN, soil1;
  if (!sys.dose.soil(rep.zone, soil1)) soil1 = NAN;
  Src src = _act.on ? _act.src : Src::MANUAL;

  sys.log.run(rep.zone, srcName(src), soil0, soil1, rep.pumpS, stopName(rep.reason));
  sys.cost.add(rep.zone, rep.pumpS);
  sys.wb.addApplied(rep.zone, rep.pumpS);
  sys.stats.addWatered(rep.zone, rep.pumpS);
  persistToday();
  rec.magic = 0;
  Nvs::erase("rt", "run");

  bool more = _act.on && rep.reason == StopReason::DONE && _act.rest > 0;
  if (more) {                                               // cycle and soak: the rest after a pause
    WaterRequest r = _act.req;
    r.sec = _act.rest;
    r.soil0 = _act.soil0;
    _soak[rep.zone] = Soak{r, (uint32_t)(millis() + sys.dose.soakS(rep.zone, rep.pumpS) * 1000UL)};
    _soakOn[rep.zone] = true;
  } else {
    sys.dose.runEnded(rep.zone, soil0);                     // the rise check once the whole dose is in
  }
  _act.on = false;

  // A stop by the user already cleared the queue (stopAll/stopZone); a Water now sent right after
  // it (a new length) must survive. Other stops (tank low, fault, DND) end the whole cycle.
  if (rep.reason != StopReason::DONE && rep.reason != StopReason::USER) {
    clearQueue(stopName(rep.reason));
    for (uint8_t z = 0; z < ZONES; z++) dropSoak(z);
  }
}

void Irrigation::cycleMaybeEnded() {
  if (_cycle && idle()) {
    _cycle = false;
    sys.fb.cycleEnd();
  }
}

// ---------------- soak ----------------
void Irrigation::processSoaks() {
  for (uint8_t z = 0; z < ZONES; z++) {
    if (!_soakOn[z] || (int32_t)(millis() - _soak[z].readyMs) < 0) continue;
    _soakOn[z] = false;
    if (!enqueue(_soak[z].q)) sys.log.skip(z, srcName(_soak[z].q.src), NAN, "QUEUE_FULL");
  }
}

void Irrigation::dropSoak(uint8_t z) { if (z < ZONES) _soakOn[z] = false; }

bool Irrigation::soaking(uint8_t z) const { return z < ZONES && _soakOn[z]; }

uint32_t Irrigation::soakUntil(uint8_t z) const {
  if (!soaking(z)) return 0;
  int32_t left = (int32_t)(_soak[z].readyMs - millis());
  return sys.clock.unixNow() + (left > 0 ? (uint32_t)(left + 999) / 1000 : 0);
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
  for (uint8_t z = 0; z < ZONES; z++) if (_soakOn[z]) return false;
  return !sys.water.busy() && !sys.test.active() && _n == 0;
}

void Irrigation::drop(uint8_t i, const char* reason) {
  const WaterRequest& q = _q[i].q;
  if (q.src == Src::SCHEDULE || q.src == Src::RESUME) {     // the experiment needs every planned run accounted for
    float s;
    if (!sys.dose.soil(q.zone, s)) s = NAN;
    sys.log.skip(q.zone, srcName(q.src), s, reason);
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

  float soil0 = q.soil0;
  if (isnan(soil0) && !sys.dose.soil(z, soil0)) soil0 = NAN;

  // Cycle and soak for automatic and deficit doses; a fixed Water now runs as asked
  uint32_t burst = q.src == Src::MANUAL ? 0 : sys.dose.burstS(z);
  uint32_t run = burst && q.sec > burst ? burst : q.sec;
  uint32_t rest = q.sec - run;
  if (rest && rest < MIN_RUN_S) { run = q.sec; rest = 0; }  // no burst too short to start

  StartResult r = sys.water.start(z, run);
  switch (r) {
    case StartResult::OK:
      _act = Active{true, z, q.src, soil0, q.dndOverride, q.beep, rest, q};
      rec = RunRec{REC_MAGIC, z, (uint8_t)q.src, 0, 0, soil0, now, now, run + rest};
      saveRun(true);
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

  // The zone that is running: change its length, nothing restarts (and DND/cold were answered
  // when it started). A deficit of 0 ends it.
  if (_act.on && _act.zone == z && sys.water.running()) {
    sec = min(sec, MAX_RUN_S);
    sys.water.adjust(sec);
    _act.rest = 0;                                           // the new length replaces any planned bursts
    _act.src = src;
    if (rec.magic == REC_MAGIC) {                            // a power cut now resumes the new length
      rec.plan = (rec.last - rec.start) + sec;
      rec.src = (uint8_t)src;
      saveRun(true);
    }
    a.r = "OK";
    return a;
  }

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
    a.c = roundTo(sys.airTempC(), 1);
    a.lim = sys.cfg.z[z].cold;
    return a;
  }

  dropSoak(z);                                               // a new request replaces a soaking rest
  bool wasIdle = idle();
  if (!enqueue(WaterRequest{z, sec, src, dndOverride, force, true})) { a.r = "BUSY"; return a; }   // start and end sounds, as from the button
  if (wasIdle) processQueue();
  a.r = sys.water.activeZone() == (int8_t)z ? "OK" : "BUSY";
  return a;
}

void Irrigation::stopAll(StopReason why) {
  clearQueue(stopName(why));
  for (uint8_t z = 0; z < ZONES; z++) dropSoak(z);
  if (sys.water.running()) sys.water.stop(why);
  _act.rest = 0;
}

void Irrigation::stopZone(uint8_t z, StopReason why) {
  for (uint8_t i = 0; i < _n; i++) if (_q[i].q.zone == z) drop(i--, stopName(why));
  dropSoak(z);
  if (sys.water.activeZone() == (int8_t)z && sys.water.running()) {
    _act.rest = 0;
    sys.water.stop(why);
  }
}

void Irrigation::dndChanged(bool explicitSet) {
  const uint32_t now = sys.clock.unixNow();
  if (_act.on && sys.water.running() && sys.dnd.active(_act.zone, now) && (explicitSet || !_act.dndOverride)) {
    _act.rest = 0;
    sys.water.stop(StopReason::DND);
  }
  for (uint8_t i = 0; i < _n; i++) {
    const WaterRequest& q = _q[i].q;
    if (sys.dnd.active(q.zone, now) && (explicitSet || !q.dndOverride)) drop(i--, "DND");
  }
  // a soaking rest waits for the DND like any queued run (tryStart checks it), so it stays
}

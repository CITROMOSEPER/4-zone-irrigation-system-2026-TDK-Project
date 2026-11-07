#include "logic/Stats.h"
#include <LittleFS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <stdarg.h>
#include "System.h"

namespace {
const char* FILE_Q = "/stats.bin";
constexpr size_t REC = sizeof(Stats::Quarter);

// Collects text and sends it in ~1 kB chunks
struct Out {
  WebServer& s;
  char buf[1100];
  size_t n = 0;
  explicit Out(WebServer& w) : s(w) {}
  void add(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    va_list a;
    va_start(a, fmt);
    int k = vsnprintf(buf + n, sizeof(buf) - n, fmt, a);
    va_end(a);
    if (k > 0) n += min((size_t)k, sizeof(buf) - n - 1);
    if (n > 1000) flush();
  }
  // a long answer takes a while: the safety layer keeps timing the pump and valves between chunks
  void flush() { if (n) { s.sendContent(buf, n); n = 0; esp_task_wdt_reset(); sys.water.update(); } }
};

void csvStart(WebServer& s) {
  s.sendHeader("Cache-Control", "no-store");
  s.setContentLength(CONTENT_LENGTH_UNKNOWN);
  s.send(200, "text/csv", "");
}

void pctOut(Out& o, uint8_t v, char sep) {
  if (v == 255) o.add("%c", sep);
  else if (v & 1) o.add("%u.5%c", v / 2, sep);
  else o.add("%u%c", v / 2, sep);
}
}  // namespace

void Stats::begin() {
  // The flash ring: preallocate once, so every slot can be overwritten in place
  File f = LittleFS.open(FILE_Q, FILE_READ);
  size_t want = (size_t)STAT_Q_N * REC;
  bool ok = f && f.size() == want;
  if (f) f.close();
  if (!ok) {
    File w = LittleFS.open(FILE_Q, FILE_WRITE);
    if (w) {
      uint8_t zero[256] = {};
      for (size_t i = 0; i < want; i += sizeof(zero)) {
        w.write(zero, min(sizeof(zero), want - i));
        esp_task_wdt_reset();
      }
      w.close();
      ok = true;
    }
  }
  _fileOk = ok;
  _t1 = millis();
}


void Stats::addTo(Acc& a, const float* soil, float tankCm, float tankPct, float t, float h) {
  for (uint8_t z = 0; z < ZONES; z++) if (!isnan(soil[z])) { a.sum[z] += soil[z]; a.n[z]++; }
  if (!isnan(t)) { a.tSum += t; a.tN++; }
  if (!isnan(h)) { a.hSum += h; a.hN++; }
  // fast: tank distance (cm); quarter: tank level (%)
  float tank = isnan(tankCm) ? tankPct : tankCm;
  if (!isnan(tank)) { a.tankSum += tank; a.tankN++; }
}

void Stats::update() {
  if (!elapsed(_t1, 1000)) return;
  _t1 += 1000;
  if ((uint32_t)(millis() - _t1) > 5000) _t1 = millis();   // fell far behind: no burst of catch-up samples
  sample1s();
}

void Stats::sample1s() {
  float soil[ZONES];
  for (uint8_t z = 0; z < ZONES; z++) soil[z] = sys.soil[z].percent();
  float cm = sys.tank.filteredCm();
  int p = sys.tank.percent();
  addTo(_af, soil, cm, NAN, NAN, NAN);
  addTo(_aq, soil, NAN, p < 0 ? NAN : (float)p, sys.env.temperatureC(), sys.env.humidity());
  _secs++;

  if (_secs % STAT_FAST_S == 0) pushFast();
  if (_secs % STAT_MIN_S == 0) pushMinute();

  // Quarter-hours by the clock (15-minute steps are the same in UTC and local time)
  if (!sys.clock.valid()) return;
  uint32_t q = sys.clock.unixNow() / STAT_Q_S * STAT_Q_S;
  if (!_qStart) { _qStart = q; return; }
  if (q != _qStart) {
    writeQuarter(_qStart);
    _qStart = q;
  }
}

void Stats::pushFast() {
  Fast& s = _fast[_fHead];
  for (uint8_t z = 0; z < ZONES; z++) s.soil[z] = _af.n[z] ? pct2(_af.sum[z] / _af.n[z]) : 255;
  s.tankMm = _af.tankN ? (uint16_t)lroundf(_af.tankSum / _af.tankN * 10) : 0xFFFF;
  uint8_t fl = 0;
  if (sys.water.pumpOn()) fl |= F_PUMP;
  for (uint8_t z = 0; z < ZONES && z < 4; z++) if (sys.water.valveOn(z)) fl |= F_VALVE0 << z;
  if (sys.water.faulted()) fl |= F_FAULT;
  if (sys.tank.low()) fl |= F_TANKLOW;
  if (sys.rain.wet()) fl |= F_RAIN;
  s.flags = fl;
  _fHead = (_fHead + 1) % STAT_FAST_N;
  if (_fN < STAT_FAST_N) _fN++;
  _fLastMs = millis();
  _af = Acc{};
}

void Stats::pushMinute() {
  Minute& m = _min[_mHead];
  for (uint8_t z = 0; z < ZONES; z++) {
    m.raw[z] = sys.soil[z].fault() ? 0xFFFF : (uint16_t)sys.soil[z].raw();
    m.est[z] = pct2(sys.wb.estimate(z));
    Deficit d;
    m.def[z] = sys.dose.deficit(z, d) ? (uint16_t)lroundf(min(d.mm, 6500.0f) * 10) : 0xFFFF;
  }
  float t = sys.env.temperatureC(), h = sys.env.humidity();
  m.temp = isnan(t) ? INT16_MIN : (int16_t)lroundf(t * 10);
  m.hum = isnan(h) ? 255 : (uint8_t)lroundf(h);
  m.rain = sys.rain.wet() ? 1 : 0;
  m.rssi = sys.net.connected() ? (int8_t)WiFi.RSSI() : 0;
  _mHead = (_mHead + 1) % STAT_MIN_N;
  if (_mN < STAT_MIN_N) _mN++;
  _mLastMs = millis();
}

void Stats::addWatered(uint8_t zone, uint32_t sec) {
  if (zone < ZONES) _w[zone] += sec;
}

void Stats::writeQuarter(uint32_t qStart) {
  Quarter r{};
  r.t = qStart;
  for (uint8_t z = 0; z < ZONES; z++) {
    r.soil[z] = _aq.n[z] ? pct2(_aq.sum[z] / _aq.n[z]) : 255;
    r.est[z] = pct2(sys.wb.estimate(z));
    r.w[z] = (uint16_t)min(_w[z], (uint32_t)65535);
    _w[z] = 0;
  }
  r.tank = _aq.tankN ? (uint8_t)lroundf(_aq.tankSum / _aq.tankN) : 255;
  r.hum = _aq.hN ? (uint8_t)lroundf(_aq.hSum / _aq.hN) : 255;
  r.temp = _aq.tN ? (int16_t)lroundf(_aq.tSum / _aq.tN * 10) : INT16_MIN;
  _aq = Acc{};
  if (!_fileOk) return;
  File f = LittleFS.open(FILE_Q, "r+");
  if (!f) return;
  f.seek((size_t)((qStart / STAT_Q_S) % STAT_Q_N) * REC);
  f.write((const uint8_t*)&r, REC);
  f.close();
}

// ---------------- HTTP ----------------
// First line: the time of the first row and the step, then one row per sample, oldest first
void Stats::sendFast(WebServer& s) {
  csvStart(s);
  Out o(s);
  uint32_t now = sys.clock.unixNow();
  uint32_t ageS = _fN ? (millis() - _fLastMs) / 1000 : 0;
  uint32_t t0 = now && _fN ? now - ageS - (uint32_t)(_fN - 1) * STAT_FAST_S : 0;
  o.add("t0=%lu,step=%lu,cols=s1|s2|s3|s4|tank_mm|flags\n", (unsigned long)t0, (unsigned long)STAT_FAST_S);
  for (uint16_t i = 0; i < _fN; i++) {
    const Fast& f = _fast[(_fHead + STAT_FAST_N - _fN + i) % STAT_FAST_N];
    for (uint8_t z = 0; z < ZONES; z++) pctOut(o, f.soil[z], ',');
    if (f.tankMm == 0xFFFF) o.add(","); else o.add("%u,", f.tankMm);
    o.add("%u\n", f.flags);
  }
  o.flush();
  s.sendContent("");
}

void Stats::sendMinute(WebServer& s) {
  csvStart(s);
  Out o(s);
  uint32_t now = sys.clock.unixNow();
  uint32_t ageS = _mN ? (millis() - _mLastMs) / 1000 : 0;
  uint32_t t0 = now && _mN ? now - ageS - (uint32_t)(_mN - 1) * STAT_MIN_S : 0;
  o.add("t0=%lu,step=%lu,cols=r1|r2|r3|r4|temp|hum|rain|rssi|e1|e2|e3|e4|d1|d2|d3|d4\n", (unsigned long)t0,
        (unsigned long)STAT_MIN_S);
  for (uint16_t i = 0; i < _mN; i++) {
    const Minute& m = _min[(_mHead + STAT_MIN_N - _mN + i) % STAT_MIN_N];
    for (uint8_t z = 0; z < ZONES; z++) { if (m.raw[z] == 0xFFFF) o.add(","); else o.add("%u,", m.raw[z]); }
    if (m.temp == INT16_MIN) o.add(","); else o.add("%.1f,", m.temp / 10.0f);
    if (m.hum == 255) o.add(","); else o.add("%u,", m.hum);
    o.add("%u,%d,", m.rain, m.rssi);
    for (uint8_t z = 0; z < ZONES; z++) pctOut(o, m.est[z], ',');
    for (uint8_t z = 0; z < ZONES; z++) {
      if (m.def[z] == 0xFFFF) o.add(z + 1 < ZONES ? "," : "\n");
      else o.add("%.1f%c", m.def[z] / 10.0f, z + 1 < ZONES ? ',' : '\n');
    }
  }
  o.flush();
  s.sendContent("");
}

void Stats::sendQuarter(WebServer& s, uint16_t days) {
  csvStart(s);
  Out o(s);
  o.add("cols=t|s1|s2|s3|s4|e1|e2|e3|e4|tank|temp|hum|w1|w2|w3|w4\n");
  uint32_t now = sys.clock.unixNow();
  File f = _fileOk && now ? LittleFS.open(FILE_Q, FILE_READ) : File();
  if (f) {
    days = clampv<uint16_t>(days, 1, STAT_Q_N / 96);
    uint32_t last = now / STAT_Q_S * STAT_Q_S;               // the quarter being collected is not stored yet
    uint32_t n = (uint32_t)days * 96;
    for (uint32_t i = n; i >= 1; i--) {
      uint32_t t = last - i * STAT_Q_S;
      Quarter r;
      f.seek((size_t)((t / STAT_Q_S) % STAT_Q_N) * REC);
      if (f.read((uint8_t*)&r, REC) != REC || r.t != t) continue;   // nothing recorded then
      o.add("%lu,", (unsigned long)r.t);
      for (uint8_t z = 0; z < ZONES; z++) pctOut(o, r.soil[z], ',');
      for (uint8_t z = 0; z < ZONES; z++) pctOut(o, r.est[z], ',');
      if (r.tank == 255) o.add(","); else o.add("%u,", r.tank);
      if (r.temp == INT16_MIN) o.add(","); else o.add("%.1f,", r.temp / 10.0f);
      if (r.hum == 255) o.add(","); else o.add("%u,", r.hum);
      for (uint8_t z = 0; z < ZONES; z++) o.add("%u%c", r.w[z], z + 1 < ZONES ? ',' : '\n');
    }
    f.close();
  }
  o.flush();
  s.sendContent("");
}

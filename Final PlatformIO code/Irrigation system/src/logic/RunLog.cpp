#include "logic/RunLog.h"
#include <LittleFS.h>
#include <WebServer.h>
#include <esp_task_wdt.h>
#include <algorithm>
#include "System.h"
#include "util/TimeUtil.h"
#include "util/Util.h"

namespace {
const char* DIR = "/log";
const char* HEADER = "utc,local,zone,mode,soil0,soil1,sec,litres,result\n";

void pct(char* b, size_t n, float v) { if (isnan(v)) b[0] = 0; else snprintf(b, n, "%.0f", v); }

// Start of the row: "utc,local,"
int head(uint32_t t, char* b, size_t n) {
  char lt[20];
  TimeUtil::formatLocal(t, lt);
  return snprintf(b, n, "%lu,%s,", (unsigned long)t, lt);
}

bool validKey(const char* k) {
  return k && strlen(k) == 7 && isdigit((unsigned char)k[0]) && isdigit((unsigned char)k[1]) &&
         isdigit((unsigned char)k[2]) && isdigit((unsigned char)k[3]) && k[4] == '-' &&
         isdigit((unsigned char)k[5]) && isdigit((unsigned char)k[6]);
}

// Stream a month file without its header line
void streamBody(WebServer& s, File& f) {
  f.seek(0);
  while (f.available()) { int c = f.read(); if (c == '\n' || c < 0) break; }
  char buf[1024];
  while (f.available()) {
    size_t n = f.read((uint8_t*)buf, sizeof(buf));
    if (!n) break;
    s.sendContent(buf, n);
    esp_task_wdt_reset();
    sys.water.update();                           // a long download must not delay a valve or the pump
  }
}
}  // namespace

void RunLog::keyFor(uint32_t t, char* key) {
  LTime l;
  if (!t || !TimeUtil::toLocal(t, l)) { key[0] = 0; return; }
  snprintf(key, 8, "%04d-%02d", l.y, l.mo);
}

void RunLog::pathFor(const char* key, char* path) { snprintf(path, 24, "%s/%s.csv", DIR, key); }

int RunLog::find(const char* key) const {
  for (uint8_t i = 0; i < _mn; i++) if (!strcmp(_m[i].k, key)) return i;
  return -1;
}

void RunLog::listMonths() {
  _mn = 0;
  _total = 0;
  File d = LittleFS.open(DIR);
  if (!d) return;
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    const char* n = f.name();
    const char* base = strrchr(n, '/');
    base = base ? base + 1 : n;
    char k[8];
    if (strlen(base) == 11 && !strcmp(base + 7, ".csv")) {
      memcpy(k, base, 7);
      k[7] = 0;
      if (validKey(k) && _mn < MAX_MONTHS) {
        copyStr(_m[_mn].k, sizeof(_m[_mn].k), k);
        _m[_mn].b = f.size();
        _total += _m[_mn].b;
        _mn++;
      }
    }
    f.close();
  }
  d.close();
  std::sort(_m, _m + _mn, [](const Month& a, const Month& b) { return strcmp(a.k, b.k) < 0; });
}

void RunLog::begin() {
  _fsOk = LittleFS.begin(true);
  if (!_fsOk) { Serial.println("LOG: LittleFS mount failed, nothing will be logged"); return; }
  LittleFS.mkdir(DIR);
  listMonths();
  enforceBudget();
  // The caches only need the newest rows: the last three months are plenty
  char path[24];
  for (uint8_t i = _mn > 3 ? _mn - 3 : 0; i < _mn; i++) { pathFor(_m[i].k, path); scan(path); }
}

void RunLog::scan(const char* path) {
  File f = LittleFS.open(path, FILE_READ);
  if (!f) return;
  char line[160];
  while (f.available()) {
    size_t n = f.readBytesUntil('\n', line, sizeof(line) - 1);
    line[n] = 0;
    if (n && strncmp(line, "utc,", 4) != 0) remember(line);
    esp_task_wdt_reset();
  }
  f.close();
}

void RunLog::remember(const char* line) {
  char row[160];
  copyStr(row, sizeof(row), line);
  row[strcspn(row, "\r\n")] = 0;                 // the cached rows carry no line ending
  char buf[160];
  copyStr(buf, sizeof(buf), row);
  char* f[9];
  if (splitCsv(buf, f, 9) < 9) return;
  int z = atoi(f[2]);
  if (z < 1 || z > ZONES) return;
  z--;

  uint8_t i = _hHead[z];
  copyStr(_hist[z][i], sizeof(_hist[z][i]), row);
  _hHead[z] = (i + 1) % HIST_ROWS;
  if (_hn[z] < HIST_ROWS) _hn[z]++;

  bool isRun = strcmp(f[3], "ALERT") && strcmp(f[3], "COST") && strncmp(f[8], "SKIP_", 5);
  if (isRun) {
    _last[z].valid = true;
    _last[z].t = strtoul(f[0], nullptr, 10);
    _last[z].sec = strtoul(f[6], nullptr, 10);
    copyStr(_last[z].r, sizeof(_last[z].r), f[8]);
  }
}

const char* RunLog::hist(uint8_t z, uint8_t i) const {
  if (z >= ZONES || i >= _hn[z]) return "";
  return _hist[z][(_hHead[z] + HIST_ROWS - 1 - i) % HIST_ROWS];
}

void RunLog::enforceBudget() {
  while (_total > LOG_TOTAL_MAX && _mn > 1) {     // never the newest month
    char path[24];
    pathFor(_m[0].k, path);
    LittleFS.remove(path);
    Serial.printf("LOG: %s deleted (log over %u kB)\n", path, (unsigned)(LOG_TOTAL_MAX / 1024));
    _total -= min(_total, _m[0].b);
    for (uint8_t i = 1; i < _mn; i++) _m[i - 1] = _m[i];
    _mn--;
  }
}

void RunLog::append(uint32_t t, const char* row) {
  Serial.print("LOG: ");
  Serial.print(row);
  remember(row);
  if (!_fsOk) return;
  char key[8];
  keyFor(t, key);
  if (!key[0]) {                                  // no clock: the newest month, or a placeholder
    if (_mn) copyStr(key, sizeof(key), _m[_mn - 1].k);
    else copyStr(key, sizeof(key), "1970-01");
  }
  int i = find(key);
  if (i < 0) {
    if (_mn >= MAX_MONTHS) { _total -= min(_total, _m[0].b); for (uint8_t j = 1; j < _mn; j++) _m[j - 1] = _m[j]; _mn--; }
    copyStr(_m[_mn].k, sizeof(_m[_mn].k), key);
    _m[_mn].b = 0;
    _mn++;
    std::sort(_m, _m + _mn, [](const Month& a, const Month& b) { return strcmp(a.k, b.k) < 0; });
    i = find(key);
  }
  char path[24];
  pathFor(key, path);
  File f = LittleFS.open(path, FILE_APPEND);
  if (!f) { Serial.println("LOG: cannot open the log file"); return; }
  size_t before = f.size(), w = 0;                // size() at open is exact; after writes it may lag a buffer
  if (before == 0) w += f.print(HEADER);
  w += f.print(row);
  f.close();
  size_t after = before + w;
  _m[i].b = after;
  _total += after - before;
  enforceBudget();
}

void RunLog::run(uint8_t z, const char* mode, float soil0, float soil1, uint32_t sec, const char* result) {
  char row[160], s0[8], s1[8], lit[16];
  pct(s0, sizeof(s0), soil0);
  pct(s1, sizeof(s1), soil1);
  snprintf(lit, sizeof(lit), "%.2f", z < ZONES ? Settings::litres(sys.cfg.z[z], sec) : 0.0f);
  uint32_t t = sys.clock.unixNow();
  int n = head(t, row, sizeof(row));
  snprintf(row + n, sizeof(row) - n, "%u,%s,%s,%s,%lu,%s,%s\n", z + 1, mode, s0, s1, (unsigned long)sec, lit, result);
  append(t, row);
}

void RunLog::skip(uint8_t z, const char* mode, float soil0, const char* reason) {
  char row[160], s0[8];
  pct(s0, sizeof(s0), soil0);
  uint32_t t = sys.clock.unixNow();
  int n = head(t, row, sizeof(row));
  snprintf(row + n, sizeof(row) - n, "%u,%s,%s,,0,,SKIP_%s\n", z + 1, mode, s0, reason);
  append(t, row);
}

void RunLog::alert(uint8_t zone, const char* code, char level) {
  char row[160];
  uint32_t t = sys.clock.unixNow();
  int n = head(t, row, sizeof(row));
  snprintf(row + n, sizeof(row) - n, "%u,ALERT,%s,,,,%c\n", zone, code, level);
  append(t, row);
}

void RunLog::cost(char period, uint32_t start, uint32_t sec, float litres, float cost) {
  char row[160];
  uint32_t t = sys.clock.unixNow();
  int n = head(t, row, sizeof(row));
  snprintf(row + n, sizeof(row) - n, "0,COST,%c,%lu,%lu,%.2f,%.2f\n", period, (unsigned long)start, (unsigned long)sec, litres, cost);
  append(t, row);
}

void RunLog::clear() {
  char path[24];
  for (uint8_t i = 0; i < _mn; i++) { pathFor(_m[i].k, path); LittleFS.remove(path); }
  _mn = 0;
  _total = 0;
  memset(_last, 0, sizeof(_last));
  memset(_hist, 0, sizeof(_hist));
  memset(_hn, 0, sizeof(_hn));
  memset(_hHead, 0, sizeof(_hHead));
  Serial.println("LOG: history deleted");
}

// ---------------- HTTP ----------------
void RunLog::dropFuture(uint32_t now) {
  char cur[8], path[24];
  keyFor(now, cur);
  if (!cur[0]) return;
  bool any = false;
  for (uint8_t i = 0; i < _mn; i++) {
    if (strcmp(_m[i].k, cur) <= 0) continue;
    pathFor(_m[i].k, path);
    LittleFS.remove(path);
    Serial.printf("LOG: removed %s (dated after today: a clock error)\n", path);
    any = true;
  }
  if (any) listMonths();
}

void RunLog::sendMonths(WebServer& s) {
  String out = "{\"m\":[";
  for (int i = _mn - 1; i >= 0; i--) {
    char b[48];
    snprintf(b, sizeof(b), "%s{\"k\":\"%s\",\"b\":%lu}", i == _mn - 1 ? "" : ",", _m[i].k, (unsigned long)_m[i].b);
    out += b;
  }
  out += "]}";
  s.sendHeader("Cache-Control", "no-store");
  s.send(200, "application/json", out);
}

void RunLog::sendMonth(WebServer& s, const char* key) {
  s.sendHeader("Cache-Control", "no-store");
  s.setContentLength(CONTENT_LENGTH_UNKNOWN);
  s.send(200, "text/csv", "");
  s.sendContent(HEADER);
  const char* k = validKey(key) ? key : (_mn ? _m[_mn - 1].k : "");
  if (_fsOk && *k) {
    char path[24];
    pathFor(k, path);
    File f = LittleFS.open(path, FILE_READ);
    if (f) { streamBody(s, f); f.close(); }
  }
  s.sendContent("");                               // end of the chunked reply
}

void RunLog::sendAll(WebServer& s) {
  if (!_fsOk || !_mn) { s.send(404, "text/plain", "no log yet"); return; }
  s.sendHeader("Content-Disposition", "attachment; filename=irrigation-log.csv");
  s.setContentLength(CONTENT_LENGTH_UNKNOWN);
  s.send(200, "text/csv", "");
  s.sendContent(HEADER);
  for (uint8_t i = 0; i < _mn; i++) {
    char path[24];
    pathFor(_m[i].k, path);
    File f = LittleFS.open(path, FILE_READ);
    if (f) { streamBody(s, f); f.close(); }
  }
  s.sendContent("");
}

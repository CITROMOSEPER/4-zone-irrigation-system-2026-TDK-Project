#include "logic/RunLog.h"
#include <LittleFS.h>
#include <WebServer.h>
#include <esp_task_wdt.h>
#include "System.h"
#include "util/TimeUtil.h"
#include "util/Util.h"

namespace {
const char* LOG_FILE = "/log.csv";
const char* LOG_OLD = "/log_old.csv";
const char* HEADER = "utc,local,zone,mode,soil0,soil1,sec,litres,result\n";

void pct(char* b, size_t n, float v) { if (isnan(v)) b[0] = 0; else snprintf(b, n, "%.0f", v); }

// Start of the row: "utc,local,"
int head(char* b, size_t n) {
  uint32_t t = sys.clock.unixNow();
  char lt[20];
  TimeUtil::formatLocal(t, lt);
  return snprintf(b, n, "%lu,%s,", (unsigned long)t, lt);
}
}  // namespace

void RunLog::begin() {
  _fsOk = LittleFS.begin(true);
  if (!_fsOk) { Serial.println("LOG: LittleFS mount failed, nothing will be logged"); return; }
  // A log in the old v1 format is kept aside, not mixed with v2 rows
  File f = LittleFS.open(LOG_FILE, FILE_READ);
  if (f) {
    String first = f.readStringUntil('\n');
    f.close();
    if (first + "\n" != HEADER) {
      LittleFS.remove("/log_v1.csv");
      LittleFS.rename(LOG_FILE, "/log_v1.csv");
      LittleFS.remove(LOG_OLD);
    }
  }
  scan(LOG_OLD);
  scan(LOG_FILE);
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

void RunLog::append(const char* row) {
  Serial.print("LOG: ");
  Serial.print(row);
  remember(row);
  if (!_fsOk) return;
  File f = LittleFS.open(LOG_FILE, FILE_APPEND);
  if (f && f.size() > LOG_MAX_BYTES) {       // rotate: keep one old file
    f.close();
    LittleFS.remove(LOG_OLD);
    LittleFS.rename(LOG_FILE, LOG_OLD);
    f = LittleFS.open(LOG_FILE, FILE_APPEND);
  }
  if (!f) { Serial.println("LOG: cannot open the log file"); return; }
  if (f.size() == 0) f.print(HEADER);
  f.print(row);
  f.close();
}

void RunLog::run(uint8_t z, const char* mode, float soil0, float soil1, uint32_t sec, const char* result) {
  char row[160], s0[8], s1[8], lit[12] = "";
  pct(s0, sizeof(s0), soil0);
  pct(s1, sizeof(s1), soil1);
  if (sys.cfg.g.flow > 0) snprintf(lit, sizeof(lit), "%.1f", sec * sys.cfg.g.flow / 60.0f);
  int n = head(row, sizeof(row));
  snprintf(row + n, sizeof(row) - n, "%u,%s,%s,%s,%lu,%s,%s\n", z + 1, mode, s0, s1, (unsigned long)sec, lit, result);
  append(row);
}

void RunLog::skip(uint8_t z, const char* mode, float soil0, const char* reason) {
  char row[160], s0[8];
  pct(s0, sizeof(s0), soil0);
  int n = head(row, sizeof(row));
  snprintf(row + n, sizeof(row) - n, "%u,%s,%s,,0,,SKIP_%s\n", z + 1, mode, s0, reason);
  append(row);
}

void RunLog::alert(uint8_t zone, const char* code, char level) {
  char row[160];
  int n = head(row, sizeof(row));
  snprintf(row + n, sizeof(row) - n, "%u,ALERT,%s,,,,%c\n", zone, code, level);
  append(row);
}

void RunLog::cost(char period, uint32_t start, uint32_t sec, float litres, float cost) {
  char row[160];
  int n = head(row, sizeof(row));
  snprintf(row + n, sizeof(row) - n, "0,COST,%c,%lu,%lu,%.1f,%.2f\n", period, (unsigned long)start, (unsigned long)sec, litres, cost);
  append(row);
}

// ---- download ----
namespace {
// Stream [from, end) of a file; if 'dropFirst', skip up to the first newline (a partial row)
void streamPart(WebServer& s, File& f, size_t from, bool dropFirst) {
  f.seek(from);
  char buf[1024];
  if (dropFirst) {
    while (f.available()) {
      int c = f.read();
      if (c == '\n' || c < 0) break;
    }
  }
  while (f.available()) {
    size_t n = f.read((uint8_t*)buf, sizeof(buf));
    if (!n) break;
    s.sendContent(buf, n);
    esp_task_wdt_reset();
  }
}
}  // namespace

void RunLog::sendTail(WebServer& s) {
  s.sendHeader("Cache-Control", "no-store");
  s.setContentLength(CONTENT_LENGTH_UNKNOWN);
  s.send(200, "text/csv", "");
  s.sendContent(HEADER);
  if (_fsOk) {
    File cur = LittleFS.open(LOG_FILE, FILE_READ);
    size_t curSize = cur ? cur.size() : 0;
    if (curSize < LOG_API_TAIL_BYTES) {            // top up from the rotated file
      File old = LittleFS.open(LOG_OLD, FILE_READ);
      if (old) {
        size_t want = LOG_API_TAIL_BYTES - curSize;
        size_t from = old.size() > want ? old.size() - want : 0;
        streamPart(s, old, from, true);            // a partial first row, or the header: dropped
        old.close();
      }
    }
    if (cur) {
      size_t from = curSize > LOG_API_TAIL_BYTES ? curSize - LOG_API_TAIL_BYTES : 0;
      streamPart(s, cur, from, true);              // the header line, or a partial row
      cur.close();
    }
  }
  s.sendContent("");                               // end of the chunked reply
}

void RunLog::sendFile(WebServer& s) {
  File f = _fsOk ? LittleFS.open(LOG_FILE, FILE_READ) : File();
  if (!f) { s.send(404, "text/plain", "no log yet"); return; }
  s.sendHeader("Content-Disposition", "attachment; filename=irrigation-log.csv");
  s.streamFile(f, "text/csv");
  f.close();
}

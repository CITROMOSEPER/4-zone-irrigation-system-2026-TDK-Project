#pragma once
// The log: one CSV file per local month on LittleFS, /log/YYYY-MM.csv, each with the header line.
//   utc,local,zone,mode,soil0,soil1,sec,litres,result
//   run:   mode SCHEDULE|MANUAL|DEFICIT, result DONE|USER|DND|TANK_LOW|FAULT|SKIP_<reason>
//   alert: utc,local,zone (0 = system),ALERT,<code>,,,,<E|W|I>
//   cost:  utc,local,0,COST,<D|W|M|Y>,<period start utc>,<pump s>,<litres>,<cost>
// When all months together pass LOG_TOTAL_MAX, the oldest month is deleted (never the current one).
// The state polled every 2 s needs each zone's last run and the zone view its last rows: both are
// kept in RAM (rebuilt from the newest months at boot) so no request has to read a file.
#include <Arduino.h>
#include "Config.h"

class WebServer;

struct LastRun {
  bool valid;
  uint32_t t, sec;
  char r[16];
};

class RunLog {
public:
  void begin();
  bool ok() const { return _fsOk; }

  void run(uint8_t zone, const char* mode, float soil0, float soil1, uint32_t sec, const char* result);
  void skip(uint8_t zone, const char* mode, float soil0, const char* reason);
  void alert(uint8_t zone, const char* code, char level);
  void cost(char period, uint32_t start, uint32_t sec, float litres, float cost);

  const LastRun& last(uint8_t zone) const { return _last[zone]; }
  // Rows of one zone, newest first (i = 0..count-1)
  uint8_t histCount(uint8_t zone) const { return _hn[zone]; }
  const char* hist(uint8_t zone, uint8_t i) const;

  // HTTP
  void sendMonths(WebServer& s);                    // /api/log/months: {"m":[{"k":"2026-11","b":1234},...]} newest first
  void sendMonth(WebServer& s, const char* key);    // /api/log?m=YYYY-MM (default: the newest month)
  void sendAll(WebServer& s);                       // /log.csv: every month, oldest first, one header

  uint32_t totalBytes() const { return _total; }
  void clear();
  void dropFuture(uint32_t now);                    // month files after this month: written by a clock error                                     // delete every month file and the caches

private:
  struct Month { char k[8]; uint32_t b; };          // "YYYY-MM", bytes
  static constexpr uint8_t MAX_MONTHS = 120;

  void append(uint32_t t, const char* row);
  void remember(const char* row);                   // update the RAM caches from one row
  void scan(const char* path);
  void listMonths();
  void enforceBudget();
  int find(const char* key) const;
  static void keyFor(uint32_t t, char* key);        // local month of a unix time; "" if unknown
  static void pathFor(const char* key, char* path);

  bool _fsOk = false;
  Month _m[MAX_MONTHS];                             // oldest first
  uint8_t _mn = 0;
  uint32_t _total = 0;
  LastRun _last[ZONES] = {};
  char _hist[ZONES][HIST_ROWS][96] = {};
  uint8_t _hn[ZONES] = {}, _hHead[ZONES] = {};
};

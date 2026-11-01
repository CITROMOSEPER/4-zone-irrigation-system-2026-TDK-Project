#pragma once
// /log.csv on LittleFS, rotated to /log_old.csv above LOG_MAX_BYTES.
//   utc,local,zone,mode,soil0,soil1,sec,litres,result
//   run:   mode SCHEDULE|MANUAL|DEFICIT, result DONE|USER|DND|TANK_LOW|FAULT|SKIP_<reason>
//   alert: utc,local,zone (0 = system),ALERT,<code>,,,,<E|W|I>
//   cost:  utc,local,0,COST,<D|W|M|Y>,<period start utc>,<pump s>,<litres>,<cost>
// The state polled every 2 s needs each zone's last run and the zone view its last rows: both are
// kept in RAM (rebuilt from the file at boot) so no request has to read the file.
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

  void sendTail(WebServer& s);     // /api/log: header + the newest LOG_API_TAIL_BYTES
  void sendFile(WebServer& s);     // /log.csv

private:
  void append(const char* row);
  void remember(const char* row); // update the RAM caches from one row
  void scan(const char* path);

  bool _fsOk = false;
  LastRun _last[ZONES] = {};
  char _hist[ZONES][HIST_ROWS][96] = {};
  uint8_t _hn[ZONES] = {}, _hHead[ZONES] = {};
};

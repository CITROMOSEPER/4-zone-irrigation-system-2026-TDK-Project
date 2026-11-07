#include "util/TimeUtil.h"
#include <stdio.h>
#include <stdlib.h>
#include "Config.h"

namespace TimeUtil {

void begin() {
  setenv("TZ", LOCAL_TZ, 1);
  tzset();
}

bool toLocal(uint32_t ut, LTime& o) {
  if (ut == 0) return false;
  time_t t = ut;
  tm lt;
  localtime_r(&t, &lt);
  o.y = lt.tm_year + 1900;
  o.mo = lt.tm_mon + 1;
  o.d = lt.tm_mday;
  o.h = lt.tm_hour;
  o.mi = lt.tm_min;
  o.s = lt.tm_sec;
  o.wd = (uint8_t)((lt.tm_wday + 6) % 7);
  o.mod = (uint16_t)(o.h * 60 + o.mi);
  o.ymd = makeYmd(o.y, o.mo, o.d);
  return true;
}

uint32_t localToUnix(uint32_t ymd, int minuteOfDay) {
  tm t = {};
  t.tm_year = ymdY(ymd) - 1900;
  t.tm_mon = ymdM(ymd) - 1;
  t.tm_mday = ymdD(ymd);
  t.tm_hour = minuteOfDay / 60;     // mktime normalises 24:00 to the next day
  t.tm_min = minuteOfDay % 60;
  t.tm_isdst = -1;                  // let the TZ rules decide
  time_t r = mktime(&t);
  return r < 0 ? 0 : (uint32_t)r;
}

// Howard Hinnant's civil-from-days algorithms (proleptic Gregorian)
int32_t daysFromYmd(uint32_t ymd) {
  int y = ymdY(ymd), m = ymdM(ymd), d = ymdD(ymd);
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}

uint32_t ymdFromDays(int32_t z) {
  z += 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int y = (int)yoe + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp < 10 ? mp + 3 : mp - 9;
  return makeYmd(y + (m <= 2), (int)m, (int)d);
}

int dayOfYear(uint32_t ymd) {
  return daysFromYmd(ymd) - daysFromYmd(makeYmd(ymdY(ymd), 1, 1)) + 1;
}

uint32_t parseYmd(const char* s) {
  int y, m, d;
  char tail;
  if (!s || sscanf(s, "%4d-%2d-%2d%c", &y, &m, &d, &tail) != 3) return 0;
  if (y < 2000 || y > 2099 || m < 1 || m > 12 || d < 1 || d > 31) return 0;
  uint32_t v = makeYmd(y, m, d);
  return ymdFromDays(daysFromYmd(v)) == v ? v : 0;   // rejects 2026-02-30
}

void formatYmd(uint32_t ymd, char* buf) {
  // the ranges tell the compiler the text always fits (no -Wformat-truncation warning)
  snprintf(buf, 11, "%04u-%02u-%02u", (unsigned)ymdY(ymd) % 10000u, (unsigned)ymdM(ymd) % 100u, (unsigned)ymdD(ymd) % 100u);
}

void formatLocal(uint32_t ut, char* buf) {
  LTime l;
  if (!toLocal(ut, l)) { buf[0] = 0; return; }
  snprintf(buf, 20, "%04d-%02d-%02d %02d:%02d:%02d", l.y, l.mo, l.d, l.h, l.mi, l.s);
}

uint32_t periodStart(Period p, uint32_t ut) {
  LTime l;
  if (!toLocal(ut, l)) return 0;
  uint32_t day = l.ymd;
  switch (p) {
    case Period::WEEK:  day = addDays(day, -(int)l.wd); break;
    case Period::MONTH: day = makeYmd(l.y, l.mo, 1); break;
    case Period::YEAR:  day = makeYmd(l.y, 1, 1); break;
    default: break;
  }
  return localToUnix(day, 0);
}

}  // namespace TimeUtil

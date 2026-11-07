#pragma once
// Local time helpers. The RTC and every stored timestamp are unix seconds (UTC);
// local time (Europe/Budapest, DST via the TZ string) is used only to decide schedules,
// day boundaries and what the user sees.
//
// Dates are kept as an integer YYYYMMDD ("ymd"), e.g. 20261030.
#include <Arduino.h>
#include <time.h>

struct LTime {
  int y, mo, d, h, mi, s;
  uint8_t  wd;     // weekday, 0 = Monday (matches the UI's bitmask: bit0 = Monday)
  uint16_t mod;    // minute of the day, 0..1439
  uint32_t ymd;    // YYYYMMDD
};

namespace TimeUtil {

void begin();                                     // sets TZ

bool toLocal(uint32_t ut, LTime& out);          // false for ut == 0
uint32_t localToUnix(uint32_t ymd, int minuteOfDay);   // minute may be 1440 (= next midnight)

// Date arithmetic, no time zone involved
inline uint32_t makeYmd(int y, int m, int d) { return (uint32_t)y * 10000 + m * 100 + d; }
inline int ymdY(uint32_t v) { return v / 10000; }
inline int ymdM(uint32_t v) { return (v / 100) % 100; }
inline int ymdD(uint32_t v) { return v % 100; }

int32_t  daysFromYmd(uint32_t ymd);               // days since 1970-01-01
uint32_t ymdFromDays(int32_t days);
inline uint32_t addDays(uint32_t ymd, int n) { return ymdFromDays(daysFromYmd(ymd) + n); }
inline uint8_t weekday(uint32_t ymd) { return (uint8_t)((daysFromYmd(ymd) + 3) % 7); }   // 1970-01-01 was a Thursday
int dayOfYear(uint32_t ymd);

// "YYYY-MM-DD" <-> ymd. parse returns 0 for an invalid date.
uint32_t parseYmd(const char* s);
void formatYmd(uint32_t ymd, char* buf);          // buf >= 11
// "YYYY-MM-DD HH:MM:SS" local; empty for ut == 0. buf >= 20
void formatLocal(uint32_t ut, char* buf);

// Start of the local day / week (Monday) / month / year that contains 'unix'
enum class Period : uint8_t { DAY, WEEK, MONTH, YEAR };
uint32_t periodStart(Period p, uint32_t ut);

}  // namespace TimeUtil

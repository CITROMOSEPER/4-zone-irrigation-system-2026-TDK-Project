#pragma once
// Calendar scheduler: decides WHEN. Once a second (valid clock only):
//   - local midnight: daily counters, water-balance step, old one-offs pruned
//   - a watering entry whose minute has come fires once per occurrence:
//       DND -> SKIP_DND, rain (not covered, not closed loop) -> SKIP_RAIN, All off (winter) -> nothing,
//       the mode's dose 0 -> SKIP_WET, otherwise queued (Irrigation retries until the queue timeout)
//   - an occurrence missed by more than the queue timeout (controller off) -> SKIP_WINDOW_MISSED
// It also predicts each zone's next run for the UI.
#include <Arduino.h>
#include "store/Calendar.h"
#include "util/TimeUtil.h"

struct NextRun {
  bool valid;
  uint32_t t;
  int32_t sec;          // -1 = calculated when it fires (loop modes)
  const char* x;        // predicted skip reason, "" if none
  int32_t est;          // loop modes: the expected length from the forecast (informative: recalculated when it fires);
                        // 0 = probably moist enough (skipped), -1 = unknown
};

class Scheduler {
public:
  void begin();
  void update();

  NextRun next(uint8_t zone) const;

  // Unique key of one occurrence: local day number x 1440 + minute
  static uint32_t occKey(uint32_t ymd, uint16_t minute);
  static bool occursOn(const Watering& e, uint32_t ymd, uint8_t wd);

  // A new or edited entry must not fire for an occurrence that is already past today
  void markPastToday(Watering& e) const;

private:
  void repairFuture(const LTime& now);             // undo what a clock error dated in the future
  bool _repaired = false;
  void newDay(uint32_t prevYmd, uint32_t ymd);
  void fire(uint8_t zone, const Watering& e);

  uint32_t _tick = 0;
  uint32_t _ymd = 0;
};

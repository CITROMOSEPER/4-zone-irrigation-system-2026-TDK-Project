#pragma once
// Irrigation: every watering request (calendar, Water now, button) goes through here.
//   - a FIFO run queue: one zone at a time, the next one starts when the previous ended DONE
//   - waiting entries retry until the queue timeout (frost, tank low, cold, DND, busy)
//   - Water now with its precedence and the answers the UI translates
//   - when a run ends: log row, cost, water balance, closed-loop rise check, cycle beeps
// It only REQUESTS runs: WaterController still decides whether one may start.
#include <Arduino.h>
#include "Config.h"
#include "hw/WaterController.h"

enum class Src : uint8_t { SCHEDULE, MANUAL, DEFICIT };
const char* srcName(Src s);

struct WaterRequest {
  uint8_t zone;
  uint32_t sec;
  Src src;
  bool dndOverride;     // run despite DND (Water now after the question, the button)
  bool coldOverride;    // run below the zone's minimum temperature
  bool beep;            // part of an automatic/button cycle: beep at its start and end
};

// Answer of Water now, in the UI's words
struct WaterAnswer {
  const char* r;        // OK BUSY FAULT ALL_OFF FROST TANK_LOW OFF DAILY_LIMIT DND COLD BAD_ZONE
  char k = 0;           // DND: 'z' 'g' 'w'
  uint32_t u = 0;       // DND: until
  float c = NAN;        // COLD: temperature now
  int lim = 0;          // COLD: the zone's limit
};

class Irrigation {
public:
  void begin();
  void update();

  WaterAnswer waterNow(uint8_t zone, uint32_t sec, bool deficit, bool dndOverride, bool force);
  bool enqueue(const WaterRequest& r);            // false when the queue is full

  void stopAll(StopReason why);                   // the run and the whole queue
  void stopZone(uint8_t zone, StopReason why);    // that zone's run and queue entries
  void dndChanged(bool explicitSet);              // stop runs a DND now covers

  bool queued(uint8_t zone) const;
  bool idle() const;                              // nothing running, queued or testing
  Src activeSrc() const { return _act.src; }

  void persistToday();                            // seconds watered today, per zone

private:
  struct Item {
    WaterRequest q;
    uint32_t enqMs;
    const char* wait;                             // last reason it could not start ("" = none)
  };
  static constexpr uint8_t QMAX = 8;

  void onRunEnded(const RunReport& rep);
  void processQueue();
  bool tryStart(Item& it, bool& drop);            // true = started
  void drop(uint8_t i, const char* reason);
  void clearQueue(const char* reason);
  void cycleMaybeEnded();

  Item _q[QMAX];
  uint8_t _n = 0;

  struct Active { bool on; uint8_t zone; Src src; float soil0; bool dndOverride; bool beep; } _act{};
  bool _cycle = false;                            // a beeping cycle is in progress
};

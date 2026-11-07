#pragma once
// Irrigation: every watering request (calendar, Water now, button) goes through here.
//   - a FIFO run queue: one zone at a time, the next one starts when the previous ended DONE
//   - waiting entries retry until the queue timeout (frost, tank low, cold, DND, busy)
//   - cycle and soak: an automatic or deficit dose faster than the soil can take is split into
//     bursts; after each burst the rest waits (soaking) while other zones may run
//   - Water now with its precedence and the answers the UI translates; Water now on the zone that
//     is running changes its length instead of restarting it
//   - when a run ends: log row, cost, water balance, closed-loop rise check, cycle beeps
//   - a power cut or reset mid-run: the run is logged (FAULT) at the next boot, and if the power came
//     back within RESUME_WINDOW_S its rest is queued once (RESUMED); every usual check applies
// It only REQUESTS runs: WaterController still decides whether one may start.
#include <Arduino.h>
#include "Config.h"
#include "hw/WaterController.h"

enum class Src : uint8_t { SCHEDULE, MANUAL, DEFICIT, RESUME };   // RESUME: the rest of a run a power cut stopped
const char* srcName(Src s);

struct WaterRequest {
  uint8_t zone;
  uint32_t sec;
  Src src;
  bool dndOverride;     // run despite DND (Water now after the question, the button)
  bool coldOverride;    // run below the zone's minimum temperature
  bool beep;            // part of an automatic/button cycle: beep at its start and end
  float soil0;          // soil before the first burst (kept across bursts); NAN = read at start

  // A constructor, not a default member value: the ESP32 toolchain compiles C++11, where a struct
  // with "float soil0 = NAN;" can no longer be filled with {zone, sec, ...}
  WaterRequest() : zone(0), sec(0), src(Src::MANUAL), dndOverride(false), coldOverride(false), beep(false), soil0(NAN) {}
  WaterRequest(uint8_t z, uint32_t s, Src from, bool dnd, bool cold, bool bp, float s0 = NAN)
      : zone(z), sec(s), src(from), dndOverride(dnd), coldOverride(cold), beep(bp), soil0(s0) {}
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

  void stopAll(StopReason why);                   // the run, the queue and every soak
  void quietEnd() { _cycle = false; }             // no "finished" sound (winter mode has its own)
  void stopZone(uint8_t zone, StopReason why);    // that zone's run, queue entries and soak
  void dndChanged(bool explicitSet);              // stop runs a DND now covers

  bool queued(uint8_t zone) const;
  bool soaking(uint8_t zone) const;
  uint32_t soakUntil(uint8_t zone) const;         // unix time the next burst may start, 0 = none
  bool idle() const;                              // nothing running, queued, soaking or testing

  void persistToday();                            // seconds watered today, per zone

private:
  struct Item {
    WaterRequest q;
    uint32_t enqMs;
    const char* wait;                             // last reason it could not start ("" = none)
  };
  struct Soak {
    WaterRequest q;                               // the rest of the dose
    uint32_t readyMs;
  };
  static constexpr uint8_t QMAX = 8;

  void onRunEnded(const RunReport& rep);
  void processSoaks();
  void processQueue();
  bool tryStart(Item& it, bool& drop);            // true = started
  void drop(uint8_t i, const char* reason);
  void clearQueue(const char* reason);
  void dropSoak(uint8_t zone);
  void cycleMaybeEnded();
  void saveRun(bool now);
  void resumeMaybe();

  Item _q[QMAX];
  uint8_t _n = 0;
  Soak _soak[ZONES];
  bool _soakOn[ZONES] = {};

  struct Active {
    bool on; uint8_t zone; Src src; float soil0; bool dndOverride; bool beep;
    uint32_t rest;                                // seconds left after this burst (cycle and soak)
    WaterRequest req;
  } _act{};
  bool _cycle = false;                            // a beeping cycle is in progress
  uint32_t _runSavedAt = 0;                       // unix time the run record was last written to flash
  bool _resumePending = false;                    // a cut run's rest waits for the sensors after boot
  WaterRequest _resume;
};

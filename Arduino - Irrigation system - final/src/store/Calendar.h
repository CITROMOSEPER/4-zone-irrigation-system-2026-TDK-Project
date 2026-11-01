#pragma once
// Calendar entries in NVS, one blob per list.
//
// Watering {sid, d, s, sec, dt}: d = weekday bitmask (bit0 = Monday); d = 0 means one-off on dt.
// DND window {sid, d, s, e, dt, de}: weekly windows with e <= s run past midnight;
//   a one-off runs from dt at s to de at e.
// List 0 of the DND lists is "all zones"; lists 1..ZONES belong to the zones.
#include <Arduino.h>
#include "Config.h"

struct Watering {
  uint16_t sid;
  uint8_t d;
  uint16_t s;          // minute of day
  uint16_t sec;        // length (Schedule mode); 0 = calculated (loop modes)
  uint32_t dt;         // ymd, one-off only
  uint32_t fired;      // occurrence key of the last time it fired (Scheduler), see Scheduler::occKey
};

struct DndWindow {
  uint16_t sid;
  uint8_t d;
  uint16_t s, e;
  uint32_t dt, de;     // ymd, one-off only
};

struct WaterList { uint8_t n; Watering e[MAX_WATERINGS]; };
struct DndList   { uint8_t n; DndWindow e[MAX_DND]; };

class Calendar {
public:
  WaterList w[ZONES];
  DndList dnd[ZONES + 1];                 // [0] = all zones

  void begin();

  // sid 0 = new. Returns the sid, or 0 when the list is full.
  uint16_t putWatering(uint8_t zone, Watering e);
  bool delWatering(uint8_t zone, uint16_t sid);
  Watering* findWatering(uint8_t zone, uint16_t sid);
  bool saveWaterings(uint8_t zone);

  uint16_t putDnd(uint8_t list, DndWindow e);
  bool delDnd(uint8_t list, uint16_t sid);

  // One-offs that ended more than a week ago are dropped (called at local midnight)
  void prune(uint32_t todayYmd);

private:
  uint16_t newSid();
  bool saveDnd(uint8_t list);
  uint16_t _nextSid = 1;
};

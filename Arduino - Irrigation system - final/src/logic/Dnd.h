#pragma once
// Do-not-disturb: one-off "until" values (global and per zone, from the DND sheet) plus the
// calendar's DND windows (Calendar::dnd). While DND covers a zone, no watering starts there.
#include <Arduino.h>
#include "Config.h"
#include "store/Calendar.h"

class Dnd {
public:
  void begin();
  void update(uint32_t now);              // expires one-off DNDs

  // id 0 = all zones, 1..ZONES = that zone; until 0 = off, DND_FOREVER = until turned off
  void set(uint8_t id, uint32_t until);
  uint32_t until(uint8_t id) const { return id <= ZONES ? _u.until[id] : 0; }

  bool active(uint8_t zone, uint32_t t) const { return kind(zone, t) != 0; }
  // Why: 'z' the zone's one-off, 'g' the global one-off, 'w' a window; 0 = none
  char kind(uint8_t zone, uint32_t t) const;

  // Is t inside one of the list's windows? (local time)
  static bool inWindows(const DndList& l, uint32_t t);

private:
  struct Stored { uint32_t until[ZONES + 1]; };
  void save();
  Stored _u{};
};

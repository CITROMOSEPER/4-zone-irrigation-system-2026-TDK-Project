#include "store/Calendar.h"
#include "util/Nvs.h"
#include "util/TimeUtil.h"

namespace {
const char* NS = "cal";
const char* wKey(uint8_t z) { static char k[8]; snprintf(k, sizeof(k), "w%u", (unsigned)z); return k; }
const char* dKey(uint8_t l) { static char k[8]; snprintf(k, sizeof(k), "d%u", (unsigned)l); return k; }

// Remove element i from a fixed array of n elements
template <typename T> void removeAt(T* a, uint8_t& n, uint8_t i) {
  for (uint8_t j = i + 1; j < n; j++) a[j - 1] = a[j];
  n--;
}
}  // namespace

void Calendar::begin() {
  uint16_t maxSid = 0;
  for (uint8_t z = 0; z < ZONES; z++) {
    if (!Nvs::load(NS, wKey(z), w[z]) || w[z].n > MAX_WATERINGS) w[z].n = 0;
    for (uint8_t i = 0; i < w[z].n; i++) maxSid = max(maxSid, w[z].e[i].sid);
  }
  for (uint8_t l = 0; l <= ZONES; l++) {
    if (!Nvs::load(NS, dKey(l), dnd[l]) || dnd[l].n > MAX_DND) dnd[l].n = 0;
    for (uint8_t i = 0; i < dnd[l].n; i++) maxSid = max(maxSid, dnd[l].e[i].sid);
  }
  _nextSid = maxSid + 1;
}

uint16_t Calendar::newSid() {
  if (_nextSid == 0) _nextSid = 1;
  return _nextSid++;
}

Watering* Calendar::findWatering(uint8_t zone, uint16_t sid) {
  if (zone >= ZONES) return nullptr;
  for (uint8_t i = 0; i < w[zone].n; i++) if (w[zone].e[i].sid == sid) return &w[zone].e[i];
  return nullptr;
}

uint16_t Calendar::putWatering(uint8_t zone, Watering e) {
  if (zone >= ZONES) return 0;
  WaterList& l = w[zone];
  if (e.sid) {
    if (Watering* old = findWatering(zone, e.sid)) {
      *old = e;
      saveWaterings(zone);
      return e.sid;
    }
  }
  if (l.n >= MAX_WATERINGS) return 0;
  e.sid = newSid();
  l.e[l.n++] = e;
  saveWaterings(zone);
  return e.sid;
}

bool Calendar::delWatering(uint8_t zone, uint16_t sid) {
  if (zone >= ZONES) return false;
  WaterList& l = w[zone];
  for (uint8_t i = 0; i < l.n; i++) {
    if (l.e[i].sid != sid) continue;
    removeAt(l.e, l.n, i);
    return saveWaterings(zone);
  }
  return true;   // already gone
}

bool Calendar::saveWaterings(uint8_t zone) {
  return Nvs::save(NS, wKey(zone), w[zone]);
}

uint16_t Calendar::putDnd(uint8_t list, DndWindow e) {
  if (list > ZONES) return 0;
  DndList& l = dnd[list];
  if (e.sid) {
    for (uint8_t i = 0; i < l.n; i++) {
      if (l.e[i].sid != e.sid) continue;
      l.e[i] = e;
      saveDnd(list);
      return e.sid;
    }
  }
  if (l.n >= MAX_DND) return 0;
  e.sid = newSid();
  l.e[l.n++] = e;
  saveDnd(list);
  return e.sid;
}

bool Calendar::delDnd(uint8_t list, uint16_t sid) {
  if (list > ZONES) return false;
  DndList& l = dnd[list];
  for (uint8_t i = 0; i < l.n; i++) {
    if (l.e[i].sid != sid) continue;
    removeAt(l.e, l.n, i);
    return saveDnd(list);
  }
  return true;
}

bool Calendar::saveDnd(uint8_t list) {
  return Nvs::save(NS, dKey(list), dnd[list]);
}

void Calendar::prune(uint32_t today) {
  uint32_t limit = TimeUtil::addDays(today, -7);
  for (uint8_t z = 0; z < ZONES; z++) {
    bool ch = false;
    for (uint8_t i = w[z].n; i-- > 0;) {
      const Watering& e = w[z].e[i];
      if (!e.d && e.dt < limit) { removeAt(w[z].e, w[z].n, i); ch = true; }
    }
    if (ch) saveWaterings(z);
  }
  for (uint8_t l = 0; l <= ZONES; l++) {
    bool ch = false;
    for (uint8_t i = dnd[l].n; i-- > 0;) {
      const DndWindow& e = dnd[l].e[i];
      if (!e.d && (e.de ? e.de : e.dt) < limit) { removeAt(dnd[l].e, dnd[l].n, i); ch = true; }
    }
    if (ch) saveDnd(l);
  }
}

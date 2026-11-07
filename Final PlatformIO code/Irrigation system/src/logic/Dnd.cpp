#include "logic/Dnd.h"
#include "System.h"
#include "util/Nvs.h"
#include "util/TimeUtil.h"

void Dnd::begin() {
  if (!Nvs::load("rt", "dnd", _u)) memset(&_u, 0, sizeof(_u));
}

void Dnd::save() { Nvs::save("rt", "dnd", _u); }

void Dnd::set(uint8_t id, uint32_t until) {
  if (id > ZONES) return;
  _u.until[id] = until;
  save();
}

void Dnd::update(uint32_t now) {
  if (!now) return;
  bool ch = false;
  for (auto& u : _u.until) {
    if (u && u != DND_FOREVER && u <= now) { u = 0; ch = true; }
  }
  if (ch) save();
}

char Dnd::kind(uint8_t zone, uint32_t t) const {
  if (zone >= ZONES) return 0;
  if (t && t < _u.until[zone + 1]) return 'z';
  if (t && t < _u.until[0]) return 'g';
  if (inWindows(sys.cal.dnd[0], t) || inWindows(sys.cal.dnd[zone + 1], t)) return 'w';
  return 0;
}

bool Dnd::inWindows(const DndList& l, uint32_t t) {
  LTime lt;
  if (!TimeUtil::toLocal(t, lt)) return false;
  const uint8_t k = lt.wd, pk = (k + 6) % 7;
  for (uint8_t i = 0; i < l.n; i++) {
    const DndWindow& x = l.e[i];
    if (!x.d) {                                            // one-off: dt s .. de e
      uint32_t f = TimeUtil::localToUnix(x.dt, x.s), u = TimeUtil::localToUnix(x.de ? x.de : x.dt, x.e);
      if (t >= f && t < u) return true;
      continue;
    }
    // weekly: today's window, or yesterday's window running past midnight
    if ((x.d & (1 << k)) && lt.mod >= x.s && (x.e > x.s ? lt.mod < x.e : true)) return true;
    if (x.e <= x.s && (x.d & (1 << pk)) && lt.mod < x.e) return true;
  }
  return false;
}

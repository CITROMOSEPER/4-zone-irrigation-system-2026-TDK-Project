#include "logic/ButtonActions.h"
#include "System.h"

void ButtonActions::handle(ButtonEvent e) {
  switch (e) {
    case ButtonEvent::PRESS: onPress(); break;
    case ButtonEvent::SHORT: onShort(); break;
    case ButtonEvent::LONG:  onLong();  break;
    default: break;
  }
}

void ButtonActions::onPress() {
  sys.fb.press();
  _consumed = false;
  _wasWatering = false;

  if (sys.fb.errorSounding()) {
    sys.fb.muteError();
    _consumed = true;
    return;
  }
  if (sys.cfg.g.off) {                                   // winter mode off
    sys.setAllOff(false);
    sys.fb.winterOff();
    _consumed = true;
    return;
  }
  if (!sys.irr.idle()) {
    _wasWatering = true;
    sys.irr.stopAll(StopReason::USER);
  }
}

bool ButtonActions::refusedNow() {
  GBlock g = Rules::globalBlock();
  if (g == GBlock::FROST || g == GBlock::TANK_LOW || sys.water.faulted()) {
    sys.fb.refused();
    return true;
  }
  return false;
}

void ButtonActions::onShort() {
  if (_consumed || _wasWatering || !sys.cfg.g.btn) return;
  if (refusedNow()) return;

  // Every enabled zone with a known deficit, the driest (largest deficit in mm) first
  uint8_t order[ZONES], n = 0;
  Deficit d[ZONES];
  for (uint8_t z = 0; z < ZONES; z++) {
    if (!sys.cfg.z[z].en || !sys.dose.deficit(z, d[z]) || d[z].sec < MIN_RUN_S) continue;
    uint8_t i = n++;
    while (i > 0 && d[order[i - 1]].mm < d[z].mm) { order[i] = order[i - 1]; i--; }
    order[i] = z;
  }
  if (!n) { sys.fb.noDeficit(); return; }
  for (uint8_t i = 0; i < n; i++) {
    uint8_t z = order[i];
    sys.irr.enqueue(WaterRequest{z, min(d[z].sec, BUTTON_DOSE_MAX_S), Src::DEFICIT, true, true, true});
  }
}

void ButtonActions::onLong() {
  if (_consumed) return;
  sys.fb.longConfirm();
  if (_wasWatering) {                                    // stop (done at press) + winter mode on
    sys.setAllOff(true);
    sys.fb.winterOn();
    return;
  }
  if (refusedNow()) return;
  bool any = false;
  for (uint8_t z = 0; z < ZONES; z++) {
    if (!sys.cfg.z[z].en) continue;
    sys.irr.enqueue(WaterRequest{z, BUTTON_DOSE_MAX_S, Src::MANUAL, true, true, true});
    any = true;
  }
  if (!any) sys.fb.noDeficit();
}

#pragma once
// What the hardware button does (decided 3 Nov 2026).
//
// Press (confirmed):  tick + LED 150, then the first that applies:
//   error alarm sounding    -> mute it; the short press does nothing else
//   watering, queued or soaking -> stop at once, clear the queue
// Short (released before 3 s), only if nothing above happened:
//   winter mode, frost, tank low or fault -> refused (low "bonk" + 3 flashes)
//   btn setting off -> nothing more (stopping and the long press always work)
//   every enabled zone with a deficit, driest first, deficit dose up to 15 min each;
//   none -> "nothing to water" (two soft notes + 2 flashes)
// Long (3 s, while still held): ALWAYS toggles winter mode (All off). Four falling notes (on) or rising
//   notes (off) play the moment 3 s is reached, so they also say "you can let go".
// Button runs override DND and the zone cold limit; frost, tank low and fault refuse.
#include <Arduino.h>
#include "hw/Button.h"

class ButtonActions {
public:
  void handle(ButtonEvent e);

private:
  void onPress();
  void onShort();
  void onLong();
  bool refusedNow();               // winter, frost, tank low or fault: refuse with feedback

  bool _consumed = false;          // this press already did its job (mute or stop)
};

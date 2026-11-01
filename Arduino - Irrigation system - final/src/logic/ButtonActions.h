#pragma once
// What the hardware button does (decided 30 Oct, flowchart 7.8 of the plan).
//
// Press (confirmed):  LED 150 + beep 150, then the first that applies:
//   error buzzer sounding  -> mute it; nothing else for this press
//   winter mode (All off)  -> All off off, beep 750/300/150; nothing else for this press
//   watering or queued     -> stop at once, clear the queue
// Short (release < 3 s), only if nothing above happened and the btn setting is on:
//   frost / tank low / fault -> refused (750 ms beep + 3 flashes)
//   every zone with a deficit, driest first, deficit dose up to 15 min each; none -> 2 beeps + 2 flashes
// Long (3 s):
//   confirm (150/300/150 + 2 flashes); was watering -> winter mode on (150/300/150/300/1000);
//   idle -> every enabled zone, 15 min each, zone order
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
  bool refusedNow();               // frost, tank low or fault: refuse with feedback

  bool _consumed = false;          // this press already did its job (mute, winter off)
  bool _wasWatering = false;
};

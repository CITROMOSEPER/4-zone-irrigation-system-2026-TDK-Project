#pragma once
// One place for the precedence rules and the zone status the UI shows.
//
//   (1) All off, frost (BME280 working), tank low: nothing starts
//   (2) Water now and DND: the last action wins (the UI asks first)
//   (3) Zone disabled, zone cold limit (Water now may override after a warning)
//   (4) Mode logic
#include <Arduino.h>

enum class GBlock : uint8_t { NONE, ALL_OFF, FROST, TANK_LOW };
enum class ZSt : uint8_t { ON, OFF, DND, WATERING, QUEUED, ALLOFF, FAULT, WAIT };

// Bits of the serious errors (LED + buzzer)
enum : uint8_t { ERR_FAULT = 1, ERR_TANK = 2, ERR_CLOCK = 4 };

namespace Rules {

GBlock globalBlock();
const char* blockName(GBlock b);         // "" for NONE

bool belowFrost();                       // false while the BME280 is faulted
bool belowCold(uint8_t zone);
bool rainSensorWet();
bool forecastRain();                     // today's forecast rain >= the rain-stop setting
bool rainSkips(uint8_t zone);            // either one, and the zone is not covered

struct ZoneStatus { ZSt st; const char* why; };
ZoneStatus zoneStatus(uint8_t zone);
const char* stName(ZSt s);

uint8_t seriousErrors();                 // ERR_* bits

}  // namespace Rules

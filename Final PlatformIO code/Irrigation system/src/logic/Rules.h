#pragma once
// One place for the precedence rules and the zone status the UI shows.
//
//   (1) All off, frost (BME280 working), tank low: nothing starts
//   (2) Water now and DND: the last action wins (the UI asks first)
//   (3) Zone disabled, zone cold limit (Water now may override after a warning)
//   (4) Mode logic
#include <Arduino.h>

enum class GBlock : uint8_t { NONE, ALL_OFF, FROST, TANK_LOW };
enum class ZSt : uint8_t { ON, OFF, DND, WATERING, QUEUED, SOAK, ALLOFF, FAULT, WAIT };

// Bits of the serious errors (LED + buzzer)
enum : uint8_t { ERR_FAULT = 1, ERR_TANK = 2, ERR_CLOCK = 4 };

namespace Rules {

GBlock globalBlock();
const char* blockName(GBlock b);         // "" for NONE

bool belowFrost();                       // false while the BME280 is faulted
bool belowCold(uint8_t zone);
bool rainSensorWet();
// Forecast rain over the 24 h from 'at' (0 = now) reaches the rain-stop setting in BOTH models (ICON
// and ECMWF; one model when the other has no data). Research: agreement catches most wet days with
// few false skips; a single model gives more false alarms. Not used beyond WX_RAIN_TRUST_S.
bool forecastRain(uint32_t at = 0);
bool rainSkips(uint8_t zone);            // the rain sensor or the forecast, and the zone is not covered

struct ZoneStatus { ZSt st; const char* why; };
ZoneStatus zoneStatus(uint8_t zone);
const char* stName(ZSt s);

uint8_t seriousErrors();                 // ERR_* bits

}  // namespace Rules

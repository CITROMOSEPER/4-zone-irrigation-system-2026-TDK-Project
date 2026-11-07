#pragma once
// Compile-time constants: hard safety ceilings and defaults.
// Runtime values live in Settings (NVS) and are clamped to the ceilings here, so a bad
// write from the web page can only make watering more conservative, never exceed them.
#include <stdint.h>
#include <stddef.h>

constexpr const char* FW_VERSION = "1.3.2";
constexpr const char* HW_NAME    = "ESP-WROOM32-CH340-USB-C";

constexpr uint8_t ZONES = 4;

// ---- Relay board (measured: switches ON with a HIGH input) ----
constexpr bool RELAY_ACTIVE_LOW = false;

// ---- Water safety (WaterController) ----
constexpr uint32_t PUMP_START_DELAY_MS  = 1500;        // valve opens, then the pump starts
constexpr uint32_t VALVE_CLOSE_DELAY_MS = 1500;        // pump stops, then the valve closes
constexpr uint32_t MIN_RUN_S            = 2;           // short runs: the scale model applies ~0.4 mm a second
constexpr uint32_t MAX_RUN_S            = 60 * 60;     // one run
constexpr uint32_t RUN_SAVE_S           = 60;          // a running watering is saved to flash this often (power cuts)
constexpr uint32_t RESUME_WINDOW_S      = 30 * 60;     // power back within this: the cut run's rest is watered...
constexpr uint32_t RESUME_DELAY_MS      = 10000;       // ...once the sensors have settled after the boot
constexpr uint32_t MAX_ZONE_DAY_S       = 240 * 60;    // ceiling for the "daily limit per zone" setting
constexpr uint32_t HARD_CAP_MARGIN_MS   = 30000;       // each output's own backstop, above MAX_RUN_S

// ---- Tank, HC-SR04 on top looking down ----
constexpr float    TANK_MIN_VALID_CM = 2.0f;
constexpr float    TANK_MAX_VALID_CM = 400.0f;
constexpr uint32_t TANK_PERIOD_MS    = 200;            // one ping every 200 ms...
constexpr uint8_t  TANK_MEDIAN_N     = 5;              // ...the level is the median of the last 5 (two bad echoes in a row change nothing)
constexpr uint8_t  TANK_BAD_TO_LOW   = 2;              // consecutive bad medians -> LOW
constexpr uint8_t  TANK_GOOD_TO_OK   = 3;              // consecutive good medians -> OK
constexpr uint8_t  TANK_MISS_TO_LOW  = 25;             // 5 s without a valid median (no echo) -> LOW; shorter dropouts are ignored
constexpr int      TANK_LOW_PCT      = 10;             // below this fill level the tank counts as low...
constexpr int      TANK_OK_PCT       = 15;             // ...and only above this it is OK again (hysteresis)
constexpr float    TANK_SHOW_STEP_CM = 0.2f;           // the shown level moves only when the reading moved this much

// ---- Soil sensors (capacitive: higher raw = drier) ----
constexpr int      SOIL_RAW_MIN_OK   = 100;            // outside = disconnected or shorted
constexpr int      SOIL_RAW_MAX_OK   = 4000;
constexpr uint32_t SOIL_PERIOD_MS    = 1000;

// ---- Rain sensor SA-28: powered only while sampling (electrode corrosion) ----
constexpr uint32_t RAIN_SAMPLE_PERIOD_MS = 30000;
constexpr uint32_t RAIN_POWERUP_MS       = 300;

// ---- BME280 ----
constexpr uint32_t ENV_PERIOD_MS   = 5000;
constexpr uint8_t  ENV_FAULT_COUNT = 3;

// ---- Button (pressed = HIGH: 1k pull-down, button to 3.3 V) ----
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;
constexpr uint32_t BUTTON_LONG_MS     = 3000;
constexpr uint32_t BUTTON_DOSE_MAX_S  = 15 * 60;       // per zone, short press (deficit fill)

// ---- Local time: the RTC stays in UTC, local time only where a schedule is decided ----
constexpr const char* LOCAL_TZ = "CET-1CEST,M3.5.0,M10.5.0/3";   // Europe/Budapest
constexpr uint32_t RTC_MIN_VALID_UNIX = 1767225600;               // 2026-01-01 UTC
constexpr uint32_t RTC_MAX_VALID_UNIX = 2524608000;               // 2050-01-01 UTC: a DS1302 misread (e.g. 2090) is not a time
constexpr uint8_t  RTC_CONFIRM_READS  = 3;                        // a jump in the RTC is believed after this many agreeing reads
constexpr uint32_t DND_FOREVER        = 4102444800UL;             // "until I turn it back on"
constexpr uint32_t SNTP_PERIOD_MS     = 6UL * 3600 * 1000;

// ---- Calendar capacity ----
constexpr uint8_t MAX_WATERINGS = 16;                  // per zone
constexpr uint8_t MAX_DND       = 8;                   // per zone, and for all zones

// ---- Defaults (all TBD on the bench) ----
constexpr const char* DEFAULT_NAME   = "irrigation";
constexpr float    DEFAULT_FROST_C   = 2.0f;
constexpr int8_t   DEFAULT_ZONE_COLD_C = 4;
constexpr uint16_t DEFAULT_CAP_MIN   = 90;
constexpr float    DEFAULT_RAIN_MM   = 2.0f;
constexpr uint16_t DEFAULT_QTO_MIN   = 120;
constexpr float    DEFAULT_LAT       = 47.4979f;       // Budapest
constexpr float    DEFAULT_LON       = 19.0402f;
constexpr float    DEFAULT_AREA_M2   = 1.0f;           // zone area; the sprinkler preset's mm/h sets the flow
constexpr uint16_t DEFAULT_TANK_EMPTY_CM = 72;
constexpr uint16_t DEFAULT_TANK_FULL_CM  = 8;
constexpr int      DEFAULT_SOIL_DRY  = 2800;
constexpr int      DEFAULT_SOIL_WET  = 1200;
constexpr uint32_t SCHEDULE_DEFAULT_S = 900;           // a Schedule entry without a length

// ---- Cycle and soak: a dose faster than the soil can take it is split into bursts ----
constexpr float    SOAK_STORE_MM     = 2.0f;           // water that may stand on the surface before it runs off
constexpr uint32_t SOAK_MIN_S        = 30;             // pause between bursts: at least...
constexpr uint32_t SOAK_MAX_S        = 3600;           // ...and at most
constexpr uint32_t BURST_MIN_S       = 3;

// ---- Closed loop ----
constexpr float    RISE_MIN_PCT      = 2.0f;           // soil must rise this much after a run
constexpr uint8_t  RISE_MISSES_MAX   = 3;              // misses in a row -> fall back to Schedule
constexpr uint32_t RISE_CHECK_DELAY_MS = 10UL * 60 * 1000;

// ---- Weather (Open-Meteo, no key) ----
constexpr uint32_t WX_PERIOD_MS       = 2UL * 3600 * 1000;
constexpr uint32_t WX_RETRY_MS        = 15UL * 60 * 1000;
constexpr uint32_t WX_STACK           = 12288;         // the weather task (HTTPS); /health shows what is left
constexpr uint32_t WX_FRESH_S         = 12UL * 3600;   // older forecasts are not used for rain skips
constexpr float    RAIN_EFFECTIVE     = 0.8f;          // share of rain that reaches the root zone
constexpr uint32_t WX_RAIN_TRUST_S    = 48UL * 3600;   // rain forecasts beyond this are not used (research: noise after ~3 days)

// ---- Alerts ----
constexpr int      WIFI_UNUSABLE_RSSI = -85;           // logged only when the signal stays this weak...
constexpr uint16_t WIFI_ALERT_S       = 300;           // ...or the connection stays lost, this long

// ---- Log ----
constexpr size_t   LOG_TOTAL_MAX      = 480 * 1024;    // /log/YYYY-MM.csv: the oldest month goes above this
constexpr uint8_t  HIST_ROWS          = 5;             // per zone, for the zone view

// ---- Statistics ----
constexpr uint32_t STAT_FAST_S        = 3;             // RAM: soil, tank, outputs every 3 s...
constexpr uint16_t STAT_FAST_N        = 3600;          // ...for 3 hours
constexpr uint32_t STAT_MIN_S         = 60;            // RAM: raw readings, weather and estimates every minute...
constexpr uint16_t STAT_MIN_N         = 180;           // ...for 3 hours
constexpr uint32_t STAT_Q_S           = 900;           // flash: 15-minute averages...
constexpr uint16_t STAT_Q_N           = 35 * 96;       // ...for 5 weeks

// ---- Misc ----
constexpr uint32_t WDT_TIMEOUT_S = 10;

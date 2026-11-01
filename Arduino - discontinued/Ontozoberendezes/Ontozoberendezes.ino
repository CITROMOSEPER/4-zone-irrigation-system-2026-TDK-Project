// 4 zónás öntözőberendezés
// ESP32
// Nemes Dániel
//
// Required libraries (Library Manager):
//   - Adafruit BME280 Library (+ Adafruit Unified Sensor, Adafruit BusIO)
//   - Rtc by Makuna (DS1302 support)
//   - ArduinoJson (version 7.x)
//   - WiFi, WebServer, DNSServer, HTTPClient, WiFiClientSecure, Preferences (ESP32 core)
//
// PINS.h must be in the same folder as this sketch.
//
// ---------------------------------------------------------------------------
// SERIAL COMMANDS (921600 baud, every command is one line terminated with \n)
//
//   HELP              list commands
//   ID                identify the device, answer: ID <DEVICE_ID> <FW_VERSION> (used by the PC editor to find the port)
//   PING              keep-alive, answer: PONG 1 (PC link established) or PONG 0 (no PC link, send HELLO)
//   HELLO             the PC editor connected: chime, answer OK HELLO
//   BYE               the PC editor disconnected: reverse chime, answer OK BYE
//   PRINT             print all internal data once
//   AUTOPRINT 0|1     turn the periodic data print off/on (off by default)
//   TT PARAMS         list condition parameters in the order used by the timetable
//   TT GET            download the whole timetable as ONE line
//   TT SET <payload>  upload the whole timetable as ONE line (replaces everything)
//   TT CLEAR          delete the whole timetable
//   CFG GET           print the settings as ONE line: CFG SET <apikey>;<lat>;<lon>;<timezone>;<high_cm>;<low_cm>;<beeper>
//   CFG SET <payload> change the settings (saved to flash), payload = <apikey>;<lat>;<lon>;<timezone>;<high_cm>;<low_cm>;<beeper>
//                     (the last three fields may be left out, the tank levels and the beeper then stay as they are;
//                      the beeper field can only be sent together with the tank levels)
//
// PC LINK
//
//   After HELLO the PC link is active. Every received line (normally PING, sent about every 1.5 s) refreshes it.
//   If nothing is received for LINK_TIMEOUT_MS the link is considered broken and the reverse chime is played.
//
// SETTINGS (CFG)
//
//   apikey    OpenWeatherMap key (letters, digits, - and _ , max 64 characters)
//   lat, lon  decimal degrees, e.g. 47.4979;19.0402
//   timezone  POSIX TZ string, e.g. CET-1CEST,M3.5.0,M10.5.0/3 (Central Europe with DST), UTC0
//   high_cm   HC-SR04+ reading (distance from the sensor to the water) when the tank is full, 2-400 cm
//   low_cm    HC-SR04+ reading when the tank is empty, 2-400 cm, at least 5 cm more than high_cm
//   beeper    1 = the buzzer is on (default), 0 = the buzzer never makes a sound (all chimes and beeps are silenced)
//   Example:  CFG SET 0123456789abcdef0123456789abcdef;47.4979;19.0402;CET-1CEST,M3.5.0,M10.5.0/3;10;90;1
//
// WATER TANK
//
//   The ultrasonic sensor sits on top of the tank and points down, so a SHORTER distance means MORE water.
//   level % = (low_cm - distance) / (low_cm - high_cm) * 100. The error LED stays on below 10 %.
//
// WEATHER FETCHING
//
//   Only while WiFi is up (no WiFi: nothing is even tried). Current weather and forecast are fetched as soon as they
//   are unreliable (missing or stale), plus on a schedule (current weather every 15 minutes, forecast every 24 hours).
//   A fetch is tried 3 times in a row; when all 3 fail, that data rests for 30 minutes and then tries 3 times again,
//   and so on until it works.
//
// TIMETABLE LINE FORMAT
//
//   TT SET <n>;<event>;<event>;...;<event>
//
//   <n> is the number of events (used as an integrity check).
//   TT GET prints exactly this format, so its output can be pasted back into the
//   serial monitor to restore a timetable.
//
//   <event> = days,start,end,zones,reliable,c0,c1,...,c15     (21 comma separated fields)
//
//     days      7 characters of 0/1, Monday first ("1111100" = Mon-Fri)
//     start     HHMM, 24h local time ("0630")
//     end       HHMM, 24h local time ("0645"); may be before start (crosses midnight)
//     zones     4 characters of 0/1, zone 1 first ("1010" = zones 1 and 3)
//     reliable  1 = only run if ALL data is reliable, 0 = run on sensor data alone
//     c0..c15   one condition per parameter (order from TT PARAMS):
//                 *        parameter is ignored (always true)
//                 min:max  event may start only if min <= value <= max
//
//   Example (two events):
//   TT SET 2;1111100,0600,0615,1100,1,30:60,*,*,*,5:35,*,*,*,*,*,*,*,*,*,*,*;0000011,1800,1830,0011,0,*,*,30:70,*,*,*,*,*,*,*,*,*,*,*,*,*
// ---------------------------------------------------------------------------

#include "PINS.h"
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <ThreeWire.h>
#include <RtcDS1302.h>
#include <sys/time.h>
#include <math.h>

// ----- Configuration ------------------------------------------------------------------------------------------------

// OpenWeatherMap and time zone: these are only the DEFAULTS, CFG SET overrides them (stored in flash)
String OWM_API_KEY = "";   // set your own key here or with CFG SET
float LOCATION_LAT = 47.4979f;   // Budapest, change as needed
float LOCATION_LON = 19.0402f;

// Water tank, only the DEFAULTS, CFG SET overrides them (stored in flash).
// The HC-SR04+ sits on top of the tank and measures the distance to the water, so the HIGH level is the SHORT distance.
float TANK_HIGH_CM = 10.0f;   // sensor to water when the tank is full (100 %)
float TANK_LOW_CM = 90.0f;    // sensor to water when the tank is empty (0 %)

// Buzzer on/off, only the DEFAULT, CFG SET overrides it (stored in flash). Read by the indicator task and written
// by the serial task, hence volatile.
volatile bool BEEPER_ENABLED = true;

// Time
String TIMEZONE = "CET-1CEST,M3.5.0,M10.5.0/3";   // Central Europe with DST
const char* NTP_SERVER = "pool.ntp.org";

// WiFi portal
const char* AP_SSID = "Ontozo-Setup";
String AP_PASSWORD = "";   // empty = open network, otherwise at least 8 characters

// Button wiring (original sketch used a plain INPUT, so an external pulldown is assumed)
#define BUTTON_ACTIVE_LEVEL HIGH
#define BUTTON_PIN_MODE INPUT

// Output wiring
#define SOLENOID_ON_LEVEL HIGH

// If true, holding the button at power-up also forgets the stored WiFi credentials
const bool BUTTON_AT_BOOT_CLEARS_CREDENTIALS = true;

// Timing
const uint32_t WIFI_CONNECT_TIMEOUT_MS = 30UL * 1000UL;
const uint32_t PORTAL_TIMEOUT_MS = 2UL * 60UL * 1000UL;
const uint32_t WIFI_RETRY_INTERVAL_MS = 15UL * 60UL * 1000UL;   // reconnect attempts while in offline mode
const uint32_t NTP_INTERVAL_MS = 24UL * 60UL * 60UL * 1000UL;
const uint32_t NTP_RETRY_MS = 60UL * 60UL * 1000UL;

// Passive buzzer tones (driven with a square wave, a plain HIGH only gives a click)
const uint16_t BEEP_FAIL_HZ = 1000;    // failed API call (low)
const uint16_t BEEP_START_HZ = 3000;   // irrigation event start (high)
const uint16_t BEEP_END_HZ = 2500;     // irrigation event end (slightly lower than start)
const uint16_t BEEP_MS = 500;
const uint16_t BEEP_GAP_MS = 150;      // silence between queued beeps
const uint8_t BUZZER_LEDC_CHANNEL = 0; // only used with ESP32 Arduino core 2.x

// Water level (HC-SR04+ ultrasonic sensor)
const float SENSOR_MIN_CM = 2.0f;            // range of the sensor
const float SENSOR_MAX_CM = 400.0f;
const float TANK_MIN_SPAN_CM = 5.0f;         // high and low level must be at least this far apart
const float WATER_LOW_ON_PERCENT = 10.0f;    // the error LED turns on below this level ...
const float WATER_LOW_OFF_PERCENT = 12.0f;   // ... and off again above this one (so it does not flicker at the edge)
const uint32_t ECHO_TIMEOUT_US = 30000UL;    // no echo within 30 ms (about 5 m) means no measurement
const uint8_t WATER_MAX_FAILS = 5;           // this many failed measurements in a row (one per second) = sensor lost

const uint32_t SCHED_INTERVAL_MS = 1000;                   // the whole timetable is re-evaluated this often
const uint32_t ZONE_MIN_DWELL_MS = 30UL * 1000UL;          // after a zone switched, conditions may not switch it again for this long (anti flapping)
const uint32_t SENSOR_INTERVAL_MS = 1000;
const uint32_t PRINT_INTERVAL_MS = 10000;
const time_t FORECAST_INTERVAL_S = 24L * 3600L;
const time_t CURRENT_REFRESH_S = 15L * 60L;   // periodic current weather refresh while online

// Fetching weather data (only while WiFi is up): unreliable data is fetched at once, FETCH_ATTEMPTS tries in a row.
// When all of them fail, that data rests for FETCH_BACKOFF_MS before the next series of tries.
const uint8_t FETCH_ATTEMPTS = 3;
const uint32_t FETCH_ATTEMPT_GAP_MS = 2000;
const uint32_t FETCH_BACKOFF_MS = 30UL * 60UL * 1000UL;

// Data older than this is stale and ignored
const time_t CURRENT_MAX_AGE_S = 2L * 3600L;
const time_t FORECAST_MAX_AGE_S = 7L * 24L * 3600L;

// Soil sensor calibration (raw 12 bit ADC values, SOILCAP gives HIGHER values when DRIER). Calibrate per zone.
int SOIL_RAW_DRY[4] = {3000, 3000, 3000, 3000};   // value in dry air / dry soil (0 %)
int SOIL_RAW_WET[4] = {1300, 1300, 1300, 1300};   // value in water / saturated soil (100 %)

// ----- Types --------------------------------------------------------------------------------------------------------

const int MAX_EVENTS = 24;
const uint8_t TT_VERSION = 1;

// Condition parameters, the order here is the order of the fields in the timetable format
enum Param : uint8_t
{
  P_SOIL1, P_SOIL2, P_SOIL3, P_SOIL4,                                  // sensors
  P_TEMP, P_AIR_HUM, P_PRESSURE,                                       // BME280
  P_W_TEMP, P_W_HUM, P_W_PRESSURE, P_W_WIND, P_W_CLOUDS, P_W_RAIN1H,   // current weather
  P_F_POP, P_F_RAIN, P_F_TMAX,                                         // forecast for today
  P_COUNT
};

struct ParamInfo
{
  const char* name;
  const char* unit;
};

const ParamInfo PARAM_INFO[P_COUNT] =
{
  {"SOIL1", "%"}, {"SOIL2", "%"}, {"SOIL3", "%"}, {"SOIL4", "%"},
  {"TEMP", "C"}, {"AIR_HUM", "%"}, {"PRESSURE", "hPa"},
  {"W_TEMP", "C"}, {"W_HUM", "%"}, {"W_PRESSURE", "hPa"}, {"W_WIND", "m/s"}, {"W_CLOUDS", "%"}, {"W_RAIN1H", "mm"},
  {"F_POP", "%"}, {"F_RAIN", "mm"}, {"F_TMAX", "C"}
};

struct Condition
{
  float min;   // NaN = parameter ignored
  float max;
};

struct IrrigationEvent
{
  uint8_t daysMask;          // bit0 = Monday ... bit6 = Sunday
  uint8_t zoneMask;          // bit0 = zone 1 ... bit3 = zone 4
  uint8_t requireReliable;   // 1 = only run when all data is reliable
  uint8_t reserved;
  uint16_t startMin;         // minutes since midnight
  uint16_t endMin;
  Condition cond[P_COUNT];
};

struct CurrentWeather
{
  bool valid;
  time_t fetchedAt;
  float temp;       // C
  float humidity;   // %
  float pressure;   // hPa
  float wind;       // m/s
  float clouds;     // %
  float rain1h;     // mm
};

struct DayForecast
{
  int32_t dateKey;   // yyyymmdd, local date
  float tMin;
  float tMax;
  float pop;         // probability of precipitation, %
  float rain;        // mm
};

struct WeeklyForecast
{
  uint8_t count;
  time_t fetchedAt;
  DayForecast days[8];
};

struct WeatherSnapshot
{
  CurrentWeather cur;
  WeeklyForecast fc;
  DayForecast today;
  bool curOk;   // current weather present and not stale
  bool fcOk;    // forecast has an entry for today and is not stale
};

struct Reliability
{
  bool sensors;
  bool current;
  bool forecast;
};

enum TimeSource : uint8_t
{
  TS_NONE,
  TS_RTC,
  TS_NTP
};

enum NetState : uint8_t
{
  NET_OFFLINE,
  NET_ONLINE,
  NET_PORTAL
};

enum EvalResult : uint8_t
{
  EV_START,
  EV_SKIP_UNRELIABLE,
  EV_SKIP_CONDITION
};

enum FetchResult : uint8_t
{
  FETCH_OK,
  FETCH_FAILED,    // every attempt failed
  FETCH_SKIPPED    // nothing was tried (no WiFi, no clock) or the series was interrupted
};

// ----- Raw sensor data ----------------------------------------------------------------------------------------------

// SOILCAP-V20 x4
int SOIL_HUMIDITY[4] = {};         // raw ADC
float SOIL_PERCENT[4] = {};        // calibrated 0..100 %

// Bosch BME280 (I2C)
float SENSOR_TEMP = 0;             // C
float SENSOR_AIR_HUMIDITY = 0;     // %
float SENSOR_AIR_PRESSURE = 0;     // hPa
bool BME_OK = false;               // module found on the bus
bool BME_VALID = false;            // last reading was valid

// HC-SR04+ ultrasonic sensor on top of the water tank
float WATER_DISTANCE_CM = 0;             // sensor to water surface, the water level in centimeters
float WATER_PERCENT = 0;                 // 0 % = low level, 100 % = high level
bool WATER_VALID = false;                // there is a recent reading
uint8_t WATER_FAILS = 0;                 // measurements in a row without a usable echo
volatile bool WATER_LOW_ALERT = false;   // close to empty: the error LED stays on

Adafruit_BME280 bme;

// ----- Fetched weather data (OpenWeatherMap API) --------------------------------------------------------------------

CurrentWeather CURRENT_WEATHER = {};
WeeklyForecast WEEKLY_FORECAST = {};

// Handshake between the scheduler (core 1) and the network task (core 0)
volatile bool weatherRequested = false;
volatile bool weatherDone = false;
volatile bool weatherOk = false;

// ----- Fetched time & date (pool.ntp.org, DS1302) -------------------------------------------------------------------

ThreeWire rtcWire(RTC_DAT_PIN, RTC_CLK_PIN, RTC_RST_PIN);
RtcDS1302<ThreeWire> Rtc(rtcWire);

volatile TimeSource timeSource = TS_NONE;
volatile bool timeResynced = false;
time_t lastNtpSync = 0;

// ----- Timetable & conditions ---------------------------------------------------------------------------------------

IrrigationEvent TIMETABLE[MAX_EVENTS];
int TIMETABLE_COUNT = 0;

// Zone outputs
const uint8_t SOLENOID_PINS[4] = {SOLENOID_1_PIN, SOLENOID_2_PIN, SOLENOID_3_PIN, SOLENOID_4_PIN};
bool zoneOn[4] = {false, false, false, false};

// Scheduler state lives inside RUN_SCHEDULER (core 1 only)

// ----- WiFi / network state -----------------------------------------------------------------------------------------

volatile NetState netState = NET_OFFLINE;
volatile bool wifiUp = false;
volatile bool portalRequested = false;
bool startupButton = false;

String WIFI_SSID = "";
String WIFI_PASS = "";

uint32_t nextWifiTry = 0;
uint32_t nextNtpAt = 0;

// After FETCH_ATTEMPTS failed tries a kind of data rests for FETCH_BACKOFF_MS. This is a flag plus a deadline: a bare
// deadline that is never renewed would stop being "due" after 24.8 days of uptime (millis() comparison range).
struct Backoff
{
  bool active;
  uint32_t until;
};

Backoff currentBackoff = {false, 0};
Backoff forecastBackoff = {false, 0};
volatile bool weatherRefetch = false;   // CFG SET asked for fresh data right now
volatile bool netSettled = false;        // first connect attempt and first current weather fetch are over (scheduler may start)
volatile bool firstWeatherDone = false;  // first current weather fetch was attempted (success or not)

// Portal state
WebServer server(80);
DNSServer dnsServer;
String scanOptions = "";
bool portalTimed = false;
uint32_t portalDeadline = 0;
bool portalConnecting = false;
uint8_t portalResult = 0;   // 0 = none, 1 = ok, 2 = failed
uint32_t portalConnectStart = 0;
uint32_t portalFinishAt = 0;
String portalSsid = "";
String portalPass = "";

bool buttonWasPressed = false;
uint32_t buttonLastEdge = 0;

// ----- Indicators ---------------------------------------------------------------------------------------------------

struct BeepItem
{
  uint16_t hz;
  uint16_t ms;
  uint16_t gap;   // silence after this note
};

const uint8_t BEEP_QUEUE_SIZE = 8;
BeepItem beepQueue[BEEP_QUEUE_SIZE];
uint8_t beepHead = 0;
uint8_t beepCount = 0;
portMUX_TYPE beepMux = portMUX_INITIALIZER_UNLOCKED;

bool AUTO_PRINT = false;

// PC link (editor application)
const char* DEVICE_ID = "IRRIGATION-4ZONE";   // the editor looks for this in the answer to the ID command
const char* FW_VERSION = "1.2";
const uint32_t LINK_TIMEOUT_MS = 5000;        // no line from the PC for this long --> link lost
volatile bool linkActive = false;
volatile uint32_t linkLastRx = 0;

// ----- Threading ----------------------------------------------------------------------------------------------------

SemaphoreHandle_t dataMutex = nullptr;

struct DataLock
{
  DataLock()
  {
    xSemaphoreTake(dataMutex, portMAX_DELAY);
  }

  ~DataLock()
  {
    xSemaphoreGive(dataMutex);
  }
};



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Small helpers

static inline bool DUE(uint32_t deadline)
{
  return (int32_t)(millis() - deadline) >= 0;
}

bool timeIsValid()
{
  return timeSource != TS_NONE;
}

const char* TIME_SOURCE_NAME()
{
  switch (timeSource)
  {
    case TS_NTP: return "NTP";
    case TS_RTC: return "RTC";
    default: return "none";
  }
}

String FORMAT_TIME(time_t t)
{
  if (t <= 0)
  {
    return "unknown";
  }
  struct tm tmv;
  localtime_r(&t, &tmv);
  char buf[24];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
  return String(buf);
}

int32_t DATE_KEY(time_t t)
{
  struct tm tmv;
  localtime_r(&t, &tmv);
  return (tmv.tm_year + 1900) * 10000 + (tmv.tm_mon + 1) * 100 + tmv.tm_mday;
}

bool AGE_OK(time_t fetchedAt, time_t now, time_t maxAge)
{
  long long age = (long long)now - (long long)fetchedAt;
  return age >= -300 && age <= (long long)maxAge;
}

void BUZZER_INIT()
{
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(BUZZER_PIN, 2000, 8);
  ledcWriteTone(BUZZER_PIN, 0);
#else
  ledcSetup(BUZZER_LEDC_CHANNEL, 2000, 8);
  ledcAttachPin(BUZZER_PIN, BUZZER_LEDC_CHANNEL);
  ledcWriteTone(BUZZER_LEDC_CHANNEL, 0);
#endif
}

void BUZZER_TONE(uint16_t hz)   // 0 = silence
{
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWriteTone(BUZZER_PIN, hz);
#else
  ledcWriteTone(BUZZER_LEDC_CHANNEL, hz);
#endif
}

// Queue one note (safe to call from any task). Notes are played one after another by the indicator task.
void BEEP_NOTE(uint16_t hz, uint16_t ms, uint16_t gap)
{
  if (!BEEPER_ENABLED)
  {
    return;   // the beeper is switched off in the settings
  }
  portENTER_CRITICAL(&beepMux);
  if (beepCount < BEEP_QUEUE_SIZE)
  {
    beepQueue[(beepHead + beepCount) % BEEP_QUEUE_SIZE] = {hz, ms, gap};
    beepCount++;
  }
  portEXIT_CRITICAL(&beepMux);
}

void BEEP(uint16_t hz, uint16_t ms)
{
  Serial.printf("[BUZZER] beep %u Hz\n", hz);
  BEEP_NOTE(hz, ms, BEEP_GAP_MS);
}

// Boot chime: three rising notes (C7 E7 G7)
void CHIME_BOOT()
{
  Serial.println(F("[BUZZER] boot chime"));
  BEEP_NOTE(2093, 130, 40);
  BEEP_NOTE(2637, 130, 40);
  BEEP_NOTE(3136, 220, 150);
}

// WiFi connected chime: two quick notes (E7 B7)
void CHIME_WIFI()
{
  Serial.println(F("[BUZZER] wifi chime"));
  BEEP_NOTE(2637, 100, 30);
  BEEP_NOTE(3951, 220, 150);
}

// PC link chime: four rising notes (A major arpeggio). The "link lost" chime plays the same notes backwards.
const uint16_t LINK_HZ[4] = {1760, 2217, 2637, 3520};
const uint16_t LINK_MS[4] = {90, 90, 90, 220};

void CHIME_LINK(bool up)
{
  Serial.println(up ? F("[BUZZER] link up chime") : F("[BUZZER] link down chime"));
  for (int i = 0; i < 4; i++)
  {
    int k = up ? i : 3 - i;
    BEEP_NOTE(LINK_HZ[k], LINK_MS[k], (i == 3) ? 150 : 30);
  }
}

void LINK_UP()   // the PC said HELLO
{
  linkLastRx = millis();
  linkActive = true;
  CHIME_LINK(true);
}

void LINK_DOWN()   // the PC said BYE, or went silent
{
  if (linkActive)
  {
    linkActive = false;
    CHIME_LINK(false);
  }
}

void SERVICE_LINK()   // called from loop()
{
  if (linkActive && (uint32_t)(millis() - linkLastRx) > LINK_TIMEOUT_MS)
  {
    Serial.println(F("[LINK] PC went silent"));
    LINK_DOWN();
  }
}

bool BEEP_POP(BeepItem& item)
{
  bool got = false;
  portENTER_CRITICAL(&beepMux);
  if (beepCount > 0)
  {
    item = beepQueue[beepHead];
    beepHead = (beepHead + 1) % BEEP_QUEUE_SIZE;
    beepCount--;
    got = true;
  }
  portEXIT_CRITICAL(&beepMux);
  return got;
}

void BEEP_FAIL()
{
  BEEP(BEEP_FAIL_HZ, BEEP_MS);
}

void BEEP_START()
{
  BEEP(BEEP_START_HZ, BEEP_MS);
}

void BEEP_END()
{
  BEEP(BEEP_END_HZ, BEEP_MS);
}

String FMT_FLOAT(float v)
{
  String s = String(v, 2);
  if (s.indexOf('.') >= 0)
  {
    while (s.endsWith("0"))
    {
      s.remove(s.length() - 1);
    }
    if (s.endsWith("."))
    {
      s.remove(s.length() - 1);
    }
  }
  return s;
}

String FMT_HHMM(uint16_t minutes)
{
  char buf[5];
  snprintf(buf, sizeof(buf), "%02d%02d", minutes / 60, minutes % 60);
  return String(buf);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// RTC (DS1302) and system clock

// The ESP32 system clock (with TZ set) is the single time source for the whole program.
// The RTC stores LOCAL time.

bool LOAD_TIME_FROM_RTC()
{
  if (!Rtc.GetIsRunning())
  {
    return false;
  }
  RtcDateTime dt = Rtc.GetDateTime();
  if (dt.Year() < 2024 || dt.Year() > 2099)
  {
    return false;
  }

  struct tm t = {};
  t.tm_year = dt.Year() - 1900;
  t.tm_mon = dt.Month() - 1;
  t.tm_mday = dt.Day();
  t.tm_hour = dt.Hour();
  t.tm_min = dt.Minute();
  t.tm_sec = dt.Second();
  t.tm_isdst = -1;

  struct timeval tv;
  tv.tv_sec = mktime(&t);
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);

  timeSource = TS_RTC;
  return true;
}

void WRITE_TIME_TO_RTC()
{
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  RtcDateTime dt(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);

  if (Rtc.GetIsWriteProtected())
  {
    Rtc.SetIsWriteProtected(false);
  }
  if (!Rtc.GetIsRunning())
  {
    Rtc.SetIsRunning(true);
  }
  Rtc.SetDateTime(dt);
}

bool NTP_QUERY(time_t& epochOut)
{
  IPAddress ip;
  if (!WiFi.hostByName(NTP_SERVER, ip))
  {
    return false;
  }

  WiFiUDP udp;
  if (!udp.begin(2390))
  {
    return false;
  }

  uint8_t pkt[48] = {};
  pkt[0] = 0x1B;   // LI = 0, version 3, mode 3 (client)
  udp.beginPacket(ip, 123);
  udp.write(pkt, 48);
  udp.endPacket();

  uint32_t start = millis();
  while (millis() - start < 2000)
  {
    if (udp.parsePacket() >= 48)
    {
      udp.read(pkt, 48);
      udp.stop();
      uint32_t secs = ((uint32_t)pkt[40] << 24) | ((uint32_t)pkt[41] << 16) | ((uint32_t)pkt[42] << 8) | (uint32_t)pkt[43];
      if (secs < 2208988800UL)
      {
        return false;
      }
      time_t epoch = (time_t)(secs - 2208988800UL);
      if (epoch < 1700000000)   // sanity check (before Nov 2023 means garbage)
      {
        return false;
      }
      epochOut = epoch;
      return true;
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
  udp.stop();
  return false;
}

// Fetch the time from pool.ntp.org, set the system clock and sync the DS1302
bool FETCH_TIME_DATE()
{
  if (!wifiUp)
  {
    return false;
  }

  for (int attempt = 0; attempt < 3; attempt++)
  {
    time_t epoch;
    if (NTP_QUERY(epoch))
    {
      struct timeval tv;
      tv.tv_sec = epoch;
      tv.tv_usec = 0;
      settimeofday(&tv, nullptr);

      timeSource = TS_NTP;
      lastNtpSync = epoch;
      timeResynced = true;
      WRITE_TIME_TO_RTC();
      return true;
    }
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
  return false;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// NVS persistence

void SAVE_TIMETABLE()
{
  Preferences p;
  if (!p.begin("timetable", false))
  {
    return;
  }
  p.putUChar("ver", TT_VERSION);
  p.putUChar("n", (uint8_t)TIMETABLE_COUNT);
  if (TIMETABLE_COUNT > 0)
  {
    p.putBytes("ev", TIMETABLE, sizeof(IrrigationEvent) * TIMETABLE_COUNT);
  }
  else
  {
    p.remove("ev");
  }
  p.end();
}

void LOAD_TIMETABLE()
{
  TIMETABLE_COUNT = 0;
  Preferences p;
  if (!p.begin("timetable", true))
  {
    return;
  }
  uint8_t n = p.getUChar("n", 0);
  if (p.getUChar("ver", 0) == TT_VERSION && n <= MAX_EVENTS)
  {
    if (n == 0)
    {
      p.end();
      return;
    }
    if (p.getBytesLength("ev") == sizeof(IrrigationEvent) * n)
    {
      p.getBytes("ev", TIMETABLE, sizeof(IrrigationEvent) * n);
      TIMETABLE_COUNT = n;
    }
  }
  p.end();
}

void SAVE_FORECAST(const WeeklyForecast& f)
{
  Preferences p;
  if (!p.begin("weather", false))
  {
    return;
  }
  p.putBytes("fc", &f, sizeof(WeeklyForecast));
  p.end();
}

void LOAD_FORECAST()
{
  Preferences p;
  if (!p.begin("weather", true))
  {
    return;
  }
  if (p.getBytesLength("fc") == sizeof(WeeklyForecast))
  {
    WeeklyForecast f;
    p.getBytes("fc", &f, sizeof(WeeklyForecast));
    if (f.count <= 8)
    {
      WEEKLY_FORECAST = f;
    }
  }
  p.end();
}

void LOAD_WIFI_CREDENTIALS()
{
  Preferences p;
  WIFI_SSID = "";
  WIFI_PASS = "";
  if (p.begin("wifi", true))
  {
    WIFI_SSID = p.getString("ssid", "");
    WIFI_PASS = p.getString("pass", "");
    p.end();
  }
}

void SAVE_WIFI_CREDENTIALS()
{
  Preferences p;
  if (p.begin("wifi", false))
  {
    p.putString("ssid", WIFI_SSID);
    p.putString("pass", WIFI_PASS);
    p.end();
  }
}

bool VALID_TANK_LEVELS(float high, float low)
{
  return isfinite(high) && isfinite(low) && high >= SENSOR_MIN_CM && low <= SENSOR_MAX_CM && (low - high) >= TANK_MIN_SPAN_CM;
}

void SAVE_CONFIG()
{
  Preferences p;
  if (!p.begin("config", false))
  {
    return;
  }
  p.putString("key", OWM_API_KEY);
  p.putFloat("lat", LOCATION_LAT);
  p.putFloat("lon", LOCATION_LON);
  p.putString("tz", TIMEZONE);
  p.putFloat("tank_hi", TANK_HIGH_CM);
  p.putFloat("tank_lo", TANK_LOW_CM);
  p.putBool("beeper", BEEPER_ENABLED);
  p.end();
}

void LOAD_CONFIG()   // keeps the compiled-in defaults for anything that was never saved
{
  Preferences p;
  if (!p.begin("config", true))
  {
    return;
  }
  OWM_API_KEY = p.getString("key", OWM_API_KEY);
  LOCATION_LAT = p.getFloat("lat", LOCATION_LAT);
  LOCATION_LON = p.getFloat("lon", LOCATION_LON);
  TIMEZONE = p.getString("tz", TIMEZONE);
  float hi = p.getFloat("tank_hi", TANK_HIGH_CM);
  float lo = p.getFloat("tank_lo", TANK_LOW_CM);
  if (VALID_TANK_LEVELS(hi, lo))   // a damaged value falls back to the defaults
  {
    TANK_HIGH_CM = hi;
    TANK_LOW_CM = lo;
  }
  BEEPER_ENABLED = p.getBool("beeper", true);   // on unless the user switched it off
  p.end();
}

void CLEAR_WIFI_CREDENTIALS()
{
  Preferences p;
  if (p.begin("wifi", false))
  {
    p.clear();
    p.end();
  }
  WIFI_SSID = "";
  WIFI_PASS = "";
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Weather snapshot, reliability and condition evaluation

bool FIND_TODAY(const WeeklyForecast& f, time_t now, DayForecast& out)
{
  int32_t key = DATE_KEY(now);
  for (int i = 0; i < f.count && i < 8; i++)
  {
    if (f.days[i].dateKey == key)
    {
      out = f.days[i];
      return true;
    }
  }
  return false;
}

WeatherSnapshot SNAPSHOT_WEATHER()
{
  WeatherSnapshot w = {};
  {
    DataLock lock;
    w.cur = CURRENT_WEATHER;
    w.fc = WEEKLY_FORECAST;
  }

  if (timeIsValid())
  {
    time_t now = time(nullptr);
    w.curOk = w.cur.valid && AGE_OK(w.cur.fetchedAt, now, CURRENT_MAX_AGE_S);
    if (w.fc.count > 0 && AGE_OK(w.fc.fetchedAt, now, FORECAST_MAX_AGE_S))
    {
      w.fcOk = FIND_TODAY(w.fc, now, w.today);
    }
  }
  return w;
}

Reliability GET_RELIABILITY(const WeatherSnapshot& w)
{
  Reliability r;
  r.sensors = BME_OK && BME_VALID;
  r.current = w.curOk;
  r.forecast = w.fcOk;
  return r;
}

bool IS_WEATHER_PARAM(uint8_t p)
{
  return p >= P_W_TEMP;
}

// Returns false if the value is not available (weather stale/missing, BME failure)
bool GET_PARAM(uint8_t p, const WeatherSnapshot& w, float& v)
{
  switch (p)
  {
    case P_SOIL1:
    case P_SOIL2:
    case P_SOIL3:
    case P_SOIL4: v = SOIL_PERCENT[p]; return true;
    case P_TEMP: v = SENSOR_TEMP; return BME_OK && BME_VALID;
    case P_AIR_HUM: v = SENSOR_AIR_HUMIDITY; return BME_OK && BME_VALID;
    case P_PRESSURE: v = SENSOR_AIR_PRESSURE; return BME_OK && BME_VALID;
    case P_W_TEMP: v = w.cur.temp; return w.curOk;
    case P_W_HUM: v = w.cur.humidity; return w.curOk;
    case P_W_PRESSURE: v = w.cur.pressure; return w.curOk;
    case P_W_WIND: v = w.cur.wind; return w.curOk;
    case P_W_CLOUDS: v = w.cur.clouds; return w.curOk;
    case P_W_RAIN1H: v = w.cur.rain1h; return w.curOk;
    case P_F_POP: v = w.today.pop; return w.fcOk;
    case P_F_RAIN: v = w.today.rain; return w.fcOk;
    case P_F_TMAX: v = w.today.tMax; return w.fcOk;
    default: return false;
  }
}

// Rules:
//  - reliable flag = 1 and any data unreliable --> skip
//  - reliable flag = 0 --> unavailable WEATHER parameters count as satisfied (sensors decide alone)
//  - unavailable SENSOR parameter (BME280 failure) --> condition fails (fail safe)
EvalResult EVALUATE_EVENT(const IrrigationEvent& e, const WeatherSnapshot& w, const Reliability& r, uint8_t& failParam)
{
  if (e.requireReliable && !(r.sensors && r.current && r.forecast))
  {
    return EV_SKIP_UNRELIABLE;
  }

  for (uint8_t p = 0; p < P_COUNT; p++)
  {
    if (isnan(e.cond[p].min))
    {
      continue;   // ignored parameter, always true
    }

    float v;
    bool available = GET_PARAM(p, w, v);
    if (!available)
    {
      if (IS_WEATHER_PARAM(p))
      {
        continue;
      }
      failParam = p;
      return EV_SKIP_CONDITION;
    }

    if (v < e.cond[p].min || v > e.cond[p].max)
    {
      failParam = p;
      return EV_SKIP_CONDITION;
    }
  }
  return EV_START;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper functions

void PRINT_DATA()   // Print ALL internal fetched data
{
  WeatherSnapshot w = SNAPSHOT_WEATHER();
  Reliability r = GET_RELIABILITY(w);
  time_t now = timeIsValid() ? time(nullptr) : 0;

  Serial.println(F("============================================================"));
  Serial.printf("Time: %s (source: %s, last NTP: %s)\n", FORMAT_TIME(now).c_str(), TIME_SOURCE_NAME(), FORMAT_TIME(lastNtpSync).c_str());

  const char* ns = netState == NET_ONLINE ? "online" : (netState == NET_PORTAL ? "portal (AP open)" : "offline");
  Serial.printf("WiFi: %s%s%s\n", ns, wifiUp ? ", SSID " : "", wifiUp ? WIFI_SSID.c_str() : "");

  Serial.println(F("--- Sensors"));
  for (int i = 0; i < 4; i++)
  {
    Serial.printf("Soil %d: raw %d, %.1f %%\n", i + 1, SOIL_HUMIDITY[i], SOIL_PERCENT[i]);
  }
  if (BME_OK && BME_VALID)
  {
    Serial.printf("BME280: %.2f C, %.1f %% RH, %.1f hPa\n", SENSOR_TEMP, SENSOR_AIR_HUMIDITY, SENSOR_AIR_PRESSURE);
  }
  else
  {
    Serial.println(F("BME280: not available"));
  }
  if (WATER_VALID)
  {
    Serial.printf("Water level: %.1f cm, %.0f %%, low=%d\n", WATER_DISTANCE_CM, WATER_PERCENT, WATER_LOW_ALERT ? 1 : 0);
  }
  else
  {
    Serial.println(F("Water level: not available"));
  }

  Serial.println(F("--- Current weather"));
  if (w.cur.valid)
  {
    Serial.printf("Fetched: %s (%s)\n", FORMAT_TIME(w.cur.fetchedAt).c_str(), w.curOk ? "fresh" : "STALE");
    Serial.printf("Temp %.1f C, hum %.0f %%, pressure %.0f hPa, wind %.1f m/s, clouds %.0f %%, rain(1h) %.2f mm\n",
                  w.cur.temp, w.cur.humidity, w.cur.pressure, w.cur.wind, w.cur.clouds, w.cur.rain1h);
  }
  else
  {
    Serial.println(F("No data"));
  }

  Serial.println(F("--- Forecast"));
  if (w.fc.count > 0)
  {
    Serial.printf("Fetched: %s (%s for today)\n", FORMAT_TIME(w.fc.fetchedAt).c_str(), w.fcOk ? "usable" : "NOT usable");
    for (int i = 0; i < w.fc.count; i++)
    {
      const DayForecast& d = w.fc.days[i];
      Serial.printf("%d: min %.1f C, max %.1f C, pop %.0f %%, rain %.2f mm\n", d.dateKey, d.tMin, d.tMax, d.pop, d.rain);
    }
  }
  else
  {
    Serial.println(F("No data"));
  }

  Serial.printf("--- Reliability: sensors=%d current weather=%d forecast=%d\n", r.sensors, r.current, r.forecast);

  Serial.print(F("--- Zones:"));
  for (int z = 0; z < 4; z++)
  {
    Serial.printf(" %d=%s", z + 1, zoneOn[z] ? "ON" : "off");
  }
  Serial.printf("\nTimetable events: %d\n", TIMETABLE_COUNT);
  Serial.println(F("============================================================"));
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Fetch functions

int ANALOG_AVG(uint8_t pin)
{
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++)
  {
    sum += analogRead(pin);
  }
  return (int)(sum / 8);
}

// One HC-SR04+ measurement in cm, or -1 when no usable echo came back. speed = speed of sound in m/s.
float MEASURE_DISTANCE_ONCE(float speed)
{
  digitalWrite(ULTRASONIC_TRIG_PIN, LOW);
  delayMicroseconds(4);
  digitalWrite(ULTRASONIC_TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(ULTRASONIC_TRIG_PIN, LOW);

  unsigned long us = pulseIn(ULTRASONIC_ECHO_PIN, HIGH, ECHO_TIMEOUT_US);
  if (us == 0)
  {
    return -1.0f;
  }
  float cm = (float)us * speed / 20000.0f;   // the sound travels there and back: us * 1e-6 * speed / 2 * 100
  if (cm < SENSOR_MIN_CM || cm > SENSOR_MAX_CM)
  {
    return -1.0f;
  }
  return cm;
}

// Median of up to three measurements, so a single bad echo (ripples, a droplet on the sensor) does not matter
bool MEASURE_WATER_DISTANCE(float& cmOut)
{
  float speed = (BME_OK && BME_VALID) ? 331.3f + 0.606f * SENSOR_TEMP : 343.0f;   // corrected with the air temperature
  float samples[3];
  int n = 0;
  int fails = 0;
  for (int i = 0; i < 3; i++)
  {
    float cm = MEASURE_DISTANCE_ONCE(speed);
    if (cm < 0)
    {
      if (++fails > 1)
      {
        return false;
      }
    }
    else
    {
      samples[n++] = cm;
    }
    delay(50);   // let the echoes of the previous burst die out before the next one
  }

  if (n == 2)
  {
    cmOut = (samples[0] + samples[1]) / 2.0f;
    return true;
  }
  float a = samples[0];
  float b = samples[1];
  float c = samples[2];
  cmOut = max(min(a, b), min(max(a, b), c));   // the middle one of three
  return true;
}

void FETCH_WATER_LEVEL()
{
  float high;
  float low;
  {
    DataLock lock;
    high = TANK_HIGH_CM;
    low = TANK_LOW_CM;
  }

  float cm;
  if (MEASURE_WATER_DISTANCE(cm))
  {
    WATER_DISTANCE_CM = cm;
    WATER_FAILS = 0;
    WATER_VALID = true;
  }
  else if (WATER_FAILS < 255)
  {
    WATER_FAILS++;
  }

  if (WATER_FAILS >= WATER_MAX_FAILS)
  {
    WATER_VALID = false;   // a short dropout keeps the last value, a lost sensor does not
  }

  if (!WATER_VALID || !VALID_TANK_LEVELS(high, low))
  {
    WATER_PERCENT = 0;
    WATER_LOW_ALERT = false;
    return;
  }

  WATER_PERCENT = constrain(100.0f * (low - WATER_DISTANCE_CM) / (low - high), 0.0f, 100.0f);
  if (WATER_LOW_ALERT)
  {
    if (WATER_PERCENT >= WATER_LOW_OFF_PERCENT)
    {
      WATER_LOW_ALERT = false;
    }
  }
  else if (WATER_PERCENT < WATER_LOW_ON_PERCENT)
  {
    WATER_LOW_ALERT = true;
  }
}

void FETCH_RAW_SENSOR_VALUES()
{
  SOIL_HUMIDITY[0] = ANALOG_AVG(HUMIDITY_1_PIN);
  SOIL_HUMIDITY[1] = ANALOG_AVG(HUMIDITY_2_PIN);
  SOIL_HUMIDITY[2] = ANALOG_AVG(HUMIDITY_3_PIN);
  SOIL_HUMIDITY[3] = ANALOG_AVG(HUMIDITY_4_PIN);

  for (int i = 0; i < 4; i++)
  {
    int dry = SOIL_RAW_DRY[i];
    int wet = SOIL_RAW_WET[i];
    float pct = (dry == wet) ? 0.0f : 100.0f * (float)(dry - SOIL_HUMIDITY[i]) / (float)(dry - wet);
    SOIL_PERCENT[i] = constrain(pct, 0.0f, 100.0f);
  }

  if (!BME_OK)
  {
    BME_OK = bme.begin(0x76) || bme.begin(0x77);
  }
  if (BME_OK)
  {
    float t = bme.readTemperature();
    float h = bme.readHumidity();
    float p = bme.readPressure() / 100.0f;
    if (isnan(t) || isnan(h) || isnan(p))
    {
      BME_VALID = false;
    }
    else
    {
      SENSOR_TEMP = t;
      SENSOR_AIR_HUMIDITY = h;
      SENSOR_AIR_PRESSURE = p;
      BME_VALID = true;
    }
  }
  else
  {
    BME_VALID = false;
  }

  FETCH_WATER_LEVEL();
}

// HTTPS GET + filtered JSON parse. Returns the HTTP code, or -1/-2 on connect/parse failure.
int HTTP_GET_JSON(const String& url, JsonDocument& doc, const JsonDocument& filter)
{
  WiFiClientSecure client;
  client.setInsecure();   // no certificate pinning, acceptable for public weather data
  HTTPClient http;
  http.useHTTP10(true);   // avoids chunked encoding so the stream can be parsed directly
  http.setConnectTimeout(10000);
  http.setTimeout(10000);
  if (!http.begin(client, url))
  {
    return -1;
  }

  int code = http.GET();
  if (code == HTTP_CODE_OK)
  {
    DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    if (err)
    {
      code = -2;
    }
  }
  http.end();
  return code;
}

String OWM_COORDS()
{
  DataLock lock;   // CFG SET may change these from the serial task (never call this while holding the lock)
  return "lat=" + String(LOCATION_LAT, 4) + "&lon=" + String(LOCATION_LON, 4) + "&units=metric&appid=" + OWM_API_KEY;
}

// Current weather, called at every irrigation event start
bool FETCH_WEATHER_DATA()
{
  if (!wifiUp || !timeIsValid())
  {
    return false;
  }

  JsonDocument filter;
  filter["main"]["temp"] = true;
  filter["main"]["humidity"] = true;
  filter["main"]["pressure"] = true;
  filter["wind"]["speed"] = true;
  filter["clouds"]["all"] = true;
  filter["rain"]["1h"] = true;

  JsonDocument doc;
  int code = HTTP_GET_JSON("https://api.openweathermap.org/data/2.5/weather?" + OWM_COORDS(), doc, filter);
  if (code != HTTP_CODE_OK || !doc["main"]["temp"].is<float>())
  {
    Serial.printf("WARN current weather fetch failed (code %d)\n", code);
    return false;
  }

  CurrentWeather cw = {};
  cw.valid = true;
  cw.fetchedAt = time(nullptr);
  cw.temp = doc["main"]["temp"] | 0.0f;
  cw.humidity = doc["main"]["humidity"] | 0.0f;
  cw.pressure = doc["main"]["pressure"] | 0.0f;
  cw.wind = doc["wind"]["speed"] | 0.0f;
  cw.clouds = doc["clouds"]["all"] | 0.0f;
  cw.rain1h = doc["rain"]["1h"] | 0.0f;

  {
    DataLock lock;
    CURRENT_WEATHER = cw;
  }
  return true;
}

// One Call 3.0: today + 7 days. Needs the separate "One Call by Call" subscription on the key.
bool FETCH_FORECAST_ONECALL(WeeklyForecast& out)
{
  JsonDocument filter;
  filter["daily"][0]["dt"] = true;
  filter["daily"][0]["temp"]["min"] = true;
  filter["daily"][0]["temp"]["max"] = true;
  filter["daily"][0]["pop"] = true;
  filter["daily"][0]["rain"] = true;

  JsonDocument doc;
  String url = "https://api.openweathermap.org/data/3.0/onecall?exclude=current,minutely,hourly,alerts&" + OWM_COORDS();
  int code = HTTP_GET_JSON(url, doc, filter);
  if (code != HTTP_CODE_OK)
  {
    Serial.printf("WARN One Call forecast failed (code %d)\n", code);
    return false;
  }

  out.count = 0;
  for (JsonObject d : doc["daily"].as<JsonArray>())
  {
    if (out.count >= 8)
    {
      break;
    }
    DayForecast& f = out.days[out.count];
    f.dateKey = DATE_KEY((time_t)d["dt"].as<uint32_t>());
    f.tMin = d["temp"]["min"] | 0.0f;
    f.tMax = d["temp"]["max"] | 0.0f;
    f.pop = (d["pop"] | 0.0f) * 100.0f;
    f.rain = d["rain"] | 0.0f;
    out.count++;
  }
  return out.count > 0;
}

// Fallback for keys without One Call access: free 5 day / 3 hour forecast aggregated per day
bool FETCH_FORECAST_5DAY(WeeklyForecast& out)
{
  JsonDocument filter;
  filter["list"][0]["dt"] = true;
  filter["list"][0]["main"]["temp"] = true;
  filter["list"][0]["pop"] = true;
  filter["list"][0]["rain"]["3h"] = true;

  JsonDocument doc;
  int code = HTTP_GET_JSON("https://api.openweathermap.org/data/2.5/forecast?" + OWM_COORDS(), doc, filter);
  if (code != HTTP_CODE_OK)
  {
    Serial.printf("WARN 5 day forecast failed (code %d)\n", code);
    return false;
  }

  out.count = 0;
  int idx = -1;
  for (JsonObject item : doc["list"].as<JsonArray>())
  {
    int32_t key = DATE_KEY((time_t)item["dt"].as<uint32_t>());
    if (idx < 0 || out.days[idx].dateKey != key)
    {
      if (idx + 1 >= 8)
      {
        break;
      }
      idx++;
      out.days[idx].dateKey = key;
      out.days[idx].tMin = 1000.0f;
      out.days[idx].tMax = -1000.0f;
      out.days[idx].pop = 0;
      out.days[idx].rain = 0;
    }
    DayForecast& f = out.days[idx];
    float temp = item["main"]["temp"] | 0.0f;
    f.tMin = min(f.tMin, temp);
    f.tMax = max(f.tMax, temp);
    f.pop = max(f.pop, (item["pop"] | 0.0f) * 100.0f);
    f.rain += item["rain"]["3h"] | 0.0f;
  }
  out.count = (uint8_t)(idx + 1);
  return out.count > 0;
}

// Weekly forecast, called once per day. Saved to NVS on success.
bool FETCH_FORECAST_DATA()
{
  if (!wifiUp || !timeIsValid())
  {
    return false;
  }

  WeeklyForecast f = {};
  bool ok = FETCH_FORECAST_ONECALL(f);
  if (!ok)
  {
    f = {};
    ok = FETCH_FORECAST_5DAY(f);
  }
  if (!ok)
  {
    return false;
  }

  f.fetchedAt = time(nullptr);
  {
    DataLock lock;
    WEEKLY_FORECAST = f;
  }
  SAVE_FORECAST(f);
  return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Timetable serial format

bool NEXT_TOKEN(const String& s, int& pos, char sep, String& tok)
{
  if (pos > (int)s.length())
  {
    return false;
  }
  int idx = s.indexOf(sep, pos);
  if (idx < 0)
  {
    idx = s.length();
  }
  tok = s.substring(pos, idx);
  tok.trim();
  pos = idx + 1;
  return true;
}

bool PARSE_FLOAT(const String& s, float& v)
{
  if (s.length() == 0)
  {
    return false;
  }
  char* end;
  v = strtof(s.c_str(), &end);
  return *end == '\0' && isfinite(v);
}

bool PARSE_INT(const String& s, int& v)
{
  if (s.length() == 0)
  {
    return false;
  }
  char* end;
  long l = strtol(s.c_str(), &end, 10);
  v = (int)l;
  return *end == '\0';
}

bool PARSE_HHMM(const String& s, uint16_t& minutes)
{
  if (s.length() != 4)
  {
    return false;
  }
  for (int i = 0; i < 4; i++)
  {
    if (!isDigit(s[i]))
    {
      return false;
    }
  }
  int hh = (s[0] - '0') * 10 + (s[1] - '0');
  int mm = (s[2] - '0') * 10 + (s[3] - '0');
  if (hh > 23 || mm > 59)
  {
    return false;
  }
  minutes = hh * 60 + mm;
  return true;
}

bool PARSE_EVENT(const String& line, IrrigationEvent& e, String& err)
{
  int pos = 0;
  String tok;
  memset(&e, 0, sizeof(e));

  // days
  if (!NEXT_TOKEN(line, pos, ',', tok) || tok.length() != 7)
  {
    err = "days must be 7 characters";
    return false;
  }
  for (int i = 0; i < 7; i++)
  {
    if (tok[i] != '0' && tok[i] != '1')
    {
      err = "days must contain only 0/1";
      return false;
    }
    if (tok[i] == '1')
    {
      e.daysMask |= (1 << i);
    }
  }
  if (e.daysMask == 0)
  {
    err = "no day selected";
    return false;
  }

  // start, end
  if (!NEXT_TOKEN(line, pos, ',', tok) || !PARSE_HHMM(tok, e.startMin))
  {
    err = "bad start time";
    return false;
  }
  if (!NEXT_TOKEN(line, pos, ',', tok) || !PARSE_HHMM(tok, e.endMin))
  {
    err = "bad end time";
    return false;
  }
  if (e.startMin == e.endMin)
  {
    err = "start equals end";
    return false;
  }

  // zones
  if (!NEXT_TOKEN(line, pos, ',', tok) || tok.length() != 4)
  {
    err = "zones must be 4 characters";
    return false;
  }
  for (int i = 0; i < 4; i++)
  {
    if (tok[i] != '0' && tok[i] != '1')
    {
      err = "zones must contain only 0/1";
      return false;
    }
    if (tok[i] == '1')
    {
      e.zoneMask |= (1 << i);
    }
  }
  if (e.zoneMask == 0)
  {
    err = "no zone selected";
    return false;
  }

  // reliable flag
  if (!NEXT_TOKEN(line, pos, ',', tok) || (tok != "0" && tok != "1"))
  {
    err = "reliable flag must be 0 or 1";
    return false;
  }
  e.requireReliable = (tok == "1") ? 1 : 0;

  // conditions
  for (int p = 0; p < P_COUNT; p++)
  {
    if (!NEXT_TOKEN(line, pos, ',', tok))
    {
      err = "too few condition fields";
      return false;
    }
    if (tok == "*")
    {
      e.cond[p].min = NAN;
      e.cond[p].max = NAN;
      continue;
    }
    int colon = tok.indexOf(':');
    float lo, hi;
    if (colon <= 0 || !PARSE_FLOAT(tok.substring(0, colon), lo) || !PARSE_FLOAT(tok.substring(colon + 1), hi) || lo > hi)
    {
      err = String("bad condition for ") + PARAM_INFO[p].name;
      return false;
    }
    e.cond[p].min = lo;
    e.cond[p].max = hi;
  }

  if (pos <= (int)line.length())
  {
    err = "too many fields";
    return false;
  }
  return true;
}

String EVENT_TO_STRING(const IrrigationEvent& e)
{
  String s;
  s.reserve(160);
  for (int i = 0; i < 7; i++)
  {
    s += ((e.daysMask >> i) & 1) ? '1' : '0';
  }
  s += ',';
  s += FMT_HHMM(e.startMin);
  s += ',';
  s += FMT_HHMM(e.endMin);
  s += ',';
  for (int i = 0; i < 4; i++)
  {
    s += ((e.zoneMask >> i) & 1) ? '1' : '0';
  }
  s += ',';
  s += e.requireReliable ? '1' : '0';
  for (int p = 0; p < P_COUNT; p++)
  {
    s += ',';
    if (isnan(e.cond[p].min))
    {
      s += '*';
    }
    else
    {
      s += FMT_FLOAT(e.cond[p].min);
      s += ':';
      s += FMT_FLOAT(e.cond[p].max);
    }
  }
  return s;
}

void TT_GET()
{
  String out;
  out.reserve(64 + TIMETABLE_COUNT * 160);
  {
    DataLock lock;
    out = "TT SET " + String(TIMETABLE_COUNT);
    for (int i = 0; i < TIMETABLE_COUNT; i++)
    {
      out += ';';
      out += EVENT_TO_STRING(TIMETABLE[i]);
    }
  }
  Serial.println(out);
}

void TT_SET(String payload)
{
  static IrrigationEvent tmp[MAX_EVENTS];
  payload.trim();

  int pos = 0;
  String tok;
  int n;
  if (!NEXT_TOKEN(payload, pos, ';', tok) || !PARSE_INT(tok, n) || n < 0 || n > MAX_EVENTS)
  {
    Serial.println(F("ERR TT SET: first field must be the event count (0-24)"));
    return;
  }

  int count = 0;
  String err;
  while (NEXT_TOKEN(payload, pos, ';', tok))
  {
    if (tok.length() == 0)
    {
      continue;
    }
    if (count >= MAX_EVENTS)
    {
      Serial.println(F("ERR TT SET: too many events"));
      return;
    }
    if (!PARSE_EVENT(tok, tmp[count], err))
    {
      Serial.printf("ERR TT SET: event %d: %s\n", count + 1, err.c_str());
      return;
    }
    count++;
  }

  if (count != n)
  {
    Serial.printf("ERR TT SET: count mismatch (header says %d, found %d)\n", n, count);
    return;
  }

  {
    DataLock lock;
    memcpy(TIMETABLE, tmp, sizeof(IrrigationEvent) * count);
    TIMETABLE_COUNT = count;
    SAVE_TIMETABLE();
  }
  Serial.printf("OK TT SET %d\n", count);
}

String FMT_COORD(float v)
{
  String s = String(v, 6);
  if (s.indexOf('.') >= 0)
  {
    while (s.endsWith("0"))
    {
      s.remove(s.length() - 1);
    }
    if (s.endsWith("."))
    {
      s.remove(s.length() - 1);
    }
  }
  return s;
}

bool VALID_API_KEY(const String& k)
{
  if (k.length() < 1 || k.length() > 64)
  {
    return false;
  }
  for (size_t i = 0; i < k.length(); i++)
  {
    char c = k[i];
    if (!isAlphaNumeric(c) && c != '-' && c != '_')
    {
      return false;
    }
  }
  return true;
}

bool VALID_TIMEZONE(const String& z)
{
  if (z.length() < 3 || z.length() > 63)
  {
    return false;
  }
  if (!isAlpha(z[0]) && z[0] != '<')
  {
    return false;
  }
  for (size_t i = 0; i < z.length(); i++)
  {
    char c = z[i];
    if (c < 0x21 || c > 0x7E || c == ';')
    {
      return false;
    }
  }
  return true;
}

void CFG_GET()
{
  String key;
  String tz;
  float lat;
  float lon;
  float high;
  float low;
  bool beeper;
  {
    DataLock lock;
    key = OWM_API_KEY;
    tz = TIMEZONE;
    lat = LOCATION_LAT;
    lon = LOCATION_LON;
    high = TANK_HIGH_CM;
    low = TANK_LOW_CM;
    beeper = BEEPER_ENABLED;
  }
  Serial.println("CFG SET " + key + ";" + FMT_COORD(lat) + ";" + FMT_COORD(lon) + ";" + tz + ";" + FMT_FLOAT(high) + ";" + FMT_FLOAT(low) + ";" + (beeper ? "1" : "0"));
}

void CFG_SET(String payload)
{
  payload.trim();

  int pos = 0;
  String keyTok;
  String latTok;
  String lonTok;
  String tzTok;
  String highTok;
  String lowTok;
  String beeperTok;
  bool haveTank = false;
  bool haveBeeper = false;
  if (!NEXT_TOKEN(payload, pos, ';', keyTok) || !NEXT_TOKEN(payload, pos, ';', latTok) ||
      !NEXT_TOKEN(payload, pos, ';', lonTok) || !NEXT_TOKEN(payload, pos, ';', tzTok))
  {
    Serial.println(F("ERR CFG SET: expected <apikey>;<lat>;<lon>;<timezone>;<high_cm>;<low_cm>;<beeper>"));
    return;
  }
  if (pos <= (int)payload.length())   // the two water tank fields and the beeper field are optional
  {
    if (!NEXT_TOKEN(payload, pos, ';', highTok) || !NEXT_TOKEN(payload, pos, ';', lowTok))
    {
      Serial.println(F("ERR CFG SET: expected <apikey>;<lat>;<lon>;<timezone>;<high_cm>;<low_cm>;<beeper>"));
      return;
    }
    haveTank = true;
    if (pos <= (int)payload.length())
    {
      if (!NEXT_TOKEN(payload, pos, ';', beeperTok) || pos <= (int)payload.length())
      {
        Serial.println(F("ERR CFG SET: expected <apikey>;<lat>;<lon>;<timezone>;<high_cm>;<low_cm>;<beeper>"));
        return;
      }
      haveBeeper = true;
    }
  }

  float lat;
  float lon;
  if (!VALID_API_KEY(keyTok))
  {
    Serial.println(F("ERR CFG SET: api key must be 1-64 characters (letters, digits, - or _)"));
    return;
  }
  if (!PARSE_FLOAT(latTok, lat) || lat < -90.0f || lat > 90.0f)
  {
    Serial.println(F("ERR CFG SET: latitude must be a number from -90 to 90"));
    return;
  }
  if (!PARSE_FLOAT(lonTok, lon) || lon < -180.0f || lon > 180.0f)
  {
    Serial.println(F("ERR CFG SET: longitude must be a number from -180 to 180"));
    return;
  }
  if (!VALID_TIMEZONE(tzTok))
  {
    Serial.println(F("ERR CFG SET: bad timezone (POSIX TZ string, 3-63 characters, no spaces)"));
    return;
  }

  float high = TANK_HIGH_CM;
  float low = TANK_LOW_CM;
  if (haveTank && (!PARSE_FLOAT(highTok, high) || !PARSE_FLOAT(lowTok, low) || !VALID_TANK_LEVELS(high, low)))
  {
    Serial.println(F("ERR CFG SET: water levels must be 2-400 cm, the high level at least 5 cm smaller than the low level"));
    return;
  }

  bool beeper = BEEPER_ENABLED;
  if (haveBeeper)
  {
    if (beeperTok != "0" && beeperTok != "1")
    {
      Serial.println(F("ERR CFG SET: beeper must be 0 or 1"));
      return;
    }
    beeper = (beeperTok == "1");
  }

  bool placeChanged = fabsf(lat - LOCATION_LAT) > 0.000001f || fabsf(lon - LOCATION_LON) > 0.000001f;
  bool tzChanged = (tzTok != TIMEZONE);

  {
    DataLock lock;
    OWM_API_KEY = keyTok;
    LOCATION_LAT = lat;
    LOCATION_LON = lon;
    TIMEZONE = tzTok;
    TANK_HIGH_CM = high;
    TANK_LOW_CM = low;
    BEEPER_ENABLED = beeper;
    if (placeChanged || tzChanged)
    {
      // Cached weather belongs to the old place / local dates, do not use it any more
      CURRENT_WEATHER = {};
      WEEKLY_FORECAST = {};
    }
  }
  SAVE_CONFIG();
  if (placeChanged || tzChanged)
  {
    WeeklyForecast empty = {};
    SAVE_FORECAST(empty);
  }

  if (tzChanged)
  {
    setenv("TZ", tzTok.c_str(), 1);
    tzset();
    // Keep the clock consistent: NTP time is an absolute epoch (write it to the RTC as the new local time),
    // time that came from the RTC is a wall clock (read it again under the new zone).
    if (timeSource == TS_NTP)
    {
      WRITE_TIME_TO_RTC();
    }
    else if (timeSource == TS_RTC)
    {
      LOAD_TIME_FROM_RTC();
    }
  }

  // Fetch current weather and forecast with the new settings as soon as possible (if WiFi is up)
  weatherRefetch = true;

  Serial.println(F("OK CFG SET"));
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serial communications functions

void PROCESS_COMMAND(const String& line)
{
  String up = line;
  up.toUpperCase();

  if (linkActive)
  {
    linkLastRx = millis();   // any traffic from the PC keeps the link alive
  }

  if (up == "ID")
  {
    Serial.printf("ID %s %s\n", DEVICE_ID, FW_VERSION);
  }
  else if (up == "PING")
  {
    Serial.printf("PONG %d\n", linkActive ? 1 : 0);
  }
  else if (up == "HELLO")
  {
    LINK_UP();
    Serial.println(F("OK HELLO"));
  }
  else if (up == "BYE")
  {
    LINK_DOWN();
    Serial.println(F("OK BYE"));
  }
  else if (up == "HELP")
  {
    Serial.println(F("Commands: HELP, ID, PING, HELLO, BYE, PRINT, AUTOPRINT 0|1, TT PARAMS, TT GET, TT SET <n>;<event>;..., TT CLEAR, CFG GET, CFG SET <apikey>;<lat>;<lon>;<timezone>;<high_cm>;<low_cm>;<beeper>"));
  }
  else if (up == "PRINT")
  {
    PRINT_DATA();
  }
  else if (up.startsWith("AUTOPRINT"))
  {
    AUTO_PRINT = up.endsWith("1");
    Serial.printf("OK AUTOPRINT %d\n", AUTO_PRINT);
  }
  else if (up == "TT PARAMS")
  {
    for (int p = 0; p < P_COUNT; p++)
    {
      Serial.printf("%d %s [%s]\n", p, PARAM_INFO[p].name, PARAM_INFO[p].unit);
    }
  }
  else if (up == "TT GET")
  {
    TT_GET();
  }
  else if (up == "TT CLEAR")
  {
    {
      DataLock lock;
      TIMETABLE_COUNT = 0;
      SAVE_TIMETABLE();
    }
    Serial.println(F("OK TT CLEAR"));
  }
  else if (up.startsWith("TT SET"))
  {
    TT_SET(line.substring(6));
  }
  else if (up == "CFG GET")
  {
    CFG_GET();
  }
  else if (up.startsWith("CFG SET"))
  {
    CFG_SET(line.substring(7));
  }
  else
  {
    Serial.println(F("ERR unknown command, type HELP"));
  }
}

void HANDLE_SERIAL()
{
  static String line;
  static bool overflow = false;
  const size_t MAX_LINE = 8192;

  while (Serial.available())
  {
    char c = (char)Serial.read();
    if (c == '\n')
    {
      line.trim();
      if (overflow)
      {
        Serial.println(F("ERR line too long"));
      }
      else if (line.length() > 0)
      {
        PROCESS_COMMAND(line);
      }
      line = "";
      overflow = false;
    }
    else if (c != '\r')
    {
      if (line.length() < MAX_LINE)
      {
        line += c;
      }
      else
      {
        overflow = true;
      }
    }
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Outputs and indicators

void APPLY_OUTPUTS()
{
  bool any = false;
  for (int z = 0; z < 4; z++)
  {
    digitalWrite(SOLENOID_PINS[z], zoneOn[z] ? SOLENOID_ON_LEVEL : !SOLENOID_ON_LEVEL);
    any = any || zoneOn[z];
  }
#ifdef SOLENOID_PUMP_PIN
  digitalWrite(SOLENOID_PUMP_PIN, any ? SOLENOID_ON_LEVEL : !SOLENOID_ON_LEVEL);
#endif
}

void UPDATE_INDICATORS()
{
  // Error LED: steady on while the water level is close to the minimum (below 10 %, overrides everything else),
  // otherwise flashing whenever WiFi is unavailable (offline mode, portal open, connection lost)
  if (WATER_LOW_ALERT)
  {
    digitalWrite(ERROR_LED_PIN, HIGH);
  }
  else if (!wifiUp)
  {
    digitalWrite(ERROR_LED_PIN, ((millis() / 500) % 2) ? HIGH : LOW);
  }
  else
  {
    digitalWrite(ERROR_LED_PIN, LOW);
  }

  // Buzzer: play queued beeps one after another with a short gap
  static bool playing = false;
  static uint32_t playUntil = 0;
  static uint32_t gapUntil = 0;
  static uint16_t currentGap = 0;
  if (playing)
  {
    if (DUE(playUntil) || !BEEPER_ENABLED)   // switching the beeper off cuts the current note short
    {
      BUZZER_TONE(0);
      playing = false;
      gapUntil = millis() + currentGap;
    }
  }
  else if (!BEEPER_ENABLED)
  {
    BeepItem dropped;   // throw away whatever was queued before the beeper was switched off
    while (BEEP_POP(dropped))
    {
    }
  }
  else if (DUE(gapUntil))
  {
    BeepItem item;
    if (BEEP_POP(item))
    {
      BUZZER_TONE(item.hz);
      playing = true;
      playUntil = millis() + item.ms;
      currentGap = item.gap;
    }
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scheduler (core 1, called from loop)
//
// Once per second the whole timetable is evaluated against the current time AND the current data:
//   a zone is ON  <=>  at least one event covers the current time and that event's conditions are met right now.
// Conditions are therefore checked continuously: a zone can be switched off (and on again) several times
// during one event window. To protect the valves from flapping, a zone that has just switched is not switched
// again by a CONDITION change for ZONE_MIN_DWELL_MS. The end of an event window always switches off immediately.

// True if nowMin on weekday dow lies inside the event window (days mask refers to the START day)
bool EVENT_ACTIVE_NOW(const IrrigationEvent& e, int dow, int nowMin)
{
  int prevDow = (dow + 6) % 7;
  if (e.startMin < e.endMin)
  {
    return ((e.daysMask >> dow) & 1) && nowMin >= e.startMin && nowMin < e.endMin;
  }
  if (((e.daysMask >> dow) & 1) && nowMin >= e.startMin)
  {
    return true;   // first part, before midnight
  }
  return ((e.daysMask >> prevDow) & 1) && nowMin < e.endMin;   // tail after midnight
}

void RUN_SCHEDULER()
{
  static uint32_t lastEval = 0;
  static bool prevInWindow[4] = {false, false, false, false};
  static uint32_t changeAllowedAt[4] = {0, 0, 0, 0};

  if (!timeIsValid() || !netSettled)
  {
    return;   // no clock, or the first weather fetch is still pending
  }
  uint32_t ms = millis();
  if (ms - lastEval < SCHED_INTERVAL_MS)
  {
    return;
  }
  lastEval = ms;

  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  int nowMin = t.tm_hour * 60 + t.tm_min;
  int dow = (t.tm_wday + 6) % 7;   // Monday = 0

  // Snapshot first: it takes the data lock itself
  WeatherSnapshot w = SNAPSHOT_WEATHER();
  Reliability r = GET_RELIABILITY(w);

  bool inWindow[4] = {false, false, false, false};   // some event covers the current time
  bool wanted[4] = {false, false, false, false};     // ... and its conditions are met
  EvalResult blockRes[4] = {EV_SKIP_CONDITION, EV_SKIP_CONDITION, EV_SKIP_CONDITION, EV_SKIP_CONDITION};
  uint8_t blockParam[4] = {0, 0, 0, 0};

  {
    DataLock lock;
    for (int i = 0; i < TIMETABLE_COUNT; i++)
    {
      const IrrigationEvent& e = TIMETABLE[i];
      if (!EVENT_ACTIVE_NOW(e, dow, nowMin))
      {
        continue;
      }
      uint8_t failParam = 0;
      EvalResult res = EVALUATE_EVENT(e, w, r, failParam);
      for (int z = 0; z < 4; z++)
      {
        if (!((e.zoneMask >> z) & 1))
        {
          continue;
        }
        inWindow[z] = true;
        if (res == EV_START)
        {
          wanted[z] = true;
        }
        else
        {
          blockRes[z] = res;
          blockParam[z] = failParam;
        }
      }
    }
  }

  // A window just opened: ask the network task for fresh current weather (the zones are evaluated every second,
  // so they follow as soon as the new data arrives)
  bool windowOpened = false;
  for (int z = 0; z < 4; z++)
  {
    if (inWindow[z] && !prevInWindow[z])
    {
      windowOpened = true;
    }
  }
  if (windowOpened)
  {
    weatherDone = false;
    weatherRequested = true;
  }

  bool turnedOn = false;
  bool turnedOff = false;
  for (int z = 0; z < 4; z++)
  {
    if (wanted[z] != zoneOn[z])
    {
      // Immediate: window ended, window just opened. Otherwise (condition change inside a window) respect the dwell time.
      bool apply = !inWindow[z] || !prevInWindow[z] || DUE(changeAllowedAt[z]);
      if (apply)
      {
        zoneOn[z] = wanted[z];
        changeAllowedAt[z] = ms + ZONE_MIN_DWELL_MS;
        if (wanted[z])
        {
          turnedOn = true;
          Serial.printf("[IRRIGATION] zone %d ON\n", z + 1);
        }
        else
        {
          turnedOff = true;
          if (!inWindow[z])
          {
            Serial.printf("[IRRIGATION] zone %d OFF (scheduled time ended)\n", z + 1);
          }
          else if (blockRes[z] == EV_SKIP_UNRELIABLE)
          {
            Serial.printf("[IRRIGATION] zone %d OFF (data not reliable: sensors=%d current=%d forecast=%d)\n", z + 1, r.sensors, r.current, r.forecast);
          }
          else
          {
            Serial.printf("[IRRIGATION] zone %d OFF (condition %s out of range)\n", z + 1, PARAM_INFO[blockParam[z]].name);
          }
        }
      }
    }
    prevInWindow[z] = inWindow[z];
  }

  if (turnedOn || turnedOff)
  {
    APPLY_OUTPUTS();
  }
  if (turnedOn)
  {
    BEEP_START();
  }
  if (turnedOff)
  {
    BEEP_END();
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// WiFi portal

String HTML_ESCAPE(const String& s)
{
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++)
  {
    char c = s[i];
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

const char PORTAL_TOP[] = R"HTML(<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Irrigation WiFi setup</title><style>body{font-family:sans-serif;max-width:420px;margin:2em auto;padding:0 1em}select,input,button{width:100%;padding:.6em;margin:.4em 0;font-size:1em;box-sizing:border-box}</style></head><body><h2>Irrigation system WiFi</h2><form method="POST" action="/save"><label>Network</label><select name="ssid">)HTML";

const char PORTAL_BOTTOM[] = R"HTML(</select><label>Or type a network name (hidden networks)</label><input name="ssid_manual"><label>Password</label><input type="password" name="pass"><button type="submit">Connect</button></form><p><a href="/scan">Rescan networks</a></p></body></html>)HTML";

const char PORTAL_WAIT[] = R"HTML(<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Connecting</title></head><body style="font-family:sans-serif;max-width:420px;margin:2em auto;padding:0 1em"><h2>Irrigation system WiFi</h2><p id="s">Connecting...</p><script>function p(){fetch('/status').then(function(r){return r.text()}).then(function(t){if(t=='ok'){document.getElementById('s').textContent='Connected. The setup network will close shortly.';}else if(t=='fail'){document.getElementById('s').innerHTML='Could not connect. <a href="/">Try again</a>';}else{setTimeout(p,1000);}}).catch(function(){setTimeout(p,1000);});}p();</script></body></html>)HTML";

void SCAN_NETWORKS()
{
  int n = WiFi.scanNetworks();
  scanOptions = "";
  for (int i = 0; i < n; i++)
  {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0)
    {
      continue;
    }
    String esc = HTML_ESCAPE(ssid);
    if (scanOptions.indexOf("value=\"" + esc + "\"") >= 0)
    {
      continue;
    }
    scanOptions += "<option value=\"" + esc + "\">" + esc + " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
  }
  if (scanOptions.length() == 0)
  {
    scanOptions = "<option value=\"\">No networks found</option>";
  }
  WiFi.scanDelete();
}

void HANDLE_PORTAL_ROOT()
{
  String page = PORTAL_TOP;
  page += scanOptions;
  page += PORTAL_BOTTOM;
  server.send(200, "text/html", page);
}

void HANDLE_PORTAL_SCAN()
{
  SCAN_NETWORKS();
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

void HANDLE_PORTAL_SAVE()
{
  String ssid = server.arg("ssid_manual");
  ssid.trim();
  if (ssid.length() == 0)
  {
    ssid = server.arg("ssid");
  }
  if (ssid.length() == 0)
  {
    server.send(400, "text/plain", "No network selected");
    return;
  }

  portalSsid = ssid;
  portalPass = server.arg("pass");
  portalConnecting = true;
  portalResult = 0;
  portalConnectStart = millis();
  WiFi.begin(portalSsid.c_str(), portalPass.c_str());

  server.send(200, "text/html", PORTAL_WAIT);
}

void HANDLE_PORTAL_STATUS()
{
  const char* s = portalConnecting ? "connecting" : (portalResult == 1 ? "ok" : (portalResult == 2 ? "fail" : "idle"));
  server.send(200, "text/plain", s);
}

void HANDLE_PORTAL_NOT_FOUND()
{
  // Captive portal: send every unknown URL to the setup page
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

void SETUP_PORTAL_ROUTES()
{
  server.on("/", HTTP_GET, HANDLE_PORTAL_ROOT);
  server.on("/scan", HTTP_GET, HANDLE_PORTAL_SCAN);
  server.on("/save", HTTP_POST, HANDLE_PORTAL_SAVE);
  server.on("/status", HTTP_GET, HANDLE_PORTAL_STATUS);
  server.onNotFound(HANDLE_PORTAL_NOT_FOUND);
}

void START_PORTAL()
{
  portalRequested = false;
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASSWORD.length() >= 8 ? AP_PASSWORD.c_str() : nullptr);
  vTaskDelay(200 / portTICK_PERIOD_MS);

  dnsServer.start(53, "*", WiFi.softAPIP());
  SCAN_NETWORKS();
  server.begin();

  portalConnecting = false;
  portalResult = 0;
  // With stored credentials the portal gives up after 2 minutes and reconnects to the stored network.
  // Without credentials there is nothing to go back to, so the portal stays open.
  portalTimed = WIFI_SSID.length() > 0;
  portalDeadline = millis() + PORTAL_TIMEOUT_MS;
  netState = NET_PORTAL;
  wifiUp = false;
  Serial.println(F("[WIFI] setup portal open"));
}

void STOP_PORTAL()
{
  server.stop();
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  Serial.println(F("[WIFI] setup portal closed"));
}

void SERVICE_PORTAL()
{
  dnsServer.processNextRequest();
  server.handleClient();

  if (portalRequested)
  {
    portalRequested = false;
    if (portalTimed)
    {
      portalDeadline = millis() + PORTAL_TIMEOUT_MS;
    }
  }

  if (portalConnecting)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      WIFI_SSID = portalSsid;
      WIFI_PASS = portalPass;
      SAVE_WIFI_CREDENTIALS();
      portalConnecting = false;
      portalResult = 1;
      portalFinishAt = millis() + 5000;   // leave the AP up briefly so the phone can read the result
      Serial.println(F("[WIFI] new network saved"));
    }
    else if (millis() - portalConnectStart >= WIFI_CONNECT_TIMEOUT_MS)
    {
      WiFi.disconnect(false, false);
      portalConnecting = false;
      portalResult = 2;
    }
  }
  else if (portalResult == 1 && DUE(portalFinishAt))
  {
    STOP_PORTAL();
    netState = NET_ONLINE;
  }
  else if (portalTimed && DUE(portalDeadline))
  {
    // Nobody chose a new network: back to the stored one
    STOP_PORTAL();
    WiFi.mode(WIFI_STA);
    nextWifiTry = millis();
    netState = NET_OFFLINE;
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// WiFi station connection

void POLL_BUTTON()
{
  bool pressed = digitalRead(BUTTON_PIN) == BUTTON_ACTIVE_LEVEL;
  if (pressed && !buttonWasPressed && (millis() - buttonLastEdge) > 50)
  {
    portalRequested = true;
  }
  if (pressed != buttonWasPressed)
  {
    buttonLastEdge = millis();
  }
  buttonWasPressed = pressed;
}

// Try the stored network for up to 30 seconds. Gives up early if the button asks for the portal.
bool WIFI_CONNECT_STORED()
{
  if (WIFI_SSID.length() == 0)
  {
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID.c_str(), WIFI_PASS.c_str());

  uint32_t start = millis();
  while (millis() - start < WIFI_CONNECT_TIMEOUT_MS)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.printf("[WIFI] connected to %s\n", WIFI_SSID.c_str());
      return true;
    }
    POLL_BUTTON();
    if (portalRequested)
    {
      return false;
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }

  Serial.println(F("[WIFI] network not found, offline mode"));
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  return false;
}

void TRY_CONNECT()
{
  if (WIFI_CONNECT_STORED())
  {
    netState = NET_ONLINE;
  }
  else
  {
    netState = NET_OFFLINE;
    nextWifiTry = millis() + WIFI_RETRY_INTERVAL_MS;
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Network jobs (core 0)

void JOB_NTP()
{
  if (FETCH_TIME_DATE())
  {
    nextNtpAt = millis() + NTP_INTERVAL_MS;
    Serial.printf("[TIME] NTP sync ok: %s\n", FORMAT_TIME(time(nullptr)).c_str());
  }
  else
  {
    nextNtpAt = millis() + NTP_RETRY_MS;   // retry every hour
    if (!timeIsValid())
    {
      LOAD_TIME_FROM_RTC();
    }
    Serial.println(F("[TIME] NTP failed, retry in 1 hour"));
  }
}

bool BACKOFF_BLOCKED(Backoff& b)
{
  if (!b.active)
  {
    return false;
  }
  if (DUE(b.until))
  {
    b.active = false;
    return false;
  }
  return true;
}

void BACKOFF_START(Backoff& b)
{
  b.until = millis() + FETCH_BACKOFF_MS;
  b.active = true;
}

// Pause between two attempts, in small steps so the button still works.
// False = stop trying (WiFi went away, or the button asked for the setup portal).
bool WAIT_BETWEEN_ATTEMPTS()
{
  uint32_t start = millis();
  while (millis() - start < FETCH_ATTEMPT_GAP_MS)
  {
    POLL_BUTTON();
    if (portalRequested || WiFi.status() != WL_CONNECTED)
    {
      return false;
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
  return true;
}

// Fetches the forecast (forecast = true) or the current weather, up to FETCH_ATTEMPTS times in a row.
// Without WiFi (or a clock) nothing is tried at all.
// `attempted` (optional) is set after the first attempt, whatever its result.
FetchResult FETCH_WITH_RETRIES(bool forecast, volatile bool* attempted)
{
  const char* what = forecast ? "forecast" : "current weather";
  for (uint8_t attempt = 1; attempt <= FETCH_ATTEMPTS; attempt++)
  {
    if (!wifiUp || WiFi.status() != WL_CONNECTED || !timeIsValid())
    {
      return FETCH_SKIPPED;
    }

    bool ok = forecast ? FETCH_FORECAST_DATA() : FETCH_WEATHER_DATA();
    if (attempted != nullptr)
    {
      *attempted = true;
    }
    if (ok)
    {
      return FETCH_OK;
    }

    Serial.printf("[WEATHER] %s fetch failed (attempt %d of %d)\n", what, attempt, FETCH_ATTEMPTS);
    if (attempt < FETCH_ATTEMPTS && !WAIT_BETWEEN_ATTEMPTS())
    {
      return FETCH_SKIPPED;
    }
  }
  return FETCH_FAILED;
}

FetchResult JOB_FORECAST()
{
  FetchResult r = FETCH_WITH_RETRIES(true, nullptr);
  if (r == FETCH_OK)
  {
    Serial.println(F("[WEATHER] weekly forecast updated"));
  }
  else if (r == FETCH_FAILED)
  {
    BACKOFF_START(forecastBackoff);
    Serial.println(F("[WEATHER] forecast: all attempts failed, next try in 30 minutes"));
    BEEP_FAIL();
  }
  return r;
}

FetchResult JOB_CURRENT_WEATHER(const char* reason)
{
  FetchResult r = FETCH_WITH_RETRIES(false, &firstWeatherDone);
  if (r == FETCH_OK)
  {
    Serial.printf("[WEATHER] current weather updated (%s)\n", reason);
  }
  else if (r == FETCH_FAILED)
  {
    BACKOFF_START(currentBackoff);
    Serial.println(F("[WEATHER] current weather: all attempts failed, next try in 30 minutes"));
    BEEP_FAIL();
  }
  return r;
}

// Decides once per second whether the current weather or the forecast has to be fetched. Only while WiFi is up.
//   - unreliable data (missing or stale, exactly what the timetable's "reliable" flag looks at) is fetched at once
//   - reliable data is refreshed on a schedule (current weather every 15 minutes, forecast every 24 hours)
//   - after FETCH_ATTEMPTS failed tries in a row the data rests for 30 minutes (backoff), then the tries start again
void SERVICE_WEATHER_FETCHES()
{
  static uint32_t lastCheck = 0;
  uint32_t ms = millis();
  if (ms - lastCheck < 1000)
  {
    return;
  }
  lastCheck = ms;

  if (!wifiUp || !timeIsValid())
  {
    return;   // no WiFi: do not even try
  }

  bool force = weatherRefetch;   // new settings: do not wait for anything
  if (force)
  {
    weatherRefetch = false;
    currentBackoff.active = false;
    forecastBackoff.active = false;
  }

  WeatherSnapshot w = SNAPSHOT_WEATHER();
  time_t now = time(nullptr);

  bool currentDue = force || !w.curOk || (now - w.cur.fetchedAt) >= CURRENT_REFRESH_S;
  bool forecastDue = force || !w.fcOk || w.fc.fetchedAt > now || (now - w.fc.fetchedAt) >= FORECAST_INTERVAL_S;

  if (currentDue && !BACKOFF_BLOCKED(currentBackoff))
  {
    JOB_CURRENT_WEATHER("scheduled");
  }
  if (forecastDue && !BACKOFF_BLOCKED(forecastBackoff))
  {
    JOB_FORECAST();
  }
}

// Scheduler asked for fresh current weather (an event window opened)
void SERVICE_WEATHER_REQUEST()
{
  if (!weatherRequested)
  {
    return;
  }
  weatherRequested = false;

  bool ok = false;
  if (wifiUp && timeIsValid() && !BACKOFF_BLOCKED(currentBackoff))   // no WiFi, or resting after failed tries: skip
  {
    ok = (JOB_CURRENT_WEATHER("event window opened") == FETCH_OK);
  }
  weatherOk = ok;
  weatherDone = true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Core 0 functions

void CORE_0_TASK(void* parameter)   // serial commands
{
  while (true)
  {
    HANDLE_SERIAL();
    vTaskDelay(1);
  }
}

void INDICATOR_TASK(void* parameter)   // error LED and buzzer, independent of loop()
{
  while (true)
  {
    UPDATE_INDICATORS();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void NETWORK_TASK(void* parameter)   // WiFi, portal, NTP, OpenWeatherMap
{
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  SETUP_PORTAL_ROUTES();

  LOAD_WIFI_CREDENTIALS();
  if (startupButton && BUTTON_AT_BOOT_CLEARS_CREDENTIALS)
  {
    CLEAR_WIFI_CREDENTIALS();
  }

  if (startupButton || WIFI_SSID.length() == 0)
  {
    START_PORTAL();
  }
  else
  {
    TRY_CONNECT();
  }

  bool prevUp = false;

  while (true)
  {
    // Refresh first: the fetch functions check wifiUp, and netState may just have changed to NET_ONLINE
    bool up = (netState == NET_ONLINE) && (WiFi.status() == WL_CONNECTED);
    if (up && !prevUp)
    {
      CHIME_WIFI();
    }
    prevUp = up;
    wifiUp = up;

    POLL_BUTTON();
    SERVICE_WEATHER_REQUEST();

    switch (netState)
    {
      case NET_ONLINE:
        if (portalRequested)
        {
          START_PORTAL();
          break;
        }
        if (WiFi.status() != WL_CONNECTED)
        {
          netState = NET_OFFLINE;
          nextWifiTry = millis();
          break;
        }
        if (DUE(nextNtpAt))
        {
          JOB_NTP();
        }
        SERVICE_WEATHER_FETCHES();
        break;

      case NET_OFFLINE:
        if (portalRequested)
        {
          START_PORTAL();
          break;
        }
        if (WIFI_SSID.length() > 0 && DUE(nextWifiTry))
        {
          TRY_CONNECT();
        }
        break;

      case NET_PORTAL:
        SERVICE_PORTAL();
        break;
    }

    wifiUp = (netState == NET_ONLINE) && (WiFi.status() == WL_CONNECTED);

    // The scheduler may start once the network had its first chance (connect attempt + first weather fetch)
    if (!netSettled && (netState != NET_ONLINE || firstWeatherDone))
    {
      netSettled = true;
    }
    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Main functions

void setup()
{
  Serial.setRxBufferSize(8192);   // room for a whole timetable line
  Serial.begin(921600);

  dataMutex = xSemaphoreCreateMutex();
  LOAD_CONFIG();   // saved API key / location / time zone override the defaults
  setenv("TZ", TIMEZONE.c_str(), 1);
  tzset();

  pinMode(HUMIDITY_1_PIN, INPUT);
  pinMode(HUMIDITY_2_PIN, INPUT);
  pinMode(HUMIDITY_3_PIN, INPUT);
  pinMode(HUMIDITY_4_PIN, INPUT);

  pinMode(ULTRASONIC_TRIG_PIN, OUTPUT);
  digitalWrite(ULTRASONIC_TRIG_PIN, LOW);
  pinMode(ULTRASONIC_ECHO_PIN, INPUT);

  pinMode(BUTTON_PIN, BUTTON_PIN_MODE);

  pinMode(SOLENOID_1_PIN, OUTPUT);
  pinMode(SOLENOID_2_PIN, OUTPUT);
  pinMode(SOLENOID_3_PIN, OUTPUT);
  pinMode(SOLENOID_4_PIN, OUTPUT);

  APPLY_OUTPUTS();   // everything off

  pinMode(ERROR_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  BUZZER_INIT();

  // Power-up self test: LED flash and boot chime. If you do not see/hear them, the problem is wiring or the buzzer type
  digitalWrite(ERROR_LED_PIN, HIGH);
  delay(200);
  digitalWrite(ERROR_LED_PIN, LOW);
  CHIME_BOOT();   // queued, played by the indicator task

  // Button held during power-up --> open the setup portal
  startupButton = (digitalRead(BUTTON_PIN) == BUTTON_ACTIVE_LEVEL);
  buttonWasPressed = startupButton;

  // I2C is configured by Wire.begin, the pins must not be set as plain outputs
  Wire.begin(SDA_PIN, SCL_PIN);
  BME_OK = bme.begin(0x76) || bme.begin(0x77);

  // DS1302: use it right away so the schedule works even before (or without) WiFi
  Rtc.Begin();
  if (Rtc.GetIsWriteProtected())
  {
    Rtc.SetIsWriteProtected(false);
  }
  if (!Rtc.GetIsRunning())
  {
    Rtc.SetIsRunning(true);
  }
  if (!LOAD_TIME_FROM_RTC())
  {
    Serial.println(F("[TIME] RTC does not hold a valid time, waiting for NTP"));
  }

  LOAD_TIMETABLE();
  LOAD_FORECAST();
  FETCH_RAW_SENSOR_VALUES();

  // Core 0 initialization
  xTaskCreatePinnedToCore(
    CORE_0_TASK,            // Function to run on the core
    "CORE_0_TASK",          // Name of the task
    12288,                  // Stack size (bytes)  //Have to be careful with this otherwise segfault
    NULL,                   // Task parameter
    1,                      // Task priority
    NULL,                   // Task handle
    0                       // Core to run the task on (Core 0)
  );

  xTaskCreatePinnedToCore(INDICATOR_TASK, "INDICATOR_TASK", 3072, NULL, 2, NULL, 1);

  xTaskCreatePinnedToCore(
    NETWORK_TASK,
    "NETWORK_TASK",
    20480,                  // TLS handshakes need a big stack
    NULL,
    1,
    NULL,
    0
  );
}

void loop()
{
  static uint32_t lastSensor = 0;
  static uint32_t lastPrint = 0;
  uint32_t ms = millis();

  if (ms - lastSensor >= SENSOR_INTERVAL_MS)
  {
    lastSensor = ms;
    FETCH_RAW_SENSOR_VALUES();
  }

  RUN_SCHEDULER();
  SERVICE_LINK();

  if (AUTO_PRINT && ms - lastPrint >= PRINT_INTERVAL_MS)
  {
    lastPrint = ms;
    PRINT_DATA();
  }

  delay(20);
}

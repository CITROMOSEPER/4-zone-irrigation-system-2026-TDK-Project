// Globals behind the stub headers
#include <Arduino.h>
#include <stdarg.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <Wire.h>
#include <ESPmDNS.h>
#include <Adafruit_BME280.h>
#include <RtcDS1302.h>

namespace host {
uint32_t ms = 1000;
int pinLevel[40], analogValue[40];
bool pinOutput[40];
unsigned long pulseUs = 1750;   // about 30 cm
bool bmeOk = true;
float bmeT = 15, bmeH = 60, bmeP = 1013;
uint32_t rtcBase = 0, rtcBaseMs = 0;
bool rtcValid = false;
wl_status_t wifiStatus = WL_CONNECTED;
int wifiRssi = -60;
std::map<std::string, std::vector<uint8_t>>& nvs() { static std::map<std::string, std::vector<uint8_t>> m; return m; }
std::map<std::string, std::string>& files() { static std::map<std::string, std::string> m; return m; }
}  // namespace host

HardwareSerial Serial;
EspClass ESP;
WiFiClass WiFi;
TwoWire Wire;
MDNSResponder MDNS;
LittleFSFS LittleFS;

size_t Print::printf(const char* fmt, ...) {
  char buf[512];
  va_list a;
  va_start(a, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, a);
  va_end(a);
  return print(buf), (size_t)n;
}

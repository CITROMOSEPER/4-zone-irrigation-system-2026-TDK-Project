// Globals behind the stub headers
#include <Arduino.h>
#include <stdarg.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_system.h>
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
uint32_t rtcGlitch = 0;
uint8_t rtcGlitchReads = 0;
bool rtcValid = false, rtcBroken = false;   // broken: writes do not take
uint32_t sysBase = 0, sysBaseMs = 0;      // the ESP32 system clock (SNTP); 0 = not set
wl_status_t wifiStatus = WL_CONNECTED;
int wifiRssi = -60;
esp_reset_reason_t resetReason = ESP_RST_POWERON;
std::vector<ScanNet> scanNets;
IPAddress wifiIp(192, 168, 1, 50);
uint8_t apClients = 0, apChannel = 0;
wifi_mode_t wifiMode = WIFI_STA;
std::string beginSsid, beginPass;
int beginChannel = 0, beginCount = 0;
std::function<void(arduino_event_id_t, arduino_event_info_t)> wifiEvent;
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

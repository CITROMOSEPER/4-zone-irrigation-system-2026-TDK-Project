#pragma once
#include <Arduino.h>
#include <functional>
#include <string>
#include <vector>
typedef enum { WIFI_OFF, WIFI_STA, WIFI_AP, WIFI_AP_STA } wifi_mode_t;
typedef enum { WL_IDLE_STATUS = 0, WL_CONNECTED = 3, WL_DISCONNECTED = 6 } wl_status_t;
typedef enum { WIFI_AUTH_OPEN = 0, WIFI_AUTH_WPA2_PSK = 3 } wifi_auth_mode_t;
typedef int arduino_event_id_t;
#define ARDUINO_EVENT_WIFI_STA_DISCONNECTED 5
#define WIFI_SCAN_RUNNING (-1)
struct arduino_event_info_t { struct { uint8_t reason; } wifi_sta_disconnected; };
namespace host {
extern wl_status_t wifiStatus;
extern int wifiRssi;
// The setup-portal tests drive these
struct ScanNet { std::string ssid; int rssi; uint8_t ch; bool open; };
extern std::vector<ScanNet> scanNets;
extern IPAddress wifiIp;
extern uint8_t apClients, apChannel;
extern wifi_mode_t wifiMode;
extern std::string beginSsid, beginPass;
extern int beginChannel, beginCount;
extern std::function<void(arduino_event_id_t, arduino_event_info_t)> wifiEvent;
inline void wifiDisconnect(uint8_t reason) {      // the SDK reports a failed attempt
  wifiStatus = WL_DISCONNECTED;
  arduino_event_info_t i; i.wifi_sta_disconnected.reason = reason;
  if (wifiEvent) wifiEvent(ARDUINO_EVENT_WIFI_STA_DISCONNECTED, i);
}
}
class WiFiClass {
public:
  void persistent(bool) {}
  void onEvent(void (*f)(arduino_event_id_t, arduino_event_info_t), arduino_event_id_t) { host::wifiEvent = f; }
  bool mode(wifi_mode_t m) { host::wifiMode = m; return true; }
  wifi_mode_t getMode() { return host::wifiMode; }
  void setHostname(const char*) {}
  void setSleep(bool) {}
  void setAutoReconnect(bool) {}
  void begin(const char* s, const char* p, int32_t ch = 0, const uint8_t* = nullptr, bool = true) {
    host::beginSsid = s ? s : ""; host::beginPass = p ? p : ""; host::beginChannel = ch; host::beginCount++;
  }
  void disconnect(bool, bool) {}
  wl_status_t status() { return host::wifiStatus; }
  String SSID() { return String("TestNet"); }
  String SSID(int i) { return String(host::scanNets[i].ssid.c_str()); }
  int RSSI() { return host::wifiRssi; }
  int RSSI(int i) { return host::scanNets[i].rssi; }
  int32_t channel(int i) { return host::scanNets[i].ch; }
  wifi_auth_mode_t encryptionType(int i) { return host::scanNets[i].open ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK; }
  IPAddress localIP() { return host::wifiIp; }
  IPAddress softAPIP() { return IPAddress(10, 73, 0, 1); }
  void scanDelete() {}
  int16_t scanNetworks(bool, bool) { return -1; }
  int16_t scanComplete() { return (int16_t)host::scanNets.size(); }
  void macAddress(uint8_t* m) { memset(m, 0xAB, 6); }
  bool softAP(const char*, const char*, int ch, int, int) { host::apChannel = (uint8_t)ch; return true; }
  bool softAPConfig(IPAddress, IPAddress, IPAddress) { return true; }
  uint8_t softAPgetStationNum() { return host::apClients; }
};
extern WiFiClass WiFi;

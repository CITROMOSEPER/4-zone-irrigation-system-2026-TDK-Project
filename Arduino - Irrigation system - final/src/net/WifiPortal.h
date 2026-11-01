#pragma once
// Wi-Fi with a setup portal.
//   saved credentials                          -> join that network, serve the controller UI
//   none, no connection in 30 s, button held at boot,
//   or "Change WiFi network" in the UI         -> setup AP "Irrigation-Setup-XXXX" + captive portal
// The hostname (mDNS name.local) is the controller name from Settings.
// The pages are gzipped HTML in flash (include/AppPage.h, include/PortalPage.h, built by tools/build_pages.py).
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <functional>

class WifiPortal {
public:
  enum class Mode : uint8_t { OFF, CONNECTING, STA, PRESCAN, AP };
  using HealthFn = String (*)();

  WifiPortal() : _server(80) {}

  void setHealth(HealthFn f) { _health = f; }
  void setRoutes(std::function<void(WebServer&)> f) { _routes = f; }   // the API, before begin()
  void onConnect(std::function<void()> f) { _onConnect = f; }

  void begin(bool forceAp, const char* hostname);
  void update();                                     // every loop pass, never blocks for long

  Mode mode() const { return _mode; }
  bool apMode() const { return _mode == Mode::AP || _mode == Mode::PRESCAN; }
  bool connected() const { return _mode == Mode::STA && WiFi.status() == WL_CONNECTED; }

  void setHostname(const char* name);                // restarts mDNS; DHCP sees it at the next reconnect
  void resetToSetup();                               // reboot into the setup AP (UI: change network)

private:
  struct Net { String ssid; int8_t rssi; bool open; };

  void loadCreds();
  bool saveCreds(const String& ssid, const String& pass);
  void startSta();
  void onConnected();
  void trackConnection();
  void startMdns();
  void startAp(bool forced);
  void openAp();
  void apRetry();
  void stopRetry();
  void startScan();
  void resetRadio();
  void pollScan();
  void collectNetworks(int n);
  void addNet(const String& name, int8_t rssi, bool open);
  void ensureServer();
  void registerRoutes();
  void notFound();
  void redirectHome();
  void sendPage(const uint8_t* gz, size_t len, const char* cache);
  void sendJson(const String& j);
  void sendScan();
  void handleSave();
  void restart(const char* why);

  HealthFn _health = nullptr;
  std::function<void(WebServer&)> _routes;
  std::function<void()> _onConnect;
  WebServer _server;
  DNSServer _dns;
  Mode _mode = Mode::OFF;
  char _host[25] = "irrigation";

  String _ssid, _pass;
  static constexpr uint8_t MAX_NETS = 24;
  Net _nets[MAX_NETS];
  uint8_t _netCount = 0;

  uint32_t _tMode = 0, _tRetry = 0, _tTry = 0, _restartAt = 0;
  uint8_t _scanRetries = 0;
  bool _serverOn = false, _mdnsOn = false, _wasUp = false, _scanning = false;
  bool _apForced = false, _retrying = false, _connectFailed = false, _restartPending = false;
};

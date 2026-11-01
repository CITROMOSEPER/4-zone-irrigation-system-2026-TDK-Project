// Plain-text status page at /health: works in any browser (the UI's old-browser fallback links here)
// and in setup AP mode. Also printed on the serial port with 'p'.
#include "net/Health.h"
#include <WiFi.h>
#include "System.h"
#include "util/TimeUtil.h"

String healthText() {
  String s;
  s.reserve(1400);
  const uint32_t now = sys.clock.unixNow();
  char lt[20];
  TimeUtil::formatLocal(now, lt);

  appendf(s, "%s - irrigation controller %s (%s)\n\n", sys.cfg.g.name, FW_VERSION, HW_NAME);
  appendf(s, "uptime: %lu s | free heap: %lu B\n", (unsigned long)(millis() / 1000), (unsigned long)ESP.getFreeHeap());
  if (sys.net.mode() == WifiPortal::Mode::STA) {
    appendf(s, "wifi: %s | %s | %d dBm\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
  } else {
    appendf(s, "wifi: setup AP (not on your network)\n");
  }
  appendf(s, "clock: %s (%s)\n", sys.clock.valid() ? lt : "-", sys.clock.valid() ? "ok" : "NOT SET");
  appendf(s, "env: %.1f C, %.0f %%, %.0f hPa (%s)\n", sys.env.temperatureC(), sys.env.humidity(), sys.env.pressureHpa(),
          sys.env.valid() ? "ok" : "FAULT");
  appendf(s, "tank: %s, %d %% (%.1f cm)\n", sys.tank.low() ? "LOW" : "ok", sys.tank.percent(), sys.tank.distanceCm());
  appendf(s, "rain sensor: %s\n", sys.rain.wet() ? "WET" : "dry");
  appendf(s, "all off: %s | block: %s | global DND until %lu\n", sys.cfg.g.off ? "ON" : "off",
          *Rules::blockName(Rules::globalBlock()) ? Rules::blockName(Rules::globalBlock()) : "none",
          (unsigned long)sys.dnd.until(0));
  appendf(s, "pump: %s | active zone: %d (%lu/%lu s)\n", sys.water.pumpOn() ? "ON" : "off", (int)sys.water.activeZone() + 1,
          (unsigned long)sys.water.runElapsedS(), (unsigned long)sys.water.runTotalS());

  for (uint8_t z = 0; z < ZONES; z++) {
    const ZoneCfg& c = sys.cfg.z[z];
    Rules::ZoneStatus st = Rules::zoneStatus(z);
    float soil;
    bool have = sys.dose.soil(z, soil);
    appendf(s, "zone %u \"%s\": %s%s%s | mode %u | soil raw %d, %s%.0f %%%s | today %lu s | valve %s\n",
            z + 1, c.name, Rules::stName(st.st), *st.why ? " " : "", st.why, c.m, sys.soil[z].raw(),
            c.m == 2 ? "est. " : "", have ? soil : NAN, sys.soil[z].fault() ? " (SENSOR FAULT)" : "",
            (unsigned long)sys.water.secondsToday(z), sys.water.valveOn(z) ? "OPEN" : "closed");
  }
  WxSnapshot w = sys.wx.snapshot();
  appendf(s, "weather: %s, last good fetch %lu, open-loop ET0 yesterday %.1f mm\n", w.ok ? "ok" : "no data",
          (unsigned long)w.t, sys.wb.lastEt0());
  appendf(s, "output faults: %s\n", sys.water.faulted() ? "YES (a hard cap tripped: clear it in the app)" : "none");
  return s;
}

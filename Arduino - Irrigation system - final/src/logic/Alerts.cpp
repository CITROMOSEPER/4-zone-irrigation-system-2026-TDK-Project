#include "logic/Alerts.h"
#include <WiFi.h>
#include "System.h"

void Alerts::check(Flag& f, bool now) {
  if (now != f.raw) { f.raw = now; f.since = millis(); }
  if (f.raw == f.logged || !elapsed(f.since, f.debounceS * 1000UL)) return;
  f.logged = f.raw;
  char code[16];
  // Codes as the UI knows them: TANK_LOW / TANK_OK, ENV / ENV_OK, ...
  if (f.raw) snprintf(code, sizeof(code), "%s", strcmp(f.code, "TANK") ? f.code : "TANK_LOW");
  else snprintf(code, sizeof(code), "%s_OK", f.code);
  sys.log.alert(f.zone, code, f.raw ? f.level : 'I');
}

void Alerts::update() {
  if (!elapsed(_tick, 1000)) return;
  _tick = millis();
  if (!_init) {
    _init = true;
    for (uint8_t z = 0; z < ZONES; z++) _f[5 + z] = Flag{"SOIL", 'W', (uint8_t)(z + 1), 60, false, false, 0};
  }
  check(_f[0], sys.tank.low());
  check(_f[1], !sys.env.valid());
  check(_f[2], sys.water.faulted());
  check(_f[3], !sys.clock.valid());
  check(_f[4], sys.net.connected() && WiFi.RSSI() < WIFI_WEAK_RSSI);
  for (uint8_t z = 0; z < ZONES; z++) check(_f[5 + z], sys.cfg.z[z].en && sys.soil[z].fault());
}

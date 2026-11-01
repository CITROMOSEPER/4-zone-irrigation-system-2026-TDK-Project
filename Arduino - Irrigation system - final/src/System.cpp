#include "System.h"
#include <esp_task_wdt.h>
#include <time.h>
#include "net/Health.h"

System sys;

void System::begin() {
  // 1) Outputs to the safe state before anything else
  water.begin(&tank);

  Serial.begin(115200);
  esp_task_wdt_init(WDT_TIMEOUT_S, true);         // panic and reboot if the loop hangs
  esp_task_wdt_add(NULL);

  // 2) Service mode = button held at boot (setup AP). Wait for the release so it is not a press.
  button.begin(PIN_BUTTON);
  delay(50);
  _service = button.pressed();
  while (button.pressed()) { esp_task_wdt_reset(); delay(10); }

  // 3) Hardware
  TimeUtil::begin();
  clock.begin();
  for (uint8_t z = 0; z < ZONES; z++) soil[z].begin(PIN_SOIL[z]);
  tank.begin(PIN_TANK_TRIG, PIN_TANK_ECHO);
  rain.begin(PIN_RAIN_OUT, PIN_RAIN_VCC);
  env.begin();
  fb.begin(PIN_ERROR_LED, PIN_BUZZER);

  // 4) Storage and logic (the log first: the others may write to it)
  cfg.begin();
  applySettings();
  cal.begin();
  log.begin();
  dnd.begin();
  cost.begin();
  wb.begin();
  sched.begin();
  irr.begin();
  wx.begin();

  // 5) Network
  net.setHealth(healthText);
  net.setRoutes([this](WebServer& s) { api.routes(s); });
  net.onConnect([this]() {
    fb.wifiConnected();
    configTzTime(LOCAL_TZ, "pool.ntp.org", "time.google.com");   // keeps our TZ string
    _sntpPending = true;
    _tSntp = millis();
  });
  net.begin(_service, cfg.g.name);

  Serial.printf("Irrigation controller %s ready%s. Serial: 1-4 water 1 min, s stop, x clear faults, p status\n",
                FW_VERSION, _service ? " (service mode: setup AP)" : "");
}

void System::applySettings() {
  water.setDailyCapS(cfg.g.cap * 60UL);
  tank.setCal(cfg.g.tankE, cfg.g.tankF);
  for (uint8_t z = 0; z < ZONES; z++) soil[z].setCal(cfg.g.cal[z].dry, cfg.g.cal[z].wet);
  fb.setSoundOn(cfg.g.buzz);
}

void System::setAllOff(bool on) {
  if ((cfg.g.off != 0) == on) return;
  cfg.g.off = on ? 1 : 0;
  cfg.saveGlobal();
  if (on) irr.stopAll(StopReason::USER);
  else cost.seasonStart();
}

void System::sensors() {
  tank.update();
  rain.update();
  env.update();
  clock.update();
  if (elapsed(_tSoil, SOIL_PERIOD_MS)) {
    _tSoil = millis();
    for (auto& s : soil) s.update();
  }
}

// SNTP corrects the RTC: soon after connecting, then every few hours
void System::sntp() {
  if (!net.connected()) return;
  if (!elapsed(_tSntp, _sntpPending ? 10000 : SNTP_PERIOD_MS)) return;
  _tSntp = millis();
  if (clock.syncFromSntp()) Serial.println("CLOCK: set from SNTP");
  _sntpPending = time(nullptr) < (time_t)RTC_MIN_VALID_UNIX;
}

void System::loop() {
  esp_task_wdt_reset();

  sensors();
  water.update();                                  // safety layer: every pass
  test.update();

  dnd.update(clock.unixNow());
  sched.update();
  irr.update();
  dose.update();
  cost.update();
  alerts.update();

  buttons.handle(button.update());
  net.update();
  sntp();

  fb.setState(Rules::seriousErrors(), net.apMode(), cfg.g.off);
  fb.update();
}

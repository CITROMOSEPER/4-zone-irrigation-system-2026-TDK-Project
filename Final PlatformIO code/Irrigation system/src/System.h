#pragma once
// Every module of the controller, in one object. Modules reach each other through 'sys'
// instead of passing a dozen pointers around.
//
//   hardware   sensors, clock, button, feedback, WaterController (the only code that switches water)
//   store      Settings and Calendar (NVS)
//   logic      decides when and how much: Scheduler, Irrigation (queue), Dose, Dnd, ...
//   net        WifiPortal and the HTTP API
#include <Arduino.h>
#include "Config.h"
#include "PINS.h"

#include "hw/SoilSensor.h"
#include "hw/TankSensor.h"
#include "hw/RainSensor.h"
#include "hw/Environment.h"
#include "hw/Clock.h"
#include "hw/Button.h"
#include "hw/Feedback.h"
#include "hw/WaterController.h"

#include "store/Settings.h"
#include "store/Calendar.h"

#include "logic/Rules.h"
#include "logic/Dnd.h"
#include "logic/Dose.h"
#include "logic/WaterBalance.h"
#include "logic/Weather.h"
#include "logic/RunLog.h"
#include "logic/Stats.h"
#include "logic/Irrigation.h"
#include "logic/Scheduler.h"
#include "logic/CostMeter.h"
#include "logic/Alerts.h"
#include "logic/TestRunner.h"
#include "logic/ButtonActions.h"

#include "net/WifiPortal.h"
#include "net/Api.h"

#include "util/Util.h"
#include "util/TimeUtil.h"

static_assert(sizeof(PIN_SOIL) == ZONES && sizeof(PIN_VALVE) == ZONES, "PINS.h needs one soil and one valve pin per zone");

struct System {
  // hardware
  SoilSensor soil[ZONES];
  TankSensor tank;
  RainSensor rain;
  Environment env;
  Clock clock;
  Button button;
  Feedback fb;
  WaterController water;
  // storage
  Settings cfg;
  Calendar cal;
  // logic
  Dnd dnd;
  Dose dose;
  WaterBalance wb;
  Weather wx;
  RunLog log;
  Stats stats;
  Irrigation irr;
  Scheduler sched;
  CostMeter cost;
  Alerts alerts;
  TestRunner test;
  ButtonActions buttons;
  // network
  WifiPortal net;
  Api api;

  void begin();
  void loop();

  void applySettings();          // push settings into the hardware classes (calibration, caps, sound)
  // Air temperature for decisions (frost, zone minimum, tank sound speed): the BME280, or while it is
  // faulty the forecast's hourly temperature for now (web = true). NAN when neither is available.
  float airTempC(bool* web = nullptr) const;
  void setAllOff(bool on);       // All off = winter mode; logged once when it changes
  // Settings, calendars, DND and counters back to the defaults, then a restart.
  // Wi-Fi credentials and the log stay. Waits until a running zone has closed and been logged.
  void factoryReset();
  // Restart the controller (Health page): stops watering, waits for the valves to close, logs it.
  void reboot();

private:
  void logStart();               // why the controller (re)started: POWER_ON, BROWNOUT, CRASH
  void sensors();
  void sntp();
  void factoryResetNow();
  bool _factory = false, _reboot = false;
  uint32_t _tSoil = 0, _tSntp = 0, _tAir = 0;
  bool _sntpPending = false;
  bool _service = false;
};

extern System sys;

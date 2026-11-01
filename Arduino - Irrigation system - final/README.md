# Irrigation controller firmware (v1.0.0)

ESP32 firmware for the 4-zone adaptive irrigation controller (TDK 2026). It implements the API the
v2 UI expects (`portal/mock.js`, `claude/API-v2.md`) and the plan in "Irrigation UI v2 → firmware implementation plan".

## Build and flash

1. Put the UI sources in `portal/`: `app-demo.html` (the v2 UI with the offline demo) and `index.html` (setup page).
2. `pio run -t upload`, then `pio device monitor`.

Before every build, `tools/build_pages.py` strips the mock controller and the Demo panel out of
`app-demo.html` into `portal/app.html`, and gzips both pages into `include/AppPage.h` and
`include/PortalPage.h`. If the demo markup changes and the script cannot find it, the build stops
with a message instead of shipping the mock.

Partition table: `huge_app.csv` (3 MB app, about 900 kB LittleFS for the log, no OTA).

## Layout

```
include/   Config.h (ceilings, defaults), PINS.h, generated pages
src/
  main.cpp          setup/loop + serial bench keys (1-4 water 1 min, s stop, x clear faults, p status)
  System.h/.cpp     every module in one object, 'sys'; boot order and the main loop
  util/             Util.h (helpers, UTF-8), TimeUtil (local time, dates), Nvs.h (structs in NVS)
  hw/               one class per device; WaterController is the only code that switches water
  store/            Settings (global + per zone), Calendar (waterings, DND windows)
  logic/            Rules, Dnd, Dose, WaterBalance, Weather, RunLog, Irrigation (queue),
                    Scheduler, CostMeter, Alerts, TestRunner, ButtonActions
  net/              WifiPortal, Api (routes + POST handlers), ApiJson (GET answers), Health
tools/build_pages.py
test/host/          the firmware compiled for a PC against stub hardware, with tests
```

Layers: hardware ← safety (`WaterController`) ← logic ← network. Every path that moves water
(calendar, Water now, button, output test) ends in `WaterController`, which re-checks the tank,
the hard caps, the daily budget and the valve/pump order on every pass.

## Decisions taken in the code (change them if the team decides otherwise)

| Topic | What the firmware does |
|---|---|
| Soil % | One scale: 0 % = the "dry" calibration, 100 % = "wet". Calibrate wet at field capacity (soaked, drained) and dry near wilting, so the % matches the deficit formula's plant-available water. |
| Calendar vs start window | The calendar wins. A blocked entry waits up to the queue timeout (default 120 min), then `SKIP_<reason>`. |
| Rain | Rain sensor wet, or today's forecast ≥ "rain stop from" (default 2 mm) → `SKIP_RAIN`; not for covered zones or closed loop (it measures the soil). |
| Daily limit | Runs are trimmed to what is left of the zone's daily budget; refused only below 1 min. The used seconds survive a reboot. |
| Closed loop | Waters only below `lo`, up to `hi`. Sensor fault, or 3 runs in a row with < 2 points rise (checked 10 min after a run) → Schedule length (15 min if none). |
| Open loop | Daily bucket: −ET₀×Kc, +0.8×rain (not covered), +applied water. Open-Meteo ET₀, Hargreaves from the BME280 when there is no forecast. |
| Queue | One entry per zone, FIFO; zones due in the same minute run in zone order; any stop that is not DONE clears the queue. Dropped scheduled runs are logged. |
| Reboot mid-run | The run is kept in RTC memory; after a reset its row is logged with result `FAULT` and its seconds counted. A power cut loses it. |
| Defaults | Frost 2 °C, zone minimum 4 °C, daily limit 90 min, rain stop 2 mm, flow 2 L/min (measure it), name `irrigation`. |
| Button | As decided 30 Oct: see `src/logic/ButtonActions.h`. |
| Rain sensor | Powered only while sampling (300 ms every 30 s); two agreeing samples change the state. |
| Clock | DS1302 in UTC; SNTP corrects it when Wi-Fi is up (every 6 h); the app's "Set clock" still works offline. |
| Log | `utc,local,zone,mode,soil0,soil1,sec,litres,result` + ALERT and COST rows. A v1 log is moved to `/log_v1.csv` at first boot. `/api/log` sends the newest 48 kB, `/log.csv` the whole file. |
| Skip reasons | `DND ALL_OFF RAIN WET FROST TANK_LOW COLD OFF DAILY_LIMIT FAULT QUEUE_TIMEOUT WINDOW_MISSED USER`. The UI has no word for `WET` yet: add `r_WET` ("soil moist enough" / "elég nedves"). |

Settings and calendars are stored in NVS with a version number. This firmware is version 2, so the first boot drops the v1 settings once.

## Host tests

The firmware compiles for a PC with the stub headers in `test/host/stubs` (no ESP32 needed):

```
git clone --depth 1 -b v7.4.2 https://github.com/bblanchon/ArduinoJson.git /tmp/aj
test/host/build.sh /tmp/aj/src
```

128 checks: time and DST, DND windows across midnight, UTF-8 names, the deficit formula, Water now
(valve before pump, queue, DND and cold answers, frost), the schedule (DND skip, closed-loop dose,
zone order), the button (deficit order, 15 min cap, winter mode on and off), the output test,
the tank-low stop and alerts, the cost rollover at midnight, and the log API.

Not covered on the host: real sensor timing, Wi-Fi, HTTPS to Open-Meteo, flash wear. Test those on the bench.

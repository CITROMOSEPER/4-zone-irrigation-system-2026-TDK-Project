# Irrigation controller firmware (v1.3.2)

ESP32 firmware for the 4-zone adaptive irrigation controller (TDK 2026). It implements the API in `docs/API.md` (API v2.4; what "v2" means is explained there),
which the web UI in `portal/` uses (its offline demo answers the same way). Version history: `CHANGELOG.md`.

## Build and flash

1. Put the UI sources in `portal/`: `app-demo.html` (the v2 UI with the offline demo) and `index.html` (setup page).
2. `pio run -t upload`, then `pio device monitor`.

Before every build, `tools/build_pages.py` strips the mock controller and the Demo panel out of
`app-demo.html` into `portal/app.html`, and gzips both pages into `include/AppPage.h` and
`include/PortalPage.h`. If the demo markup changes and the script cannot find it, the build stops
with a message instead of shipping the mock.

Partition table: `huge_app.csv` (3 MB app, about 900 kB LittleFS for the log and statistics, no OTA).

Updating: from 1.3.x everything stays. Firmware before 1.3 must first be updated to 1.3.0 (its converters were removed in 1.3.2); otherwise the settings start from the defaults.
The old pump flow is copied to every zone with an area that keeps the old mm/h, so **enter each zone's real
area** (Zone → Sprinkler) after the update. The old log is split into month files.

Updating from 1.1: plants stay as they were (established, no stages) and every soil gets a 30 cm depth.
After flashing 1.2:
- **Re-calibrate "wet"** with the sensor in soil that was soaked and then left to drain for a day (field
  capacity), not in a glass of water. 100 % now means field capacity, and the presets' limits assume that.
- **Enter the soil depth** of each zone (Zone → Soil), e.g. 10 cm for the room model or a pot's depth.
- Pick the new presets, or a crop with a planting date (Zone → Plant).

## Fallbacks

- No valid RTC time (dead battery, broken module): the ESP32's own clock, set by SNTP or the app's "Set from this
  device", runs the controller until the next power cut. Health shows a warning.
- Faulty BME280: frost protection, the zone minimum temperatures and the tank's speed-of-sound correction use the
  forecast's hourly temperature for the current hour (if the forecast is under 12 hours old). Without a forecast,
  the temperature rules are skipped as before, and Problems says so.

## Plant age (FAO-56)

A plant with stage lengths and a planting date follows the FAO-56 crop curve: Kc at planting during the
initial stage, a straight line to the mid-season Kc during development, the mid-season Kc, then a line to
the end Kc in the late season. The roots grow from the planting depth to the full depth by the end of
development. The roots used are never deeper than the soil. "Lawn from seed" has only the first two stages
and is an established lawn after them. All stages 0: an established plant (Kc and roots fixed).

Presets fill in values only; editing one makes the zone "Custom". Sources: FAO-56 Tables 11, 12 and 22 (Kc,
stage lengths, allowed depletion p: "water below" = 100 × (1 − p)), FAO-56 Table 19 and the FAO irrigation
manual (soils, infiltration), WUCOLS (landscape plants, estimates), HydroPoint controller defaults (sprinklers).

## Layout

```
include/   Config.h (ceilings, defaults), PINS.h, generated pages
src/
  main.cpp          setup/loop + serial bench keys (1-4 water 1 min, s stop, x clear faults, p status)
  System.h/.cpp     every module in one object, 'sys'; boot order and the main loop
  util/             Util.h (helpers, UTF-8), TimeUtil (local time, dates), Nvs.h (structs in NVS)
  hw/               one class per device; WaterController is the only code that switches water
  store/            Settings (global + per zone), Calendar (waterings, DND windows)
  logic/            Rules, Dnd, Dose, WaterBalance, Weather, RunLog (month files), Stats (charts),
                    Irrigation (queue, cycle and soak), Scheduler, CostMeter, Alerts, TestRunner, ButtonActions
  net/              WifiPortal, Api (routes + POST handlers), ApiJson (GET answers), Health
tools/build_pages.py
test/host/          the firmware compiled for a PC against stub hardware, with tests
test/ui/            browser tests of the web UI
```

Layers: hardware ← safety (`WaterController`) ← logic ← network. Every path that moves water
(calendar, Water now, button, output test) ends in `WaterController`, which re-checks the tank,
the hard caps, the daily budget and the valve/pump order on every pass.

## Decisions taken in the code (change them if the team decides otherwise)

| Topic | What the firmware does |
|---|---|
| Soil % | One scale: 0 % = the "dry" calibration, 100 % = "wet". Calibrate wet at field capacity (soaked, drained) and dry near wilting, so the % matches the deficit formula's plant-available water. |
| Calendar vs start window | The calendar wins. A blocked entry waits up to the queue timeout (default 120 min), then `SKIP_<reason>`. |
| Rain | Rain sensor wet, or the forecast rain over the next 24 h ≥ "rain stop from" (default 2 mm) in **both** ICON and ECMWF → `SKIP_RAIN`; not for covered zones or closed loop (it measures the soil). |
| Forecast | Open-Meteo, ICON + ECMWF in one call (no GFS), yesterday + 7 days; chosen by `docs/weather-api-research.md`. Rain more than 48 h ahead is never used. The zone card shows the next automatic watering's expected length (forecast ET₀ × Kc and rain up to 48 h); the real length is still calculated when it starts. |
| Run length | 2 s minimum, 1 s steps (the scale model applies about 0.4 mm a second). Water now on a running zone changes its length. |
| Sprinkler | Per zone: flow (L/min) and area (m²); the rate (mm/h) follows. Litres and cost use each zone's flow. |
| Cycle and soak | Calendar, deficit and button runs faster than the soil's infiltration are split: bursts end before 2 mm stands on the surface, pauses let it soak in (30 s – 60 min). |
| Daily limit | Runs are trimmed to what is left of the zone's daily budget; refused only below 2 s. The used seconds survive a reboot. |
| Closed loop | Waters only below `lo`, up to `hi`. Sensor fault, or 3 runs in a row with < 2 points rise (checked 10 min after a run) → Schedule length (15 min if none). |
| Open loop | Daily bucket: −ET₀×Kc, +0.8×rain (not covered), +applied water. Open-Meteo ET₀, Hargreaves from the BME280 when there is no forecast. |
| Queue | One entry per zone, FIFO; zones due in the same minute run in zone order; any stop that is not DONE clears the queue. Dropped scheduled runs are logged. |
| Reboot mid-run | The run is kept in RTC memory; after a reset its row is logged with result `FAULT` and its seconds counted. A power cut loses it. |
| Tank | Median of the last 5 pings (one every 200 ms, recomputed after each), speed of sound from the air temperature, shown in > 2 mm steps; low below 10 %, OK above 15 %; 5 s with no echo counts as low (shorter dropouts are ignored). |
| Defaults | Frost 2 °C, zone minimum 4 °C, daily limit 90 min, rain stop 2 mm, 1 m² at 20 mm/h per zone, name `irrigation`. |
| Button | Decided 3 Nov: short press stops or fills the deficit, long press always toggles winter mode. See `src/logic/ButtonActions.h`. |
| Buzzer | Passive: PWM tones (LEDC channel 2, its own timer). The sound list is in `src/hw/Feedback.h`. |
| Statistics | RAM: 3 h at 3 s (soil, tank, outputs) and at 1 min (raw, weather, estimates). Flash: 15-min averages for 5 weeks in `/stats.bin` (80 kB). |
| Rain sensor | Powered only while sampling (300 ms every 30 s); two agreeing samples change the state. |
| Clock | DS1302 in UTC; SNTP corrects it when Wi-Fi is up (every 6 h); the app's "Set clock" still works offline. Without a valid RTC time the ESP32's own clock (SNTP or the app) runs it until the next power cut. |
| Log | One file per month, `/log/YYYY-MM.csv`: `utc,local,zone,mode,soil0,soil1,sec,litres,result` + ALERT and COST rows; the oldest month goes above 480 kB. Winter mode is one row each way, not a skip per entry. |
| Skip reasons | `DND ALL_OFF RAIN WET FROST TANK_LOW COLD OFF DAILY_LIMIT FAULT QUEUE_TIMEOUT QUEUE_FULL WINDOW_MISSED USER`. |
| Wi-Fi alert | Only when the connection is lost or the signal stays below −85 dBm for 5 minutes. |
| Power cut | The running watering is in flash (start + every 60 s): the next boot logs it (`FAULT`) and, if the power was back within 30 min, waters the rest once (`RESUMED`). Every boot logs why: `POWER_ON`, `BROWNOUT`, `CRASH`. |
| Wi-Fi setup | Setup network `Irrigation-Setup-XXXX` at `http://10.73.0.1/` (not 192.168.4.x: Android hotspots can use that range). The controller joins the chosen network while the setup network stays up (AP+STA), so the page shows the IP address, or "wrong password" at once; nothing is saved until the join works. Finish closes the setup network and keeps the same connection, so the IP shown stays valid (also closes a minute after the phone has left, 10 min at most). The setup network starts on the strongest network's channel, so usually the radio does not have to move when the controller joins. |

Settings and calendars are stored in NVS with a version number. This firmware is version 5; any other version is replaced by the defaults.

## Host tests

The firmware compiles for a PC with the stub headers in `test/host/stubs` (no ESP32 needed):

```
git clone --depth 1 -b v7.4.2 https://github.com/bblanchon/ArduinoJson.git /tmp/aj
test/host/build.sh /tmp/aj/src
```

307 checks: time and DST, plant age (stages, Kc curve, root growth, soil depth cap, the 1.1 → 1.2 conversion), DND windows across midnight, UTF-8 names, the 1.0 → 1.1 settings conversion, the
deficit formula, Water now (valve before pump, queue, DND and cold answers, frost, a new length mid-run),
edits during a run, cycle and soak, the schedule, the button (stop, deficit order, 15 min cap, winter
toggle, refused in winter), the output test and buzzer tones, the tank filter, hysteresis and low stop,
alerts (Wi-Fi only when unusable), the cost rollover, month log files (split, API, budget),
statistics, memory readings and the factory reset.

Not covered on the host: real sensor timing, Wi-Fi, HTTPS to Open-Meteo, flash wear, and the ESP32-only calls
(LEDC tones, LittleFS directories, `nvs_get_stats`, heap readings). Test those on the bench.

## Browser tests

`test/ui/run.sh` builds the pages and drives the UI in Chromium against the offline demo and the setup page
(Python 3 + Playwright: `pip install playwright && playwright install chromium`). It exercises every page, sheet
and setting, in English and Hungarian, light and dark, phone and desktop, and fails on any JavaScript error,
sideways scrolling or untranslated text. Screenshots go to `test/ui/shots/`. `python3 test/ui/run.py zone stats`
runs only those groups.

Before flashing, also run `pio run`: the host build does not catch ESP32-only compile errors.

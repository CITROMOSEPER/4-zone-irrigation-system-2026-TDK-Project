# Changelog

## 1.3.2
- Docs: `API-v2.md` renamed to `docs/API.md` (next to the other docs); its first paragraph explains what "v2" means (the controller's own
  HTTP API, second generation, not an outside service's version).
- **Cleanup** (`docs/code-review-1.3.2.md`): the converters for settings, cost counters and logs from firmware
  1.0-1.2.5 are gone (go through 1.3.0 when updating older controllers); the water arithmetic shared by the water
  balance, the deficit and the estimate lives in one place (`Settings::mmToPct`, `weatherMm`, `appliedMm`,
  `secondsFor`); the rain-skip fallback uses the same two-model rule; unused code and styles removed.
- **Weather, per the forecast research** (`docs/weather-api-research.md`, a month against a station 900 m away):
  Open-Meteo with two models in one call, DWD ICON and ECMWF IFS (GFS, 2.5 °C warm, is not used); daily values are
  their mean, rain is also kept per model. 7 days ahead instead of 2.
  - Rain skip: the next 24 hours (not the calendar day), and only when **both** models reach the rain-stop amount
    (one model when the other has no data). Agreement caught most wet days with few false skips.
  - Rain forecasts beyond 48 h are not used for anything.
- **Next automatic watering: expected length** on the zone card and page for the sensor- and weather-based zones
  ("Tomorrow 06:40 · about 9 min", or "probably not needed"): today's soil moved forward by the forecast ET₀ × Kc and
  the rain up to 48 h ahead. Informative only; the length is still calculated from the real reading when it starts.
  `next.est` in the state.
- A predicted rain skip on the card is now judged for the run's own time, not for now.
- **Clock: DS1302 misreads are ignored.** In the field one read gave the right day and time in 2090; it was taken at
  once, so the forecast fetched then was stamped 2090 (never "fresh": no rain rule, no estimates, a `WX` row) and a
  run was stopped by the DND window of a Tuesday. Now a read must continue the running time (±2 s); a different
  time is believed after 3 agreeing reads (someone set the RTC), and reads after 2049 never. Once SNTP or the phone
  has set the ESP32 clock this boot, that is the reference. `/health` counts the ignored reads.
  At boot and every day, what such an error left behind is undone: calendar runs marked done in the future, cost
  periods dated in the future, log month files after this month.
- Weather task: 12 kB stack (was 8 kB; the TLS handshake and the larger answer did not fit), the answer is built
  outside the stack. `/health` shows the last attempt: HTTP status (negative = no connection), size, parsed or not,
  stack left and the largest free heap block. A host test parses a real Open-Meteo answer (`test/host/real-wx.json`).
- **Power cuts**: the running watering is kept in flash (at its start and every minute), so a power cut mid-run is
  logged (`FAULT`, counted in the daily total, cost and water balance) instead of vanishing. If the power is back within
  30 minutes the rest is watered once (`RESUMED`), after the usual checks; a resumed run that is cut again is not resumed
  (a pump that browns out the supply would otherwise reboot forever).
- **More in the log**: why the controller started (`POWER_ON`, `BROWNOUT`, `CRASH`), the fallbacks (`WX` no forecast,
  `ENV_WEB` forecast temperature in use, `CLOCK_SYS` ESP32 clock in use) and `NORISE` (a sensor-based zone gone to the
  calendar). The History Problems filter leaves out informational rows.
- Home: the whole zone card opens the zone (on touch screens the soil bar and the lines below it did nothing).
- Zone calendar: the "swipe for last and next week" sentence is gone.
- **Wi-Fi setup without guessing the address**: the controller joins the chosen network while the setup network stays up
  (AP+STA). A wrong password, a network that is not there, or no answer is reported on the form within seconds, and the
  previous network is kept (credentials are saved only after a successful join). Three calm steps: Connecting
  (spinner), Connected (one Continue button; it switches off the setup network, without a reboot, so the connection and
  the IP stay the same), Almost done (the IP address as one link; the page opens the app as soon as the device is back
  on its own network; tips only after 10 s). Opened as `name.local` while joined, the page continues by itself; a stale
  copy while the controller is online says "Already online" and offers the app (the setup page is now `no-store`).
  Hungarian: "setup mód", no "beállító".
  Robustness: the setup network starts on the strongest network's channel, the join goes straight to the chosen
  network's channel (no sweep that would drop the phone), two tries, the result is kept for a reopened page, the page
  rides out the phone dropping off for a moment. The setup network moved to `10.73.0.1` (from 192.168.4.1, a range
  Android hotspots can pick).

## 1.3.0
- **New UI** (`app-v2`): touch targets of at least 44 px, aria fixes, the zone card's name is its link, Pause icon
  for DND, snowflake for winter mode, text selection only where it is worth copying, history days rendered lazily,
  at most two requests to the controller at a time (POSTs first), one text table per language, shared confirmation sheet.
- One colour palette (the unused green set and the `data-pal` attribute removed; the colours are unchanged).
- Settings → Soil sensors: the raw readings update live while you calibrate (`z[].raw` in the state; `tankNow` and
  `soilCal[].raw` removed from `/api/settings`).
- The zone parameter drawing is split into one function per section.
- Firmware: unused `trimInPlace`, `TimedOutput::onMs`, `WaterController::dailyCapS`, `Irrigation::activeSrc`,
  `Settings::today` removed.
- New setup page (`index.html`): language remembered, manual theme, selectable controller address.
- Browser tests in the repository: `test/ui/run.sh`.
- Docs: version notes moved here from `API-v2.md`; `portal/mock.js` dropped (the demo lives in `app-demo.html`).
- Date and time fields in the pause sheets line up and fit "AM/PM" (wider time fields where the browser uses a 12-hour
  clock; no picker icon on touch screens, where the whole field opens the picker).
- Zone page: the plant, soil and sprinkler sections are headed "Parameters"; the open Values row reads "Now: …".
- Settings: shorter calibration note; the button and buzzer switches have one short line each, the long press is explained under the card.
- The desktop sidebar wraps a long label instead of cutting it.
- Tank: the level is the median of the last 5 pings (was 3), so two bad echoes in a row change nothing; a real change shows after 600 ms (`TANK_MEDIAN_N` in `Config.h`).

## 1.2.11 (UI only)
chart skeletons are empty cards; the Statistics range switch has its label on wide screens.

## 1.2.10 (UI only)
skeletons in the exact shape of the content (home cards pixel-identical, Health, Settings,
History, Statistics, the zone page), so nothing moves when the data arrives; the History Cost filter is always
shown and says why it is empty (cost off / not logged / nothing yet); the Statistics zone filters share one line.

## 1.2.9 (UI only)
one Sensors card on Health (soil rows included); Statistics: the range switch across the width with
the refresh button beside it, zone filters in a 2-column grid (4 columns on wide screens); the page asks for
/api/state one request at a time (the controller serves requests one by one); charts no longer stay empty when
the page is opened before the first state arrives.

## 1.2.8 (UI only)
zone sections keep a fixed layout: one-line headers (title + preset), the preset's examples as the preset row's detail line,
the planting date greyed while growth stages are off, today's growth as the detail line of the open Values row;
the season-end switch sits under the stage table.

## 1.2.7 (UI only)
the application rate is editable again; smaller cycle-and-soak line, larger help text.

## 1.2.6
`plant.oe` (0/1, zone GET and POST): turn the zone off when the season ends. On the first day after the late
stage the zone is disabled once and the log gets `SEASON_END` (level I, that zone); turning it on again keeps it on.
Settings blob version 5 (version 4 is converted; also fixes the 1.0 conversion, whose plant layout was wrong since 1.2.0).
UI: soil and sprinkler values always shown.

## 1.2.5
tank: short echo dropouts are ignored; only 5 s without a valid reading counts as low (fail-safe), and then
`tank.ne` = 1 in the state (the UI says "no echo" instead of "tank low"). UI: no day count in the plant header,
no source line under the presets, the growth-stage switch sits above the planting date, Values can be closed.

## 1.2.4
`POST /api/reboot` → `{"ok":1}`: stops watering, waits for the valves to close, logs `REBOOT` (level I) and restarts.
UI: Network and Clock are separate cards, factory reset is the last row of Settings, restart at the bottom of Health;
presets have one-line names (examples under the list); a plant with growth stages is edited as a table
(days, Kc and roots per stage).

## 1.2.3
fallbacks and sounds.
- Clock: without a valid RTC time the controller runs on the ESP32 system clock (set by SNTP or `/api/clock`) until the
  next power cut. State `rtc` = 0 while that happens (`ok` stays 1); the UI shows a warning.
- Temperature: while the BME280 is faulty, frost, the zone minimums and the tank's sound speed use the forecast's hourly
  temperature for now (Open-Meteo `hourly=temperature_2m`, interpolated, only if the forecast is under 12 h old).
  State `env.w` = that temperature (only while the sensor is faulty).
- Sounds: winter mode from the app plays the same notes as the long press; Water now plays the start and end notes.
- Presets (UI only): 13 plants in two groups (established, from planting) incl. fruit and ornamental trees; 6 soils; 4 sprinklers.

## 1.2.2 (UI only)
Settings order: controller name, appearance, watering, tank, soil sensors, cost, hardware,
network and clock, weather location, data, about; the mowing game scales with the screen.

## 1.2.1 (UI only)
Health and Settings reorganised. Health shows short values on the right and details under
the label; the largest free block and the settings storage are no longer rows, only a warning in Problems
(lowest free heap < 40 KB, largest block < 32 KB, NVS > 85 %). `mem` in the state is unchanged.

## 1.2.0
plant age (FAO-56 stages: `plant.kci kce ri l pd`, `gr` in the state), soil depth
(`soil.depth` caps the roots), new presets (UI only), 100 % soil = field capacity (calibrate "wet" soaked and drained).

## 1.1.0
per-zone sprinkler flow and area (no global flow); short runs (2 s minimum, 1 s steps);
cycle and soak (`SOAK`, `su`); Water now on a running zone changes its length; monthly log; statistics;
memory readings; no air pressure, no rain chance, no cost season; Wi-Fi alert only when lost or unusable;
pump-alone test 3 s; new button rules and buzzer sounds.

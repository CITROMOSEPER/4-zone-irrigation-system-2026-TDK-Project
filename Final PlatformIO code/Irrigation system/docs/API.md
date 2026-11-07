# Controller API (v2.4, firmware 1.3.2)

**What "v2" means.** "v2" is the version of this controller's own HTTP API, the `/api/*` routes the web UI uses to
talk to the ESP32. Version 2 came with the redesigned web UI (the "v2 UI", `portal/app-demo.html`, once called
`app-v2.html`) in firmware 1.0 and replaced the first UI and its API. The number after the point counts additions
that keep older pages working; firmware 1.3.2 implements v2.4. Wherever "v2" appears in the code or the docs
("API v2", "v2 UI", `app-v2`), it means this. It has nothing to do with the version of an outside service: the
weather comes from Open-Meteo's own `/v1/forecast` endpoint. (Until 1.3.2 this file was `API-v2.md` in the project root.)

The current contract between the controller and the web UI (`portal/app-demo.html`; its offline demo implements
the same answers). Version history: `CHANGELOG.md`.
POST bodies are form-encoded; decimal values may use a comma or a point. Answers are `{"ok":1}` or `{"e":"<what>"}` unless noted. Times are unix seconds (UTC).

## GET /api/state  (polled every 2 s)
```
{ name, ver, hw, t, ok (clock valid), rtc (0: the RTC has no valid time, running on SNTP/phone time), lt "YYYY-MM-DD HH:MM:SS" local, up (s),
  wifi:{ssid,rssi}, env:{ok,c,h,w (forecast temperature, only while the sensor is faulty)},
  tank:{low,ne (1: no echo for 5 s, counted as low),cm,raw (last ping, unfiltered),pct}, rain,
  g:{off, frost, dnd (until, 0 = off, 4102444800 = until turned off), blk ""|ALL_OFF|FROST|TANK_LOW},
  tst:[{o,z,u}], pump, az (active zone index or -1), el (pump seconds so far), tot (run length), flt,
  mem:{free, min (lowest free heap since boot), blk (largest free block), fs, fsT (LittleFS used/total bytes), nvs, nvsT (NVS entries)},
  z:[{ name, en, m (0 schedule,1 closed,2 open), st, why, dnd, cov,
       soil (% or null; estimate when m=2), raw (the sensor's raw reading or null; live for calibrating), sf, lo, hi, su (soaking: next burst, unix; only then),
       last:{t,sec,r}|null, next:{t,sec (-1 = auto),x (skip reason, judged for that time),est (sec = -1 only: expected length from the forecast, 0 = probably not needed; absent = unknown; informative)}|null,
       def:{sec,mm}|null,
       gr:{st EST|NOT_YET|INIT|DEV|MID|LATE|OVER, d (day since planting; negative = before), kc (today), r (roots today, cm, capped by soil.depth)} }] }
```
`st`: ON, OFF, DND, WATERING, QUEUED, SOAK, ALLOFF, FAULT, WAIT (+ `why`: ALL_OFF, FROST, TANK_LOW, RAIN).
The tank `cm`/`pct` are the median of the last 5 pings (one every 200 ms, recomputed after each; a real change shows after 600 ms), corrected for the air temperature, and move only in steps of more than 2 mm; `raw` is the last ping as it came (Settings shows it as "Distance now", for calibrating). Low below 10 %, OK again above 15 %.

## Zones
- `GET /api/zone?id=N` → `{id,name,en,m,cold,cov,plant:{p,kc,root,lo,hi,kci,kce,ri,l:[ini,dev,mid,late],pd,oe},soil:{p,fc,wp,inf,depth},spr:{p,flow,area,rate,eff},sch,dsch,gdsch,hist}`
  - Plant: `kc` = Kc mid (or the established Kc), `root` = full root depth cm, `kci`/`kce` Kc at planting / at the end, `ri` roots at planting (≤ root), `l` stage days (0–365 each; all 0 = established), `pd` planting date `YYYY-MM-DD` or "" (established). `lo`/`hi` on the field-capacity scale. `oe` 0/1: turn the zone off when the season ends (once, on the first day after the late stage; logged `SEASON_END`).
  - Soil: `depth` cm (2–300).
  - `spr.flow` L/min (0.01–500), `spr.area` m² (0.01–10000), `spr.rate` mm/h = flow × 60 / area (read-only result), `spr.eff` %. `soil.inf` 1–2000 mm/h.
- `POST /api/zone` id + any of: `name en m cold cov plant.* soil.* spr.p spr.area spr.flow spr.rate spr.eff`.
  `plant.l` = "a,b,c,d"; `plant.pd` = "YYYY-MM-DD" or "" (an invalid date → `{"e":"date"}`).
  Sprinkler: `spr.area` is applied first; then `spr.flow`, or else `spr.rate` (which sets the flow for that area). Changing only the area keeps the flow.
- `POST /api/sched` id, sid (0 = new), d, dt, s, sec (2–3600; 0 in loop modes); or id, sid, del=1
- `GET /api/calendar`, `POST /api/dnd`, `POST /api/dsch`: unchanged.

## Actions
- `POST /api/water` id + `sec` or `def=1`, optional `dnd=1`, `force=1` → `{"r":"OK"|"BUSY"|"FAULT"|"ALL_OFF"|"FROST"|"TANK_LOW"|"OFF"|"DAILY_LIMIT"}`, or DND / COLD questions as before.
  - **On the zone that is running:** the run gets the new length (it runs `sec` more, or the deficit now); nothing switches, `r` = OK. A deficit of 0 ends it.
  - Runs from 2 s; lengths in 1 s steps.
- **Cycle and soak** (calendar, deficit and button runs; not a fixed-length Water now): when the zone's rate is above `soil.inf`, the dose is split into bursts that end before 2 mm stands on the surface (burst = 2 mm / (rate − inf), at least 3 s), with a pause of burst mm / inf between them (30 s – 60 min). Each burst is a log row; the zone shows `SOAK` with `su` while it waits. Stop, All off, a fault or tank low drop the rest.
- The UI shows a "Cycle and soak" note on the zone page whenever the rate is above the infiltration (same formula), and how many bursts the current deficit takes.
- Water now is refused at once in the UI (no length to pick) when the zone is off, All off, a fault, frost or tank low.
- Editing settings, the calendar or calibration never stops a run. Only disabling the zone, All off, or a DND that now covers it does.
- `POST /api/stop`, `/api/faults/clear`, `/api/clock` (t; also sets the ESP32 clock), `/api/global` (off), `/api/wifi/reset`, `/api/factory`: as before.
- `POST /api/reboot` → `{"ok":1}`: stops watering, waits for the valves to close, logs `REBOOT` (level I) and restarts. Winter mode on/off writes one log row (`WINTER_ON`/`WINTER_OFF`); calendar entries during winter are not logged as skips.

## Weather
- `GET /api/weather` → `{ok, t, lat, lon, d:[{date, tmax, tmin, et0, rain}]}` (today and tomorrow; the mean of ICON and ECMWF).
- The controller also fetches the hourly temperature; while the BME280 is faulty, frost, the zone minimums and the tank's sound speed use it (interpolated, forecast under 12 h old).

## Output test
- As before, except: the pump alone runs **3 s**; `o=buzz` plays every note the controller uses, low to high (2 s).

## Settings
- `GET /api/settings` → `{name,frost,cap,rainmm,qto,lat,lon,tankE,tankF,soilCal:[{dry,wet}],cmode,cpw,cpe,cpp,cur,clog,tot,btn,buzz}`.
  The live readings for calibrating (tank `raw`, soil `z[].raw`) come with the 2-second state.
- Theme (auto/light/dark) is kept in the browser; there is one colour palette.

## Cost
- Per period the controller counts pump **seconds** and **litres** (each zone's own flow). Cost = litres / 1000 × `cpw` (water) or `cpp` × seconds / 3 600 000 × `cpe` (power).
- `tot` = `{d,w,m,y}`, each `{sec, l, f (period start)}`. No season.
- `clog` bits 1 day, 2 week, 4 month, 16 year. `cr=1` on a price change, `POST /api/cost/reset`: as before.

## Log
- One file per local month, `/log/YYYY-MM.csv`; above 480 kB in total the oldest month is deleted.
- `GET /api/log/months` → `{m:[{k:"2026-11",b:bytes},...]}` newest first.
- `GET /api/log?m=YYYY-MM` → that month's CSV (header + rows, oldest first); without `m` the newest month.
- `GET /log.csv` → every month, oldest first, one header (streamed).
- `POST /api/log/clear` → deletes every month file (Settings → Data → Delete history). Settings stay.
- Rows logged before the clock was set have time 0; the UI shows them as "Time unknown", and dates of other years with the year.
- Rows as before; litres with 2 decimals. Alert codes add `WINTER_ON`, `WINTER_OFF`, `REBOOT`, `SEASON_END` (level I); `WIFI` = connection lost or signal below −85 dBm for 5 minutes.
  - Start reasons: `POWER_ON` (I), `BROWNOUT` (W, the supply sagged), `CRASH` (E, firmware error or watchdog). The app's restart stays `REBOOT`.
  - Fallbacks: `WX` (W, no usable forecast for 10 min: rain skips and forecast ET0 off), `ENV_WEB` (I, air temperature from the forecast), `CLOCK_SYS` (I, time from the ESP32's own clock), each with `_OK`.
  - `NORISE` (W, per zone): a sensor-based zone saw no rise after 3 runs and waters by the calendar; `NORISE_OK` when the soil responds again.
  - Mode `RESUMED`: the rest of a run a power cut stopped (the cut run itself is logged with result `FAULT` at the next boot). Resumed once, only if the power came back within 30 minutes, after the usual checks (rain, frost, tank, DND, cold, daily limit); a skipped or timed-out resume is a skip row like a calendar run.
  - The History "Problems" filter shows warnings, errors and their `_OK` rows, not the informational ones.

## Statistics
Streamed CSV; empty fields = no value.
- `GET /api/stats/fast` → first line `t0=<unix of row 1>,step=3,cols=s1|s2|s3|s4|tank_mm|flags`, then up to 3600 rows (3 hours): soil % per zone (0.5 steps, 3 s averages), tank distance in mm, flags (1 pump, 2/4/8/16 valves 1–4, 32 fault, 64 tank low, 128 rain). RAM only: a restart clears it.
- `GET /api/stats/min` → `t0=..,step=60,cols=r1..r4|temp|hum|rain|rssi|e1..e4|d1..d4`, up to 180 rows: raw soil readings, °C, %, rain 0/1, dBm (0 = offline), open-loop estimates (%), deficits (mm).
- `GET /api/stats/q?days=1..35` → `cols=t|s1..s4|e1..e4|tank|temp|hum|w1..w4`: 15-minute averages from flash (`/stats.bin`, 5 weeks), `w` = seconds watered in that quarter.

## Deficit dose
available mm = (fc − wp)/100 × roots today (cm, ≤ soil depth) × 10; daily use = ET0 × Kc today; deficit mm = (hi − soil)/100 × available;
seconds = deficit / (eff/100) / rate(mm/h) × 3600, rounded to 1 s, capped at 60 min and the daily limit.

## Button & feedback (firmware behaviour, decided 3 Nov 2026)
Winter mode from the app plays the same notes as the long press; every watering run (app, calendar, button) plays the start and end notes.
Press: tick (E7 30 ms) + LED 150 ms. If the error alarm sounds, the press only mutes it. If watering, queued or soaking: stop at once.
Short press (released before 3 s), if the press did nothing above:
- winter mode, frost, tank low or fault → refused: G6 → E6 + 3 short flashes
- `btn` setting off → nothing more
- every enabled zone with a deficit, driest first, max 15 min each; none → "nothing to water": E7 – E7 + 2 flashes
Long press (3 s): **always toggles winter mode**. On: C8 G7 E7 C7 (falling, last note held); off: C7 E7 G7 C8. No separate confirmation.
Watering started C7 → G7, finished G7 → C7; Wi-Fi connected G7 → C8 (quick); serious error alarm G7/E6 ×3, 3 s pause, repeating until a press.
No pitch slides: a passive buzzer renders them as a cheap "zip", so every sound is separate notes with short gaps.
Passive buzzer: PWM tones on LEDC channel 2 (timer 1; the LED keeps channel 0 / timer 0); the pin is LOW when silent. `buzz` = 0 mutes everything except the output test.
LED: idle off; Wi-Fi setup sine pulse 1200 ms; winter 1500 ms on every 5 s; serious error 300/200 repeating.

## Setup network (`index.html`, only while the setup network is up)
- `GET /scan` → `{"w":1}` while scanning, else `{"w":0,"f":0|1 (saved network failed at boot),"v":saved ssid,"n":[{"s","r","o"}]}`; both also carry `"h"` (hostname), `"a"` (setup network name) and `"j"`, the join state below, so a reopened page resumes. In station mode `/scan` is 404 and `/ping` says `ok`: a stale copy of the setup page says "Already online" and offers the app.
- The setup page is served `no-store`, so a copy cached under `name.local` (the setup network's DNS answers every name) cannot greet the user later.
- `GET /rescan` → `ok`, or `busy` while joining.
- `POST /save` (`ssid`, `pass`) → `{"ok":1}` and the controller starts joining that network, the setup network staying up; `{"e":"ssid"|"pass"|"busy"}`. Nothing is stored until the join works.
- `GET /status` → `{"s":"idle"|"conn"|"ok"|"fail","v":ssid,"ip":"…" (ok),"e":"pass"|"nf"|"to"|"save" (fail),"h":hostname}`. `pass`: the network refused the key (reasons 15, 202, 204); `nf`: not found; `to`: no answer in two 20 s tries.
- `POST /done` → `{"ok":1}` (only in `ok`): 0.8 s later the setup network closes and the controller carries on in station mode on the same connection. Without it: a minute after the last phone left, or 10 minutes after joining.
- `GET /ping` → `ap` on the setup network, `ok` in station mode (CORS `*`): the page polls the IP and `name.local` after Finish and opens the first that answers.

## Browser support
Unchanged (Safari/iOS 14.1+, Chrome/Edge 84+, Firefox 63+, Samsung Internet 14+). The firmware keeps serving `/health`.

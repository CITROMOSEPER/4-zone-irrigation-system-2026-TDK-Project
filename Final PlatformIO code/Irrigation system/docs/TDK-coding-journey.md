# The Story of the 4-Zone Adaptive Irrigation Controller

*TDK 2026 · Óbuda University, Kandó · Nemes Dániel & Nyul Balázs · 8 Nov 2026*

---

## Table of contents

1. [The idea](#1-the-idea)
2. [Timeline at a glance](#2-timeline-at-a-glance)
3. [Act I: Pins, power and the first heartbeat (24–25 Oct)](#3-act-i--pins-power-and-the-first-heartbeat-2425-oct)
4. [Act II: The Wi-Fi saga (25–28 Oct)](#4-act-ii--the-wi-fi-saga-2528-oct)
5. [Act III: A brain for the water (26–28 Oct)](#5-act-iii--a-brain-for-the-water-2628-oct)
6. [Act IV: The UI without hardware (29–30 Oct)](#6-act-iv--the-ui-without-hardware-2930-oct)
7. [Act V: The great rewrite, firmware 1.0.0 (30 Oct)](#7-act-v--the-great-rewrite-firmware-100-30-oct)
8. [Act VI: Reality bites, the first real test (2 Nov)](#8-act-vi--reality-bites-the-first-real-test-2-nov)
9. [Act VII: Polishing a product (2–4 Nov, v1.1 → v1.3)](#9-act-vii--polishing-a-product-24-nov-v11--v13)
10. [Act VIII: Science time, measurements and research (6–7 Nov)](#10-act-viii--science-time-measurements-and-research-67-nov)
11. [Act IX: Hardening, v1.3.2 (7 Nov)](#11-act-ix--hardening-v132-7-nov)
12. [The bug hall of fame](#12-the-bug-hall-of-fame)
13. [Fun facts and numbers](#13-fun-facts-and-numbers)
14. [How we thought: priorities and principles](#14-how-we-thought-priorities-and-principles)
15. [What we built, in one page](#15-what-we-built-in-one-page)
16. [Epilogue](#16-epilogue)

---

## 1. The idea

The research question was set on day one:

> **Does sensor- and forecast-based irrigation use less water than a fixed schedule, while keeping soil moisture in the plant's range?**

The motivation is climate change. Hungary is warming, Mediterranean and even tropical plants are becoming realistic garden choices, and water is becoming more valuable. A home irrigation controller that waters *when the plant needs it* instead of *every morning at 6* is a small, concrete answer to that.

The plan from the start:

- **An ESP32 controller** for 4 zones, with 12 V normally-closed valves, a submersible pump, capacitive soil sensors (HW-390), a BME280 for temperature and humidity, a DS1302 real-time clock, an HC-SR04 ultrasonic tank sensor, an SA-28 rain sensor, a button, an error LED and a buzzer. All of it sits on a self-built PCB with relays.
- **Three modes per zone:**
  - **Schedule:** fixed times; also the fallback.
  - **Closed loop:** follows the soil sensor.
  - **Open loop:** a water balance from forecast ET₀ × Kc minus rain, with no soil sensor.
- **A scale model** (*makett*) with four soil boxes, each 24 × 16 cm with 10 cm of soil, and a drain pipe at the bottom so the demo can run fast cycles.
- **Safety above everything:**
  - The valve opens before the pump starts, and the pump stops before the valve closes.
  - Only one zone runs at a time, every run has a hard maximum time, and everything is closed at boot.
  - A low tank stops the pump.

The deadlines were tight from the beginning: registration by 21 October (done), the paper by **11 November**, and the conference on **18 November**.

---

## 2. Timeline at a glance

| Date | What happened |
|---|---|
| **11 Oct** | A GitHub Actions workflow starts logging weather forecasts twice a day to compare forecast sources (it ran for almost a month). |
| **21 Oct** | TDK registration done. |
| **24 Oct** | The hardware is built and tested; the practical methods are set. Pinout review: strapping pins, the boot-time service mode, the echo voltage divider. |
| **25 Oct** | Daylight saving time ends. The Wi-Fi provisioning portal is written. The Wi-Fi scan mystery begins and is solved the same day. |
| **26 Oct** | Apple-style bilingual setup page. "The Wi-Fi setup works, the hardware works." The scheduler and the PC test harness are built. |
| **28 Oct** | First compile against the real ESP32 headers: the `DISABLED` macro trap. |
| **29 Oct** | "I don't have the hardware on me." The UI is designed against a fake controller. The UI spec is written; app-v2, the rain and mowing easter eggs, and the code review follow. |
| **30 Oct** | Firmware **1.0.0**: a full modular rewrite. A non-ASCII path breaks the linker, and the old UI gets served by mistake. |
| **2 Nov** | **First real test on the scale model.** Flow is 0.89 L/min, the model waters about 1390 mm/h, and a list of bugs comes in. Feature creep is discussed and the cut list agreed. |
| **2–4 Nov** | v1.1 (runs in seconds, cycle-and-soak, statistics), v1.2 (FAO-56 plant age, presets), new buzzer and button rules, an Apple-style UX audit, then v1.3.0 (the new UI). |
| **3 Nov** | mDNS doesn't work on the phone hotspot, which starts the "how do we find the controller" problem. |
| **6 Nov** | The test protocol (*jegyzőkönyv*) gets real measurements: flow per zone with a bucket and a ruler. "Look at this, how beautiful is this." |
| **7 Nov** | The weather-API research is finalised. Analysis of the competition: 8 works in our session. Firmware **1.3.2**: the two-model rain rule, the 2090 clock bug, power-cut resume, a bulletproof Wi-Fi setup. The soil sensors are calibrated. |
| **8 Nov** | The code is packed for Google Drive. This document is written. |
| **11 Nov** | Paper deadline. |
| **18 Nov, 14:00** | Conference, room TA.2.219. |

---

## 3. Act I: Pins, power and the first heartbeat (24–25 Oct)

Once the PCB was built and the hardware had been tested and worked, the practical methods and ideas were set: the three watering modes, the safety rules and the default values. The detailed work then started with a simple question: *"Can I hold the button down during boot to enter a special mode?"*

The answer was yes, because GPIO4 isn't a strapping pin. It then opened a full review of the pinout on the ESP32-WROOM-32:

- **21 pins used, no port expander needed.** The only free pins left were 0, 2 and 12 (all strapping pins), plus the UART.
- **The soil sensors sit on ADC1** (36/39/34/35). That matters because ADC2 stops working once Wi-Fi is on.
- **The HC-SR04 echo goes through a 1k/2k divider** (5 V → 3.3 V). It was already done right.
- **The buzzer on GPIO15 chirps at boot** because GPIO15 is a strapping pin. Balázs: *"The chirp on boot is actually expected."*
- **The rain sensor is powered from a GPIO** (GPIO14), so its electrodes don't corrode by sitting under constant voltage.
- **GPIO2 was kept free on purpose** for a future flow meter.

**Lesson already visible here:** the service-mode code waits for the button to be *released* before starting the button handler. Otherwise the release would be read as a short press and start watering zone 1. Small details like this kept coming back throughout the project.

Two practical questions came up as well:

- **Why does every upload take 23 s to compile and 7 s to upload?** Normal for an ESP32. The fixes were `upload_speed = 921600` and a Windows Defender exclusion for the `.pio` folder.
- **Is the relay board active-low?** Most cheap boards are. Ours turned out, when measured, to **switch on with a HIGH input**, so `RELAY_ACTIVE_LOW = false`. The first rule of embedded work: measure, don't assume.

---

## 4. Act II: The Wi-Fi saga (25–28 Oct)

> *"In projects like this, networking is always the stumbling block."* (Balázs, 7 Nov)

That sentence describes a large part of the project.

### The goal (25 Oct)

The goal was a setup that is "simple but stupidproof":

- If there are no saved credentials, the connection fails for 30 s, or the button is held at boot, the controller opens its own open Wi-Fi network.
- The phone joins it, and a captive portal pops up with a form that only accepts valid characters.
- Save → the controller reboots onto the new network → the browser redirects to the real controller.
- It must also work with a **phone hotspot** (2.4 GHz).

The first version (`WifiPortal.h`) had:

- **Two-sided validation.** An SSID must be 1–32 bytes of valid UTF-8, because hotspot names often have accents or curly apostrophes. A password must be 8–63 printable ASCII characters.
- **Captive-portal DNS** that answers every name with the controller's own address.
- **An automatic retry** of the saved network every 60 s while nobody is connected to the setup network.

### The scan mystery

Captive portal ✓, mDNS on Windows ✓. But the **list of nearby networks came back empty**: `{"w":0,"f":0,"v":"IoT WLAN","h":"irrigation","n":[]}`.

What followed was a real debugging investigation:

1. **A bare ESP32 scanned fine** in every variant (plain, async, with the AP up), so the radio and the Arduino core were fine.
2. **The bare board still failed intermittently**, a few times in 30 boots. Restarting the radio recovered it.
3. **The real firmware failed every time.**
4. **A 24-boot experiment** added the real hardware code to the bare board one group at a time (soil, tank, rain, BME280, RTC, water pins, everything):

   | Group | Failed |
   |---|---|
   | Polling only | 1/3 |
   | Tank | 2/3 |
   | RTC | 2/3 |
   | **Everything** | **0/3** |
   | others | 0/3 |

   This cleared the sensors, since "everything" passed. A statistics lesson came with it: **with a ~20 % failure rate, 3 boots per variant proves nothing.**
5. Then Balázs asked: ***"Is it possible that the laptop USB power is weak?"***

The reasoning was that Wi-Fi transmit peaks reach 300–500 mA, and a laptop USB port with a long cable plus the relay board can sag. The test took two minutes: a real 5 V power supply.

> ***"It worked with the real power supply, I have tried it twice."***

**The bug was not in the code at all.** It was a weak USB port. This became the project's favourite debugging story.

Claude had suggested the priority check along the way: *"The scan is a convenience… I'd timebox it to this one test plus one fix."* The deadline was already shaping decisions.

### The beautiful setup page (26 Oct)

Next came a redesign of the setup page with the `apple-design` skill. Balázs's priorities were, in order: **user experience, small size (so the ESP serves it fast), compatibility (browsers back to 2020)**, SVG wherever possible, and a tiny favicon of *"a small plant icon with two leaves"*.

The result was a bilingual (Hungarian/English) page, gzipped into flash, about **7 KB**.

### The phone hotspot problem (3 Nov)

At the conference there will be no router, only a phone hotspot. **mDNS (`irrigation.local`) didn't work on Android.** The options considered:

- DHCP reservation (impossible on a phone hotspot)
- a static IP
- ntfy.sh push notifications that send the IP to the phone
- an OLED display
- the Archer C5 router as a backup
- the Fing app

### The bulletproof version (7 Nov)

After a stability-ranked comparison of eight solutions, Balázs chose **AP+STA during setup**, with one instruction: *"Make it bulletproof, the best it can be. Use online experience."*

That meant online research (the Espressif FAQ on AP+STA, disconnect reason codes) and a new design:

- **The controller joins the chosen network while the setup network stays up.** A wrong password (reason 15/202/204) or a missing network (201) shows on the form **within seconds**.
- **Credentials are saved only after a successful join**, so a typo can't lock you out.
- **Three calm steps:** *Connecting → Connected → Almost done*, with the **IP address shown as a link**.
- **No reboot.** The connection, and therefore the IP, stays the same.
- **The setup network moved from 192.168.4.1 to `10.73.0.1`**, because Android hotspots can pick a 192.168.x range and then the controller can't tell the two networks apart.
- **The setup network starts on the strongest network's channel**, so the radio doesn't have to hop and drop the phone.

---

## 5. Act III: A brain for the water (26–28 Oct)

> *"The Wi-Fi setup works, the hardware works, what's the next step?"* (26 Oct)

The next step was the **job layer** on top of `WaterController` (the only code allowed to switch water). It started with Schedule mode, because that is the minimum needed to sow the ryegrass, and *"the grass has a germination clock running."*

- **Local time.** The RTC runs in UTC (DST had ended the day before). Conversion to local time happens only at the scheduling edge.
- **A daily reset at midnight.** Without it the 90 min/zone/day limit would lock every zone out after the first day.
- **Settings in NVS** and **a CSV log**, one row per run. The experiment data depends on it.

Then came a turning point in method: **the firmware was compiled on a PC against stub hardware** (fake Arduino, NVS, LittleFS and WiFi), and a test driver ran the real `main.cpp` through simulated days. It checked, among other things, that:

- *"never more than one valve open"*
- *"pump never on without exactly one valve open"*
- *"settings survive a reboot (NVS)"*

The first run had **77 checks**. That number kept growing for the rest of the project (see §13).

**The `DISABLED` trap (28 Oct).** The first real ESP32 compile produced seven errors, all pointing at one word. `enum class Block { …, DISABLED, … }` failed because **`DISABLED` is a macro somewhere in the ESP32/Arduino headers**, so the enum member became the number `0`. Balázs had guessed it was a macro. The enumerator was renamed to `ZONE_OFF`. The PC build never saw it because it didn't load those headers, which is exactly why host tests alone aren't enough.

---

## 6. Act IV: The UI without hardware (29–30 Oct)

> *"I don't have the hardware on me, I think we have an MVP, should we develop the UI, and fix the backend accordingly?"*

The concern was that designing the UI without hardware would be wasted work. The answer was **no**, because a fake controller in the browser could feed the UI any state. What really blocked a *final* UI was that the closed-loop and open-loop modes didn't exist yet. The agreed approach was to **build each feature's backend and its screen together.**

Balázs then wrote a full **UI spec** (saved to the project):

- **Zone cards** with status chips (on, off, DND, watering, queued, winter, fault).
- **Messenger-style popups** for "Do not disturb" and "Water now".
- **"Off is not a mode":** a separate enabled switch, next to the three modes.
- **A week calendar starting Monday** that also shows the global DND windows, *"so you see why a run won't happen"*.

This chat became the longest of the project: **244 turns**. Highlights:

- **A Messenger-style swipe-back** on the zone page.
- **Zone names accept emoji; the device name doesn't**, because it is the network hostname.
- **A measured fix for the desktop menu jump:** opening the menu removed the scrollbar and shifted the page; `scrollbar-gutter: stable` keeps the space reserved.
- **The calendar "squeeze" bug**, where a shrinking entry's top drifted down. It was measured pixel by pixel in a headless browser before it was fixed.
- **The easter eggs 🥚**
  - **Rain:** tap the credits 7 times and it rains on the screen for about 3 seconds. A toast says *"💧 Made by Nemes Dániel and Nyul Balázs"*. With "reduce motion" on, only the toast appears.
  - **The mowing game:** tap the firmware version 7 times and you're mowing a lawn. Over several requests it got:
    - a wilder mower (top speed 240 → 340), with drift in fast turns;
    - **physics-based crashes:** only the speed *straight into* the fence counts (over 150 px/s kills you), so a glancing touch just slides along;
    - a score multiplier: +1 every 15 s alive, and +10 % top speed per step;
    - grass that **regrows faster the more of the lawn is cut** (up to 4× on a bare lawn);
    - the best score saved per device.
    - Cost: **3.0 KB gzipped**. The code review called it *"the one self-contained block that can go"* if flash got tight. It stayed.
- **A code review** (`claude/code-review.md`), which found legacy v1 endpoints, a log format conflict (*"one format must win before the experiment starts"*), and a 256-byte JSON buffer that could silently cut off four UTF-8 zone names.

---

## 7. Act V: The great rewrite, firmware 1.0.0 (30 Oct)

> *"Analyse the UI for all of its features, and plan how it should be implemented in the firmware. Make flowcharts… But don't start coding yet."*

The plan came first, then the code. The header-only prototype became a layered project:

```
hardware  ←  safety (WaterController)  ←  logic  ←  network
```

- `hw/`: one class per device.
- `store/`: Settings and Calendar.
- `logic/`: Rules, DND, Dose, WaterBalance, Weather, RunLog, Irrigation, Scheduler, CostMeter, Alerts, TestRunner…
- `net/`: WifiPortal, Api, Health.
- **Every path that moves water** (the calendar, Water now, the button, the output test) **ends in `WaterController`**, which rechecks the tank, the hard limits, the daily budget and the valve/pump order on every pass.
- **Hard ceilings live in `Config.h`.** Values from the web page are clamped to them, so *"a bad write from the web page can only make watering more conservative, never exceed them."*
- The UI is **minified and gzipped at build time** into `AppPage.h` (about 45.7 KB). An *outdated browser* message appears on old browsers.

Two memorable problems while getting it onto the board:

1. **The "á" problem.** The linker failed: `cannot open map file C:/Users/Nyul Balázs/…/firmware.map`. Every file had compiled; only linking broke, because the ESP32 toolchain's linker (gcc 8.4) on Windows can't handle **non-ASCII characters in a path**. The project moved to `C:\.platformio\Projects\Irrigation system`.
2. **"It served the old app HTML."** The build script couldn't find the new UI file under the expected name, so it **quietly fell back to the old v1 page**. Claude's own verdict was that *"That fallback was a bad design."* The script now finds the v2 file under any of its names and refuses to build from a v1 page.

Also added that day: native time pickers, a **factory reset** (it keeps Wi-Fi and the log, stops a running zone first, and waits for the valve to close), and an answer to *"should we also serve uncompressed HTML?"*. No: gzip is a standard HTTP feature since the late 1990s, and an uncompressed copy would waste about 140 KB of flash.

First real build of 1.0.0: **RAM 17.5 % (57 KB), Flash 39.1 % (1.23 MB)**. The follow-up explanation was that compile-time RAM ignores the Wi-Fi stack (40–60 KB) and the HTTPS handshake (about 40 KB), so "263 KB free" was too optimistic.

---

## 8. Act VI: Reality bites, the first real test (2 Nov)

> *"I have some updates. Don't code yet."*

This was the first time the system watered real soil on the scale model. The findings:

- **Flow: 0.89 L/min.**
- **Water reached the drain pipe in 15–20 seconds.**
- The calculation that followed:

  > Zone area 0.24 × 0.16 m = 0.0384 m². At 0.89 L/min that's **≈ 1390 mm/h, about 0.39 mm per second.** Real garden sprinklers apply 10–40 mm/h. **The model waters 50–100× faster than a real garden.**

  A typical 6–8 mm deficit is only **15–20 seconds** of watering. That explains why the water ran out of the drain so quickly, and it meant **the whole timing model had to move from minutes to seconds.**

The bug list Balázs brought:

1. Editing "Water now" mid-run stopped the run.
2. The tank percentage flickered every 2 s (the ultrasonic sensor is only ±1–2 mm accurate).
3. The tank distance in Settings only updated after a page reload.
4. Hungarian uses a **decimal comma**, and two-decimal precision is standard at university.
5. After a reload, History lost its zone filters and showed "#1, #2" instead of zone names.
6. The buzzer gave *"two short ones"* instead of beeps. This was the clue that **the buzzer is passive, not active**.

He also asked the question that shaped the rest of the project:

> *"What would make it more robust and professional? I want the TDK judges to feel like it could be a professional product. And I have heard that the biggest trap of AI development is adding too many features. What should I do?"*

The answer became the project's rule:

> **Every change from now on must either fix something you observed, or produce evidence for the paper.**
>
> *What makes something feel like a professional product isn't the feature count. It's that nothing flickers, nothing goes stale, reloading any page works, the units are consistent, and you have measured data showing it works.*

### The cut list

Balázs then asked for the **10 least important features**, including his own proposals. He decided on each one:

| Feature | Decision |
|---|---|
| Green colour palette | **Removed:** *"it didn't even look that great"* |
| "Season" cost counter | Removed (it was supposed to be gone already) |
| Pump-alone test (10 s against closed valves) | **Shortened to 3 s** |
| Weak Wi-Fi warning | Only an unusable signal (< −85 dBm for 5 min) and connection loss get logged |
| Air pressure, chance of rain | **Removed** (no decision used them) |
| 2 decimals everywhere | Only where it matters (flow, area, prices, Kc). Extra decimals are trimmed **with an alert that explains why** |
| Cost extras, queue timeout, `/health` link, clock-drift warning, plant growth, dev leftovers | **Kept**, Balázs's call |

---

## 9. Act VII: Polishing a product (2–4 Nov, v1.1 → v1.3)

### v1.1.0: seconds, cycle and soak, statistics

- **2 s minimum run, in 1 s steps.** The dose counts *pump-on* seconds, because the 1.5 s delay before the pump starts is a 15 % error on a 10 s run.
- **Flow (L/min) and area (m²) per zone**, with the application rate (mm/h) calculated live. Manufacturers (Rain Bird, Hunter) publish both figures, and controller forums show people mixing up L/min and mm/h.
- **Cycle and soak.** Real controllers split a run into bursts with pauses so the water can soak in. On the model, water runs straight past the sensor otherwise.
- **Statistics and charts**, drawn by the page itself with no chart library (the controller may have no internet). The chart worth most to the judges: *"soil moisture over time with the lo/hi band; if the closed loop works, the result is a clean sawtooth inside the band."*

### The C++11 surprise

The first build of v1.1.0 on the real board failed:

```
*** [.pio\build\esp32dev\src\logic\ButtonActions.cpp.o] Error 1
*** [.pio\build\esp32dev\src\logic\Irrigation.cpp.o] Error 1
*** [.pio\build\esp32dev\src\logic\Scheduler.cpp.o] Error 1
```

**The PC tests ran in C++17, but the ESP32 toolchain compiles C++11.** In C++11, a struct with a default member value (`float soil0 = NAN;`) can't be filled with `{…}`. It got a constructor, and from then on **every file is also checked under C++11** before delivery.

### v1.2.x: plants that grow up

Balázs wanted to give growing plants a *"birth date"* and compensate for growth. FAO-56 already describes this:

- **Four growth stages** (initial, development, mid-season, late), with Kc following the curve and root depth growing over time.
- **"Lawn from seed"** for the ryegrass sown in November.
- **13 plant presets, 6 soils and 4 sprinklers**, checked against FAO-56, the Minnesota Stormwater Manual, WUCOLS and HydroPoint. The weak spots were flagged honestly ("potting mix: estimate, should be marked as such").
- **Online fallbacks:**
  - **No RTC?** The ESP32 system clock is used, set by SNTP or the phone.
  - **BME280 broken?** The forecast's hourly temperature is used for the frost and cold rules.

### The buzzer becomes a musical instrument

Once it was clear the buzzer was **passive** (it only clicks on DC changes), the sounds were redesigned from scratch:

1. **Up means on/start, down means off/end.**
2. **High means OK, low means a problem.**
3. **Notes are events; slides are mode changes.**
4. **Short means "got it"; repeating means "come and look".**

The notes are real musical pitches near the buzzer's loudest range: **C7 2093 Hz, E7 2637 Hz, G7 3136 Hz, C8 4186 Hz**, plus E6 and G6 for problems. "Watering started" is C7 → G7, "Wi-Fi connected" is a rising C–E–G chord, and winter mode is a slow 600 ms slide down ("going to sleep") and up ("waking up").

### New button rules (3 Nov, Balázs's design)

> *"Long press reached needs no sound. Long press should always toggle all off (winter mode), and short should always start filling the deficit for all relevant zones, or cancel the current watering if there is any."*

Every press has one meaning, and the sound confirms it.

### The Apple-style audit (3–4 Nov)

> *"You are a senior app designer at Apple and you have to pinpoint ways to improve. Don't edit code, just explain."*

The verdict: *"Already far above typical ESP32 web UIs."* The audit still found real issues:

- Many touch targets were under 44 pt.
- 10 px calendar text.
- `user-select: none` on the whole page, so nobody could copy the IP address.
- Zone cards that a keyboard or screen reader couldn't reach.
- Jargon: "DND" means nothing for a sprinkler, and "Closed loop / Open loop" became "Sensor-based / Weather-based".
- The Health page rebuilt itself every 2 seconds.

The result was **v1.3.0**: 44 px touch targets, aria fixes, a pause icon for DND, a snowflake for winter mode, at most two requests to the controller at a time, and **browser tests in the repository** (`test/ui/run.sh`) that go through every page in both languages, both themes, and phone and desktop sizes.

---

## 10. Act VIII: Science time, measurements and research (6–7 Nov)

### The bucket experiment (6 Nov)

> *"Now I'm measuring the system. The bucket is 21 cm in diameter, I ran each zone for 56 seconds."*

The method was simple and good: a cylindrical bucket (A = π · 10.5² ≈ **346.4 cm²**, so 1 cm of level ≈ 0.346 L), a ruler, and **three repeats**:

| Zone | Avg. flow (3 runs) |
|---|---|
| 1 | **1.07 L/min** |
| 2 | **0.62 L/min** |
| 3 | **0.80 L/min** |
| 4 | **0.96 L/min** |

Surprises:

- The earlier "all zones identical, 0.89 L/min" was really **0.62–1.07**. The firmware default of 2 L/min would have **doubled every litre figure in the log** (and tripled it for zone 2).
- Zones 2 and 3 scattered from run to run, **but their combined drop was constant** (3.95 / 3.75 / 3.85 cm). The likely cause was a reading at the boundary between those two zones, not the zones themselves.
- Claude first suggested that water draining back from the pots explained the scatter. Balázs corrected it: he had used **two buckets**, kept the drain out of the measured one, and didn't want to pump the dirty water. The hypothesis was withdrawn: *"that means I was wrong."*

Then the moment of the day:

> ***"Look at this, how beautiful is this."***

The statistics chart showed the three measurement rounds as **clean staircases**, four steps each, with the watering bands lining up exactly. The tank sensor even caught the drainage being returned and every tap refill. It was an unplanned validation of the ultrasonic level measurement.

The chart also revealed something new: **soil moisture *dropped* during each watering.** Real soil doesn't get drier while it is being watered, so this was electrical: the pump or the relays pulling down the sensor supply or the ADC reference. The firmware's "check 10 minutes after the run" already avoids most of it.

### Soil calibration (7 Nov)

- First attempt: **dry point in air (~3200)**, **wet point in freshly watered soil (~1115–1136)**.
- Balázs pushed back: *"this isn't right, because soil not watered for 24 hours was also around 1200–1250."*
- That corrected the definition: **100 % should be field capacity, which is soil soaked and then drained for a day**, not saturated soil. The wet point became **~1230**.
- The **known limitation**, to be stated in the paper: the dry point is approximate, so the percentage is a relative moisture index.

### The weather-API research (11 Oct – 7 Nov)

This is the project's most "scientific" piece of independent research.

**Method:**

- A GitHub Actions workflow logged forecasts **twice a day for about four weeks**: DWD ICON, ECMWF IFS and NOAA GFS through Open-Meteo, plus MET Norway.
- The reference was a hobby weather station (IBUDAP116, about 900 m away), with 15-minute data.
- Scoring counted each target hour once per model and per lead-time bucket. **This lesson cost real effort:** counting every forecast run inflated the rain totals.
- **27 scored days**, about 700 hours per model in the 0–24 h window.

**Results:**

| Model | Temperature bias | Temperature error (0–24 h) | Daily rain error |
|---|---|---|---|
| ECMWF | −0.07 °C | 1.51 °C | 1.24 mm |
| met.no | −0.11 °C | 1.50 °C | 1.07 mm |
| ICON | +0.56 °C | 1.65 °C | **0.78 mm**, caught 6/6 wet days |
| **GFS** | **+2.46 °C** | **3.18 °C** | 0.86 mm |

- **GFS runs about 2.5 °C too warm** and was dropped.
- **Rain forecasts are usable 1–2 days ahead; beyond about 3 days they are mostly noise.** ECMWF at 5–7 days: 1 hit, 3 misses, 5 false alarms.
- **A skip rule:** "skip if at least 2 models forecast ≥ 2 mm in the next 24 h" caught 5 of 6 wet days with one false skip.
- **All models forecast about twice the station's ET₀.** The conclusion was that the *station* is the problem: it is sheltered (wind 0.2–5.8 km/h) and never got above 55 % of clear-sky solar radiation.
- **Lessons noted honestly:** an 8-day first look showed a −0.7 °C bias that disappeared at 27 days, so small samples mislead. Most weather apps repackage the same few models. And time zones bite (UTC vs. local time, plus the 25 October clock change).

This went straight into the firmware: **Open-Meteo with ICON + ECMWF in one call, and a rain skip only when both models agree.**

---

## 11. Act IX: Hardening, v1.3.2 (7 Nov)

### The year-2090 bug 🕰️

In real use, **one DS1302 read returned the right day and time, but in the year 2090.** It was accepted at once, and then:

- the weather forecast fetched at that moment was stamped 2090 (never "fresh", so no rain rule and no estimates);
- **a run was stopped by the DND window of a Tuesday**;
- calendar runs got marked as done until 2090, which would have kept them from starting for decades.

The fix:

- **A clock read must continue the running time (±2 s).** A different value is believed only after **3 agreeing reads**, and dates after 2049 never.
- **Self-repair at boot and once a day:** calendar runs marked done in the future, cost periods dated in the future, and log files for future months are undone.
- **Hardware advice:** DS1302 misreads usually come from long or loose wires or a shared supply with the pump; shorter wires and a 100 nF capacitor help.

### Power cuts

*"How does it handle power cuts? Does it lose its memory?"* The honest answer found **a gap**: a run in progress at the moment of a power cut vanished without a trace and didn't count toward the daily limit. Then Balázs asked: *"And can it continue too?"*

- The running watering is now kept in flash (at its start and every minute).
- A cut run is logged as `FAULT`.
- If the power is back **within 30 minutes**, the rest is watered **once** (`RESUMED`), after the usual checks.
- **"Only once"** is deliberate: if the pump's inrush causes the brownout, an endless reboot loop would follow otherwise.
- Every boot now logs why it started: `POWER_ON`, `BROWNOUT` or `CRASH`.

### Also in 1.3.2

- **"Next automatic watering: about 9 min"** (or "probably not needed") on the zone card, predicted from the forecast. It is informational only; the real length is still calculated from the real reading when the run starts.
- **The weather task's stack grew from 8 to 12 kB**, because the TLS handshake plus the larger two-model answer didn't fit.
- **The whole zone card is clickable** (on touch screens, the soil bar used to do nothing).
- **A cleanup audit** removed old converters, merged the duplicated water arithmetic into one place, and removed unused code and styles. Then everything was compiled again with `-Wall -Wextra` under **C++11**.

---

## 12. The bug hall of fame

| # | Bug | Root cause | Lesson |
|---|---|---|---|
| 🥇 | Wi-Fi scan returns no networks | **A weak laptop USB port**, not the code | Check power first; a 2-minute test beat days of code changes |
| 🥈 | Forecast stamped 2090, a Tuesday's DND stops a run | A single DS1302 misread accepted as true | Never trust one reading; require agreement |
| 🥉 | Linker can't open `firmware.map` | The **"á" in "Balázs"** in the Windows path | Keep embedded projects on ASCII-only paths |
| 4 | 7 compile errors on one line | `DISABLED` is a macro in the ESP32 core | PC tests can't see real-platform macros |
| 5 | 3 compile errors after a big release | C++17 on the PC vs **C++11** on the ESP32 | Compile with the target's standard |
| 6 | The new firmware serves the old UI | The build script quietly fell back to the v1 file | Fail loudly, never fall back quietly |
| 7 | Bench board always boots into service mode | Inverted button logic with the GPIO2 "ground" | Pull-ups and pull-downs decide what "pressed" means |
| 8 | Buzzer only clicks | It's **passive**; it was driven like an active one | Read the part, or listen to it |
| 9 | Tank % flickers every 2 s | Sensor noise ±1–2 mm and no filtering | Median of 5, temperature correction, 2 mm display steps, 10 %/15 % hysteresis |
| 10 | Soil % drops *while* watering | The pump or relays pull down the sensor supply / ADC reference | Measure soil after the run, not during |
| 11 | Water-now edit mid-run stops the run | Edits were treated as stop + restart | Edits never stop a run, except for 3 explicit reasons |
| 12 | History shows "#1, #2" after reload | Rendered before the zone names arrived | Fetch what you depend on first |
| 13 | Litres in the log twice too high | Firmware default 2 L/min vs a real 0.62–1.07 | Calibrate everything you report |
| 14 | A run in progress vanishes in a power cut | It was held in RTC memory, which survives a reboot but not a power cut | Persist what matters; resume only once |

---

## 13. Fun facts and numbers

- 💬 **The two longest chats:** "Final UI – without hardware" with **244 turns**, and "Testing and bugfix !!!" (three exclamation marks earned) with **140 turns**.
- 🌊 **The model waters about 1390 mm/h**, 50–100× a real garden. A real lawn's daily need is a **15–20 second** run.
- 🧪 **Test checks over time:** 77 → 128 → 139 → 222 → 282 → 307 → **403 host checks + 321 browser checks, 0 failed** (1.3.2).
- 📦 **The app:** about 151 KB raw, **48.7 KB gzipped**, of which the mowing game is 3.0 KB. The setup page is about **7 KB**.
- 🎵 **The buzzer plays a C-major chord** (C7–E7–G7) when Wi-Fi connects.
- 🌡️ **GFS ran 2.46 °C too warm** over the measured month.
- 🌧️ **27 scored days, 49.1 mm of rain, 6 wet days**: small enough that the research says so itself.
- 🔢 **13 plant presets, 6 soils, 4 sprinklers**, all with a documented source or marked as estimates.
- 🪣 **Flow measured with a 21 cm bucket and a ruler** gave ±0.05 L/min accuracy. Low-tech, honest, repeatable.
- 🏠 **The setup network lives at `10.73.0.1`**, chosen because no phone, router or tethering picks it.
- 📡 **Wi-Fi reason codes learned the hard way:** 201 = network not found, 15/202/204 = wrong password or security problem.
- 🧩 **14 skip reasons** in the log, from `DND` to `WINDOW_MISSED`.
- 🎮 **Mowing game physics:** only the speed component *perpendicular* to the fence can kill you, above 150 px/s.
- 💧 **The rain easter egg's toast** is the only place in the firmware where both authors' names appear.

---

## 14. How we thought: priorities and principles

Some principles came up again and again until they were simply how the project worked.

1. **Safety is a layer, not a feature.** One class switches water, and everything else asks it. Hard limits are compiled in, and settings can only make watering *more* careful.
2. **Fail safe, then fall back.** No tank echo means the tank counts as low. No RTC means the system clock is used. No BME280 means the forecast temperature is used. No forecast means Hargreaves from the local sensor. No soil response after 3 runs means the zone falls back to its schedule. *Every fallback is logged.*
3. **The controller is autonomous.** The internet improves it, but nothing *depends* on it. Watering continues without Wi-Fi, without internet and without the app.
4. **Measure, don't assume.** Relay polarity, flow rate, buzzer type, soil calibration, sensor reaction time: each assumption that was measured turned out to be wrong.
5. **Every change must fix an observation or produce evidence.** This is the defence against feature creep, and it came from Balázs's own worry about it.
6. **Professional means nothing flickers, nothing goes stale, and every reload works.** Not the feature count.
7. **Plan before code.** "Don't code yet" appears again and again in Balázs's messages. Plans, rankings and to-do lists came first, code second.
8. **Timebox the rabbit holes.** The Wi-Fi scan got "one test plus one fix" because the paper deadline mattered more.
9. **Be honest about limits.** Autumn only, indoors, small sample, a hobby reference station, an approximate dry point. These are stated as limitations, which the judges reward, rather than hidden.
10. **Test on the PC, then on the real thing.** Host tests and browser tests catch logic errors fast. Only the real board catches macros, C++11 and power.
11. **Balázs decides; Claude advises.** The cut list, the button rules, the AP+STA choice, keeping the cost extras, the easter eggs: the user made the calls, sometimes against the recommendation (plant growth was suggested as "future work" and was built anyway).

---

## 15. What we built, in one page

**Firmware v1.3.2** (ESP32, Arduino core 2.0.17, PlatformIO):

- **3 watering modes per zone:** fixed schedule, sensor-based (closed loop), weather-based (FAO-56 water balance with Open-Meteo ICON + ECMWF ET₀, Hargreaves offline).
- **FAO-56 plant age:** Kc curve and root growth from a planting date.
- **Short runs** (2 s minimum), **cycle and soak**, per-zone flow and area.
- **A two-model rain skip** for the next 24 h; nothing beyond 48 h is used.
- **Safety:** valve-before-pump, one zone at a time, hard caps, a daily budget, tank-low stop with hysteresis, frost and cold rules, winter mode.
- **Robustness:** RTC misread filter, power-cut logging and a one-time resume, online fallbacks for time and temperature, `NORISE` fallback to the schedule, boot-reason logging.
- **Logging:** monthly CSV files, 14 skip reasons, alerts, cost counters, 5 weeks of 15-minute statistics.
- **Wi-Fi:** AP+STA setup at `10.73.0.1` with a live result, the IP shown, credentials saved only after a successful join, and working with a phone hotspot.
- **Button:** short press = stop / fill the deficit; long press = winter mode. **Buzzer:** a musical sound language.

**Web app:** bilingual (HU/EN), light and dark, Apple-inspired, gzipped into flash, with live charts drawn by the page itself, a calendar, History, Statistics, Health and Settings, plus two easter eggs.

**Testing:** 403 host checks, 321 browser checks, a C++11 sweep, and a test protocol on the real model.

**Research:** a month-long comparison of four forecast models against a local station.

---

## 16. Epilogue

In two weeks the project went from *"can I hold the button during boot?"* to a controller that filters its own clock, survives power cuts, chooses its weather models from its own research, and plays a C-major chord when it joins Wi-Fi.

The hardest bugs weren't in the logic. They were a weak USB port, a letter "á", a hidden macro, an older C++ standard and a clock that briefly lived in 2090. The best moments weren't features either. They were a staircase chart after a session with a bucket and a ruler, and the words *"look at this, how beautiful is this."*

Two first-year students, eight works in the session, three podium places. The tools are ready, and the data is coming in.

**Good luck on the 18th. 🌱💧**

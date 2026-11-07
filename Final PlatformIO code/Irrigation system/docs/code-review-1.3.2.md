# Code review: 1.3.2 cleanup

Scope: the firmware (`src/`, `include/`), both pages (`portal/app-demo.html`, `portal/index.html`), the tests and the docs.
Method: scripted scans (functions, constants, settings fields and API routes used only where they are defined; text
keys, CSS classes and element ids the pages never use), then reading the hits and the larger files by hand.
Checks afterwards: host tests 403 / 0 failed, browser tests 321 / 0 failed, every source file compiled as C++11
(the ESP32 toolchain) with `-Wall -Wextra` and no warnings. The ESP32 build itself (`pio run`) is still to be run.

## Changes

| # | What | Why | Risk and how it is covered |
|---|---|---|---|
| 1 | Settings converters for versions 2-4 removed (firmware 1.0-1.2.5), with their old structs and `migratedFlow()` | Every controller has booted 1.3.x and kept its data (confirmed 7 Nov) | A controller still on 1.0-1.2.5 that jumps straight to 1.3.2 starts from the defaults (names, calibration, calendars). The README says to go through 1.3.0 first. New test: an unknown version gives the defaults and nothing else breaks. |
| 2 | The 1.0 cost counter converter (`"cost"` key) removed | Same | The current key `"cost3"` is kept on purpose: renaming it would zero every counter. |
| 3 | The 1.0 log splitter (`/log.csv`, `/log_old.csv` into month files) removed | Same | An unsplit 1.0 log file would stay on the flash, unread; no controller has one any more. |
| 4 | Water arithmetic shared: `Settings::mmToPct`, `weatherMm`, `appliedMm`, `secondsFor` | The same four formulas were written out in the water balance, the deficit and the forecast estimate, and had to change together | The formulas are moved, not changed (same order of operations). The deficit test (984 s for a known zone), the water-balance tests and the estimate tests pass unchanged. |
| 5 | The rain-skip fallback (no hourly data) uses the per-model daily totals with the same agreement rule | `rainM` was stored but never read; the fallback used the mean, a different rule from the main one | Only used when the hourly rain is missing. Same threshold, now consistent with the main rule. |
| 6 | `Weather::fresh()` reads only the fetch time | It copied the whole forecast cache (about 1 kB) every second for one number | Read under the same mutex; behaviour identical (all forecast tests pass). |
| 7 | `Stats::fileBytes()` and `WifiPortal::join()` removed | Never called | None: the compiler would report any caller. |
| 8 | Setup page: `.btn.sec`, `.cd` and `.ck .done` styles removed | No element uses them (leftovers of the earlier countdown design) | Visual only; the setup screenshots and the browser tests are unchanged. |
| 9 | App: `todayYmd()` built on `ymd()` | Two copies of the same date format | Same output. |
| 10 | Docs: README (updating, NVS version, host test list) and API (log migration) brought up to date | They described the converters | None. |

## Checked and kept on purpose

- **Watering maths in the UI** (`deficit`, `soakPlan`, cost): duplicates the firmware so the page can show the
  result while the user types, before anything is saved. Keeping both in step is a known cost; the browser tests
  compare them with the demo.
- **Test-only hooks** in the firmware: `Feedback::toneNow()`, `Weather::inject()`, `WifiPortal::server()`. Each is one
  line, marked as such, and lets the host tests check behaviour without the hardware.
- **Easter eggs** (the mowing game, the cactus toast, rain taps): small, isolated, asked for earlier.
- **Text keys that look unused** (`r_*`, `pp_*`, `ps_*`, `pk_*`, `al_*`, ...): they are built at run time from a
  prefix and a code. A strict scan found no key that nothing can reach, and en/hu have the same 517 keys.
- **CSS classes `QUEUED`, `SOAK`, `l1`-`l4`**: set from data (`'ch '+state`, `'sig l'+level`).
- **The clock read from two tasks**: before SNTP the weather task reads the RTC-based time while the loop updates it.
  A torn read can be off by one second at most, which does not matter for a forecast timestamp. After SNTP the
  system clock (thread-safe) is the reference.
- **Every API route** is called by one of the two pages; **every settings field** is read somewhere; no constant in
  `Config.h` is unused.

## Possible later work (not done: each has a cost)

- One shared source for the UI's and the firmware's watering maths (would need a code generator).

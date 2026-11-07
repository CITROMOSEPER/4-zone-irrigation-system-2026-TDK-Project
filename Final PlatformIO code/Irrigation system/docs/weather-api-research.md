# Weather forecast source evaluation for the irrigation controller

Budapest, 9 October - 7 November 2026. Scored period: 27 days (10 Oct - 5 Nov).

## Summary

- **Best sources:** ECMWF and MET Norway (met.no) had the lowest temperature and humidity errors. ICON had the lowest daily rain error and caught every wet day, but the rain sample is small (6 wet days). **GFS was clearly the worst** (+2.5 °C warm bias, 3.2 °C error).
- **Practical choice:** Open-Meteo (ICON + ECMWF in one call, no API key, includes ET₀) with met.no as a second source.
- **Rain forecasts are usable 1-2 days ahead when several models agree. Beyond about 3 days they are not reliable enough for irrigation decisions.**
- A local rain gauge or soil sensor is still needed as the final check.
- **Limits:** autumn only, one unvalidated reference station, 6 wet days.

## Question and background

Which forecast source should the controller use, and is a local weather station needed? We found no public, independent ranking of weather APIs for Budapest.

- HungaroMet verifies ECMWF, ALADIN and AROME against Hungarian stations every year, but only at national level and not for commercial APIs.
- A global precipitation verification (EMS 2022, over 10,000 stations) found ICON had the lowest error of the operational global models, followed by GFS, ARPEGE and NEMS.
- Most weather services repackage a few shared models (ECMWF, ICON, GFS) and differ mainly in post-processing. Open-Meteo and met.no expose the model output directly, so they are the transparent options for comparison.
- Google Weather needs a billed cloud account. For Időkép and Köpönyeg we found no public API, so they were not tested.

## Method

- **Forecasts logged twice a day** by a GitHub Actions workflow. Each row stores when the forecast was issued and the hour it predicts (needed to measure accuracy by lead time).
  - Open-Meteo models: `icon_seamless` (DWD ICON), `ecmwf_ifs025` (ECMWF IFS), `gfs_seamless` (NOAA GFS), 7 days hourly.
  - MET Norway Locationforecast, about 60 hours hourly.
- **Reference:** personal weather station IBUDAP116 (Weather Underground), about 900 m from the site. 15-minute exports of temperature, humidity, rain, wind and solar radiation. It is not validated against an official station.
- **Scoring (`compare.py`):**
  - Each target hour is counted once per model and lead-time bucket, using the freshest forecast in that bucket.
  - Rain is also compared as daily totals, for days with at least 23 matched hours.
  - Station ET₀ is calculated with FAO-56 Penman-Monteith and compared with the forecast ET₀.
- About 700 hours per model in the 0-24 h bucket.

## Results

### Temperature and humidity (hourly)

| Model | T bias, 0-24 h (°C) | T error 0-24 h | T error 2-3 days | T error 5-7 days | RH error 0-24 h (%) |
|---|---|---|---|---|---|
| ECMWF | -0.07 | 1.51 | 1.72 | 2.32 | 8.6 |
| met.no | -0.11 | 1.50 | 1.71 | n/a (about 60 h) | 8.2 |
| ICON | +0.56 | 1.65 | 1.80 | 2.40 | 12.4 |
| GFS | +2.46 | 3.18 | 3.52 | 4.00 | 27.3 |

T error is the mean absolute error in °C, bias is forecast minus observed. All models read drier than the station (7-27%), but the station's humidity sensor is not validated, so humidity is treated with caution.

### Rain (daily, next-24 h forecast)

The station measured 49.1 mm over the 27 days, with 6 wet days (1 mm or more): 10 Oct (21.1 mm), 11 Oct (7.4), 22 Oct (8.9), 25 Oct (3.8), 2 Nov (5.3), 3 Nov (1.1).

| Model | Daily error (mm) | Forecast total (mm) | Wet days 0-24 h: hit / miss / false | Wet days 24-48 h: hit / miss / false |
|---|---|---|---|---|
| ICON | 0.78 | 54.0 | 6 / 0 / 2 | 2 / 3 / 1 |
| GFS | 0.86 | 44.4 | 5 / 1 / 0 | 2 / 3 / 1 |
| met.no | 1.07 | 44.0 | 5 / 1 / 2 | 3 / 2 / 3 |
| ECMWF | 1.24 | 50.1 | 5 / 1 / 2 | 3 / 2 / 1 |

- The differences between ICON, ECMWF and met.no on rain are too small to rank with 6 wet days.
- At 5-7 days ahead the rain forecasts are mostly noise (for example ECMWF: 1 hit, 3 misses, 5 false alarms).
- Forecasts can flip between runs. For 2 Nov (5.3 mm measured), every model forecast under 1 mm the day before, but ECMWF and ICON had forecast 4-8 mm three days earlier.

### A skip-irrigation rule

"Skip if at least 2 of ICON, ECMWF and met.no forecast 2 mm or more in the next 24 h":
- Next 24 h: 5 of 6 wet days caught, 1 false skip (26 Oct, when 0.8 mm fell).
- 24-48 h ahead, with a 3 mm threshold: 3 of 5 wet days caught, no false skips.

This is a starting point from a small sample, not a validated rule.

### Evapotranspiration (ET₀)

All models forecast about twice the ET₀ calculated from the station (ICON and ECMWF about 2.0-2.2 times, GFS about 2.5 times). We think the station is the problem, not the models: its wind averages only 0.2-5.8 km/h (sheltered), and its solar radiation never exceeded 55% of the clear-sky value, even on dry days. Both lower the station-derived ET₀. ICON and ECMWF agree with each other, so the station comparison is not a reliable test of ET₀.

## What we learned

1. **Count every hour once.** Using every forecast run inflated the rain totals. Scoring each hour once per lead bucket fixed this.
2. **Daily rain totals are more meaningful than hourly.** Hourly scoring is dominated by light drizzle forecast at the wrong hour.
3. **Small samples mislead.** An 8-day first look showed a -0.7 °C bias for ECMWF and met.no. With 27 days it disappeared.
4. **A service's "current weather" is not ground truth,** because it is itself an estimate. Observations must come from a station.
5. **Reference data quality matters.** The weekly Weather Underground export is unusable (one daily extreme per column), and the station's solar and wind sensors make ET₀ unreliable.
6. **Handle time carefully.** Forecasts are in UTC, the station in local time, and the clock changed on 25 October.
7. **Models differ in what they provide.** met.no has no rain probability or ET₀, and it reports rain for the hour starting at a time, while Open-Meteo reports the hour ending at it, so they must be aligned.
8. **Most "different" services use the same few models,** so a comparison of models is more informative than a comparison of apps.

## Implications for the controller

- Use Open-Meteo as the primary source (ICON + ECMWF, no key, ET₀ included), with met.no as a second source. Do not use GFS.
- Use forecast ET₀ from ICON/ECMWF for the water-balance mode, with Hargreaves as the offline fallback.
- Use a multi-model rain-skip rule for the next 1-2 days only, and let a rain gauge or soil sensor override it.
- Do not base irrigation decisions on rain forecasts more than about 3 days ahead.
- A full local weather station is not needed. A rain gauge and soil moisture sensors cover what the forecasts get wrong.

## Limitations

- Autumn only (low evaporation, frontal rain). Summer showers are patchier and are the real test for rain forecasts.
- 6 wet days, so rain results show tendencies, not statistically solid rankings.
- The reference station is a hobby station, about 900 m away, not validated against an official HungaroMet station.
- Open-Meteo "seamless" models blend several underlying model runs, so results describe the API product, not a single model run.

## Reproducibility

- `log_forecast.py` and `log-forecast.yml` (GitHub Actions, twice daily) append to `forecast_log.csv`.
- `compare.py` reads `forecast_log.csv` and the daily station exports (`weather-history-YYYY-MM-DD.csv`) and writes `station_hourly.csv`, `comparison.csv` and `daily.csv`.
- Re-run after a summer month to test rain forecasts in convective conditions.

## Sources

- [HungaroMet: Application and verification of ECMWF products, 2024](https://www.ecmwf.int/sites/default/files/elibrary/092024/81598-application-and-verification-of-ecmwf-products-2024-hungary.pdf)
- [Schlögl et al.: Global verification of the 24-hour precipitation forecast of NWP models, EMS 2022](https://meetingorganizer.copernicus.org/EMS2022/EMS2022-383.html)
- [Open-Meteo: Best weather models in one open-source API](https://openmeteo.substack.com/p/best-weather-models-in-one-open-source)
- [MET Norway API terms of service](https://api.met.no/doc/TermsOfService)
- [OpenWeatherMap pricing](https://openweathermap.org/price)
- [Weather Underground station IBUDAP116](https://www.wunderground.com/dashboard/pws/IBUDAP116)

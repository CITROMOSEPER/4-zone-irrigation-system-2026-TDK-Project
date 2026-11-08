"""Compare logged forecasts with readings from a Weather Underground station.

Usage:
    python compare.py                      # forecast_log.csv + every weather-history-*.csv here
    python compare.py forecast_log.csv file1.csv file2.csv ...

You do NOT need to merge anything. Put forecast_log.csv and all the DAILY station
exports (weather-history-YYYY-MM-DD.csv, the date = the day the data covers) in
one folder and run the script; it reads them all. Weekly/monthly exports are skipped.

Files written to the current folder:
  station_hourly.csv  all station exports merged into one hourly table
  comparison.csv      one row per model / target hour / lead-time bucket
  daily.csv           one row per model / lead-time bucket / day (rain and ET0)

Printed summaries: hourly (temperature, humidity, rain), daily rain, daily ET0.

Each target hour is counted once per model and lead-time bucket, using the
freshest forecast that falls inside the bucket, so repeated forecast runs do
not inflate the numbers.
"""
import csv
import glob
import math
import os
import re
import sys
from collections import defaultdict
from datetime import date, datetime, timedelta, timezone
from zoneinfo import ZoneInfo

# --- Settings -------------------------------------------------------------
LOCAL_TZ = ZoneInfo("Europe/Budapest")
UTC = timezone.utc
LATITUDE = 47.526        # station IBUDAP116
ELEVATION_M = 39         # 127 ft
RAIN_THRESHOLD_MM = 0.1  # an hour counts as "rainy" at or above this
WET_DAY_MM = 1.0         # a day counts as "wet" at or above this
MIN_HOURS_PER_DAY = 23   # a day needs this many matched hours to be scored
BUCKETS = [(0, 24, "0-24h"), (24, 48, "24-48h"), (48, 72, "48-72h"),
           (72, 120, "72-120h"), (120, 168, "120-168h")]
# --------------------------------------------------------------------------

BUCKET_ORDER = [b[2] for b in BUCKETS] + [">168h"]
NUM = re.compile(r"-?\d+(?:\.\d+)?")


def num(text):
    m = NUM.search(text or "")
    return float(m.group()) if m else None


def bucket(lead_h):
    for lo, hi, name in BUCKETS:
        if lo <= lead_h < hi:
            return name
    return ">168h"


def day_of(t):
    """Local calendar day (YYYY-MM-DD) an hour belongs to. An hour is labelled by
    its end (rain) or centre (readings), so the midpoint is used."""
    return (t - timedelta(minutes=30)).astimezone(LOCAL_TZ).date().isoformat()


# ---- Station data --------------------------------------------------------
def load_station(paths):
    readings = {}  # UTC datetime -> values; later files overwrite duplicates
    for path in paths:
        m = re.search(r"(\d{4}-\d{2}-\d{2})", os.path.basename(path))
        with open(path, newline="", encoding="utf-8-sig") as f:
            rows = list(csv.DictReader(f))
        if not rows or not re.search(r"[AP]M", rows[0].get("Time", "")):
            print(f"skipping {path}: not a daily (15-minute) export")
            continue
        if not m:
            print(f"skipping {path}: no YYYY-MM-DD date in the file name")
            continue
        day = m.group(1)
        for r in rows:
            try:
                local = datetime.strptime(
                    f"{day} {r['Time'].strip()}", "%Y-%m-%d %I:%M %p"
                ).replace(tzinfo=LOCAL_TZ)
            except (ValueError, KeyError):
                continue
            readings[local.astimezone(UTC)] = {
                "day": day,
                "temp": num(r.get("Temperature")),
                "rh": num(r.get("Humidity")),
                "accum": num(r.get("Precip. Accum.")),
                "wind": num(r.get("Speed")),     # km/h
                "solar": num(r.get("Solar")),    # W/m2
            }
    return readings


def hourly_observations(readings):
    """Return {UTC hour: dict(temp, rh, wind, solar, rain)}."""
    acc = defaultdict(lambda: defaultdict(list))
    rain = defaultdict(float)
    prev_accum = prev_day = None
    for t in sorted(readings):
        r = readings[t]
        # readings are averaged around each full hour (+-30 min)
        near = (t + timedelta(minutes=30)).replace(minute=0, second=0, microsecond=0)
        for k in ("temp", "rh", "wind", "solar"):
            if r[k] is not None:
                acc[near][k].append(r[k])
        # rain: increase of the daily accumulation, credited to the hour it ends in
        if r["accum"] is not None:
            if prev_accum is not None and prev_day == r["day"]:
                inc = max(0.0, r["accum"] - prev_accum)
            else:
                inc = r["accum"]  # first reading of a day (accumulation restarts)
            end = t.replace(minute=0, second=0, microsecond=0)
            if t > end:
                end += timedelta(hours=1)
            rain[end] += inc
            prev_accum, prev_day = r["accum"], r["day"]
    out = {}
    for h, d in acc.items():
        if len(d["temp"]) < 2:
            continue  # too few readings for a reliable hour
        o = {k: (sum(d[k]) / len(d[k]) if d[k] else None)
             for k in ("temp", "rh", "wind", "solar")}
        o["rain"] = rain.get(h, 0.0)
        out[h] = o
    return out


# ---- Reference evapotranspiration (FAO-56 Penman-Monteith, daily) -------------
def sat_vp(t):
    return 0.6108 * math.exp(17.27 * t / (t + 237.3))


def extraterrestrial(doy, lat_deg):
    phi = math.radians(lat_deg)
    dr = 1 + 0.033 * math.cos(2 * math.pi * doy / 365)
    delta = 0.409 * math.sin(2 * math.pi * doy / 365 - 1.39)
    ws = math.acos(max(-1.0, min(1.0, -math.tan(phi) * math.tan(delta))))
    return (24 * 60 / math.pi) * 0.0820 * dr * (
        ws * math.sin(phi) * math.sin(delta)
        + math.cos(phi) * math.cos(delta) * math.sin(ws))


def et0_daily(tmean, tmax, tmin, rh_mean, u2, rs_mj, doy,
              lat=LATITUDE, elev=ELEVATION_M, ea=None):
    """mm/day. u2 = wind speed at 2 m in m/s, rs_mj = solar radiation in MJ/m2/day."""
    es = (sat_vp(tmax) + sat_vp(tmin)) / 2
    if ea is None:
        ea = rh_mean / 100 * es
    ra = extraterrestrial(doy, lat)
    rso = (0.75 + 2e-5 * elev) * ra
    ratio = min(1.0, rs_mj / rso) if rso > 0 else 0.5
    rnl = 4.903e-9 * (((tmax + 273.16) ** 4 + (tmin + 273.16) ** 4) / 2) * (
        0.34 - 0.14 * math.sqrt(ea)) * (1.35 * ratio - 0.35)
    rn = 0.77 * rs_mj - rnl
    p = 101.3 * ((293 - 0.0065 * elev) / 293) ** 5.26
    gamma = 0.000665 * p
    delta = 4098 * sat_vp(tmean) / (tmean + 237.3) ** 2
    top = 0.408 * delta * rn + gamma * 900 / (tmean + 273) * u2 * (es - ea)
    return max(0.0, top / (delta + gamma * (1 + 0.34 * u2)))


def observed_et0(obs):
    """Daily ET0 from the station, for days with (almost) every hour present.
    The station wind is treated as a 2 m wind speed."""
    by_day = defaultdict(list)
    for h, o in obs.items():
        if None in (o["temp"], o["rh"], o["wind"], o["solar"]):
            continue
        by_day[day_of(h)].append(o)
    out = {}
    for day, g in by_day.items():
        if len(g) < MIN_HOURS_PER_DAY:
            continue
        temps = [o["temp"] for o in g]
        n = len(g)
        out[day] = et0_daily(
            sum(temps) / n, max(temps), min(temps),
            sum(o["rh"] for o in g) / n,
            sum(o["wind"] for o in g) / n / 3.6,
            sum(o["solar"] for o in g) / n * 0.0864,
            date.fromisoformat(day).timetuple().tm_yday)
    return out


# ---- Forecasts -----------------------------------------------------------
def load_forecasts(path):
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            try:
                issued = datetime.strptime(r["issued_utc"], "%Y-%m-%dT%H:%MZ").replace(tzinfo=UTC)
                target = datetime.strptime(r["target_time_utc"], "%Y-%m-%dT%H:%M").replace(tzinfo=UTC)
            except (ValueError, KeyError):
                continue
            yield issued, r["model"], target, r


def build_pairs(forecast_file, obs):
    """One forecast per (model, target hour, lead bucket): the freshest in the bucket."""
    best = {}
    for issued, model, target, r in load_forecasts(forecast_file):
        lead = (target - issued).total_seconds() / 3600
        if lead < 0 or target not in obs:
            continue  # hours already past at issue time, or without station data
        key = (model, target, bucket(lead))
        if key in best and best[key][0] <= lead:
            continue
        best[key] = (lead, issued, r)
    pairs = []
    for (model, target, b), (lead, issued, r) in best.items():
        o = obs[target]
        pairs.append({
            "model": model, "bucket": b, "lead_h": round(lead),
            "issued_utc": issued.strftime("%Y-%m-%dT%H:%MZ"),
            "target_utc": target.strftime("%Y-%m-%dT%H:%MZ"),
            "day": day_of(target),
            "fc_temp": num(r.get("temperature_2m")), "obs_temp": o["temp"],
            "fc_rh": num(r.get("relative_humidity_2m")), "obs_rh": o["rh"],
            "fc_rain_mm": num(r.get("precipitation")), "obs_rain_mm": round(o["rain"], 2),
            "fc_et0_mm": num(r.get("et0_fao_evapotranspiration")),
        })
    pairs.sort(key=lambda p: (p["model"], BUCKET_ORDER.index(p["bucket"]), p["target_utc"]))
    return pairs


def write_csv(path, rows, fields=None):
    if not rows:
        return
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fields or list(rows[0]))
        w.writeheader()
        w.writerows(rows)


def group_key(item):
    return (item[0][0], BUCKET_ORDER.index(item[0][1]))


def mean(xs):
    return sum(xs) / len(xs)


# ---- Summaries -----------------------------------------------------------
def summarize_hourly(pairs):
    groups = defaultdict(list)
    for p in pairs:
        groups[(p["model"], p["bucket"])].append(p)
    print("HOURLY (each target hour counted once)")
    print(f"{'model':<15}{'lead':<10}{'hours':>6}{'T bias':>8}{'T MAE':>7}{'RH MAE':>8}"
          f"{'rain hit/miss/false':>21}{'fc mm':>8}{'obs mm':>8}")
    for (model, b), g in sorted(groups.items(), key=group_key):
        t = [p["fc_temp"] - p["obs_temp"] for p in g
             if p["fc_temp"] is not None and p["obs_temp"] is not None]
        rh = [abs(p["fc_rh"] - p["obs_rh"]) for p in g
              if p["fc_rh"] is not None and p["obs_rh"] is not None]
        hit = miss = false = 0
        fc_mm = obs_mm = 0.0
        for p in g:
            fr = p["fc_rain_mm"]
            if fr is None:
                continue
            fc_mm += fr
            obs_mm += p["obs_rain_mm"]
            f_rain, o_rain = fr >= RAIN_THRESHOLD_MM, p["obs_rain_mm"] >= RAIN_THRESHOLD_MM
            hit += f_rain and o_rain
            miss += (not f_rain) and o_rain
            false += f_rain and (not o_rain)
        print(f"{model:<15}{b:<10}{len(g):>6}"
              f"{(f'{mean(t):+.1f}' if t else '-'):>8}"
              f"{(f'{mean([abs(x) for x in t]):.1f}' if t else '-'):>7}"
              f"{(f'{mean(rh):.0f}' if rh else '-'):>8}"
              f"{f'{hit}/{miss}/{false}':>21}{fc_mm:>8.1f}{obs_mm:>8.1f}")
    print("T bias = forecast minus observed (C). Rain counts: hours >= "
          f"{RAIN_THRESHOLD_MM} mm. Lead = hours between forecast issue and target.\n")


def build_daily(pairs, obs_et0):
    days = defaultdict(list)
    for p in pairs:
        days[(p["model"], p["bucket"], p["day"])].append(p)
    daily = []
    for (model, b, day), g in sorted(days.items(), key=lambda kv: (kv[0][0], BUCKET_ORDER.index(kv[0][1]), kv[0][2])):
        if len(g) < MIN_HOURS_PER_DAY:
            continue
        fc_rain = [p["fc_rain_mm"] for p in g]
        fc_et0 = [p["fc_et0_mm"] for p in g]
        daily.append({
            "model": model, "bucket": b, "day": day, "hours": len(g),
            "fc_rain_mm": None if None in fc_rain else round(sum(fc_rain), 2),
            "obs_rain_mm": round(sum(p["obs_rain_mm"] for p in g), 2),
            "fc_et0_mm": None if None in fc_et0 else round(sum(fc_et0), 2),
            "obs_et0_mm": None if day not in obs_et0 else round(obs_et0[day], 2),
        })
    return daily


def summarize_daily(daily):
    rain, et = defaultdict(list), defaultdict(list)
    for d in daily:
        key = (d["model"], d["bucket"])
        if d["fc_rain_mm"] is not None:
            rain[key].append(d)
        if d["fc_et0_mm"] is not None and d["obs_et0_mm"] is not None:
            et[key].append(d)

    print(f"DAILY RAIN (days with >= {MIN_HOURS_PER_DAY} matched hours; wet day = >= {WET_DAY_MM} mm)")
    if not rain:
        print("  no complete days yet - export full daily files (all 96 readings)\n")
    else:
        print(f"{'model':<15}{'lead':<10}{'days':>5}{'MAE mm':>8}{'fc mm':>8}{'obs mm':>8}"
              f"{'wet hit/miss/false':>20}")
        for (model, b), g in sorted(rain.items(), key=group_key):
            hit = miss = false = 0
            for d in g:
                f_wet, o_wet = d["fc_rain_mm"] >= WET_DAY_MM, d["obs_rain_mm"] >= WET_DAY_MM
                hit += f_wet and o_wet
                miss += (not f_wet) and o_wet
                false += f_wet and (not o_wet)
            print(f"{model:<15}{b:<10}{len(g):>5}"
                  f"{mean([abs(d['fc_rain_mm'] - d['obs_rain_mm']) for d in g]):>8.2f}"
                  f"{sum(d['fc_rain_mm'] for d in g):>8.1f}{sum(d['obs_rain_mm'] for d in g):>8.1f}"
                  f"{f'{hit}/{miss}/{false}':>20}")
        print()

    print("DAILY ET0 (forecast vs ET0 calculated from the station; mm/day)")
    if not et:
        print("  no days with both a forecast ET0 and a complete station day yet\n")
    else:
        print(f"{'model':<15}{'lead':<10}{'days':>5}{'bias':>8}{'MAE':>7}{'obs mean':>10}")
        for (model, b), g in sorted(et.items(), key=group_key):
            diffs = [d["fc_et0_mm"] - d["obs_et0_mm"] for d in g]
            print(f"{model:<15}{b:<10}{len(g):>5}{mean(diffs):>+8.2f}"
                  f"{mean([abs(x) for x in diffs]):>7.2f}{mean([d['obs_et0_mm'] for d in g]):>10.2f}")
        print("Station ET0 assumes the station wind is a 2 m wind; treat small biases with caution.\n")


def main():
    args = sys.argv[1:]
    forecast_file = args[0] if args else "forecast_log.csv"
    station_files = args[1:] or sorted(glob.glob("weather-history-*.csv"))

    obs = hourly_observations(load_station(station_files))
    if not obs:
        sys.exit("No usable station data found.")
    obs_days = sorted({day_of(h) for h in obs})
    print(f"Station data: {len(obs)} hours on {len(obs_days)} days "
          f"({obs_days[0]} to {obs_days[-1]})\n")

    write_csv("station_hourly.csv", [
        {"utc_hour": h.strftime("%Y-%m-%dT%H:%MZ"),
         "local_time": h.astimezone(LOCAL_TZ).strftime("%Y-%m-%d %H:%M"),
         "temp_c": None if o["temp"] is None else round(o["temp"], 2),
         "rh_pct": None if o["rh"] is None else round(o["rh"], 1),
         "rain_mm": round(o["rain"], 2),
         "wind_kmh": None if o["wind"] is None else round(o["wind"], 1),
         "solar_wm2": None if o["solar"] is None else round(o["solar"], 1)}
        for h, o in sorted(obs.items())])

    pairs = build_pairs(forecast_file, obs)
    if not pairs:
        sys.exit("No forecast hours overlap the station data yet.")
    write_csv("comparison.csv", pairs)

    summarize_hourly(pairs)
    daily = build_daily(pairs, observed_et0(obs))
    write_csv("daily.csv", daily)
    summarize_daily(daily)
    print("Written: station_hourly.csv, comparison.csv, daily.csv")


if __name__ == "__main__":
    main()

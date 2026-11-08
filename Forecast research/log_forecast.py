"""Log weather forecasts from several models so you can compare them with
real measurements later. Uses only the Python standard library.

Each run appends rows to forecast_log.csv:
  issued_utc, model, target_time, variable columns...
"""
import csv
import json
import os
import urllib.parse
import urllib.request
from datetime import datetime, timezone

# --- Edit these ---------------------------------------------------------
LATITUDE = 47.5302    # your site
LONGITUDE = 19.1833
MODELS = ["icon_seamless", "ecmwf_ifs025", "gfs_seamless"]
VARIABLES = [
    "temperature_2m",
    "relative_humidity_2m",
    "precipitation",
    "precipitation_probability",   # not available for every model; blank if missing
    "et0_fao_evapotranspiration",
    "wind_speed_10m",
]
FORECAST_DAYS = 3
OUTFILE = "forecast_log.csv"
# met.no requires a User-Agent that identifies your app and gives a contact.
METNO_USER_AGENT = "my-irrigation-logger/1.0 tritenjennie@gmail.com"
# ------------------------------------------------------------------------


def fetch():
    params = {
        "latitude": LATITUDE,
        "longitude": LONGITUDE,
        "hourly": ",".join(VARIABLES),
        "models": ",".join(MODELS),
        "forecast_days": FORECAST_DAYS,
        "timezone": "UTC",
    }
    url = "https://api.open-meteo.com/v1/forecast?" + urllib.parse.urlencode(params)
    with urllib.request.urlopen(url, timeout=60) as resp:
        return json.load(resp)


def metno_rows(issued):
    """met.no forecast, converted to the same layout as the Open-Meteo rows.

    met.no gives rain for the hour *starting* at each timestamp, while
    Open-Meteo gives rain for the hour *ending* at it, so rain is taken from
    the previous entry. Only the ~60 hours with hourly data are logged.
    """
    url = (
        "https://api.met.no/weatherapi/locationforecast/2.0/compact"
        f"?lat={LATITUDE:.4f}&lon={LONGITUDE:.4f}"
    )
    req = urllib.request.Request(url, headers={"User-Agent": METNO_USER_AGENT})
    with urllib.request.urlopen(req, timeout=60) as resp:
        series = json.load(resp)["properties"]["timeseries"]

    rows = []
    for i in range(1, len(series)):
        prev_hour = series[i - 1]["data"].get("next_1_hours")
        if prev_hour is None:
            continue
        d = series[i]["data"]["instant"]["details"]
        wind = d.get("wind_speed")
        values = {
            "temperature_2m": d.get("air_temperature"),
            "relative_humidity_2m": d.get("relative_humidity"),
            "precipitation": prev_hour["details"].get("precipitation_amount"),
            "wind_speed_10m": None if wind is None else round(wind * 3.6, 1),  # m/s -> km/h
        }
        row = ["" if values.get(v) is None else values[v] for v in VARIABLES]
        rows.append([issued, "metno", series[i]["time"][:16]] + row)
    return rows


def main():
    data = fetch()
    hourly = data["hourly"]
    times = hourly["time"]
    issued = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%MZ")

    new_file = not os.path.exists(OUTFILE)
    with open(OUTFILE, "a", newline="") as f:
        w = csv.writer(f)
        if new_file:
            w.writerow(["issued_utc", "model", "target_time_utc"] + VARIABLES)
        for model in MODELS:
            for i, t in enumerate(times):
                row = []
                for var in VARIABLES:
                    series = hourly.get(f"{var}_{model}")
                    row.append("" if series is None else series[i])
                # skip rows where the model gave nothing at all
                if all(v == "" or v is None for v in row):
                    continue
                w.writerow([issued, model, t] + row)
        try:
            for row in metno_rows(issued):
                w.writerow(row)
        except Exception as exc:  # keep the Open-Meteo data even if met.no fails
            print("met.no failed:", exc)


if __name__ == "__main__":
    main()

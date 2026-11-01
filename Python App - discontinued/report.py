"""Turns the text block the ESP32 prints for the PRINT command into the sections of the Monitoring screen.

The format is fixed by PRINT_DATA() in the firmware, so every line is matched exactly instead of guessed:

    Time: 2026-10-05 17:12:29 (source: NTP, last NTP: 2026-10-05 17:00:25)
    WiFi: online, SSID MyNetwork
    --- Sensors
    Soil 1: raw 1234, 55.0 %
    BME280: 21.40 C, 52.0 % RH, 1013.2 hPa
    Water level: 34.2 cm, 62 %, low=0
    --- Current weather
    Fetched: 2026-10-05 17:00:27 (fresh)
    Temp 21.0 C, hum 50 %, pressure 1010 hPa, wind 2.0 m/s, clouds 20 %, rain(1h) 0.00 mm
    --- Forecast
    Fetched: 2026-10-05 17:00:26 (usable for today)
    20261005: min 15.2 C, max 21.3 C, pop 0 %, rain 0.00 mm
    --- Reliability: sensors=1 current weather=1 forecast=1
    --- Zones: 1=ON 2=off 3=off 4=off

Lines that do not match anything above are ignored, so a new debug line in the firmware can never break the screen.
"""
import re
from dataclasses import dataclass, field
from datetime import date, datetime
from typing import List, Optional, Tuple

_N = r"(-?\d+(?:\.\d+)?)"
_TIME = re.compile(r"^Time:\s*(.+?)\s*\(source:\s*(.*?),\s*last NTP:\s*(.*)\)\s*$")
_WIFI = re.compile(r"^WiFi:\s*(.*?)(?:,\s*SSID\s+(.*))?$")
_HEADING = re.compile(r"^---\s*([A-Za-z][A-Za-z ]*?)\s*$")
_SOIL = re.compile(rf"^Soil (\d):\s*raw\s*-?\d+,\s*{_N}\s*%")
_BME = re.compile(rf"^BME280:\s*{_N} C,\s*{_N} % RH,\s*{_N} hPa")
_WATER = re.compile(rf"^Water level:\s*{_N} cm,\s*{_N} %,\s*low=(\d)")
_FETCHED = re.compile(r"^Fetched:\s*(.+?)\s*\((.*)\)\s*$")
_CURRENT = re.compile(
    rf"^Temp {_N} C,\s*hum {_N} %,\s*pressure {_N} hPa,\s*wind {_N} m/s,\s*clouds {_N} %,\s*rain\(1h\) {_N} mm")
_DAY = re.compile(rf"^(\d{{8}}):\s*min {_N} C,\s*max {_N} C,\s*pop {_N} %,\s*rain {_N} mm")
_RELIABILITY = re.compile(r"^--- Reliability:\s*sensors=(\d)\s+current weather=(\d)\s+forecast=(\d)")
_ZONES = re.compile(r"^--- Zones:(.*)$")
_ZONE_STATE = re.compile(r"(\d)=(ON|off)")


@dataclass
class Row:
    label: str
    value: str = ""
    kind: str = "plain"     # plain | bar | hero | status | day
    caption: str = ""       # hero: the date under the clock
    percent: Optional[float] = None   # bar: fill level; day: chance of rain
    active: bool = False    # status: shows a green tick (True) or a red cross (False)
    icon: str = ""          # day: sun | cloud | rain
    low: float = 0.0        # day: temperatures
    high: float = 0.0
    scale: Tuple[float, float] = (0.0, 1.0)   # day: coldest low and warmest high of the whole forecast
    marker: Optional[float] = None            # day: current temperature (today only)


@dataclass
class Section:
    title: str
    rows: List[Row] = field(default_factory=list)

    def signature(self):
        """Changes only when rows appear or disappear, not when a value changes."""
        return self.title, tuple((r.kind, r.label) for r in self.rows)


@dataclass
class Day:
    key: str
    low: float
    high: float
    pop: float
    rain: float


@dataclass
class Report:
    clock: str = ""
    stamp: str = ""
    last_ntp: str = ""
    wifi_state: str = ""
    ssid: Optional[str] = None
    soil: dict = field(default_factory=dict)
    bme: Optional[Tuple[str, str, str]] = None
    water: Optional[Tuple[str, str, bool]] = None   # distance in cm, level in %, below the minimum (LED on)
    cur_fetched: str = ""
    cur: Optional[Tuple[str, ...]] = None
    days: List[Day] = field(default_factory=list)
    reliability: Optional[Tuple[bool, bool, bool]] = None
    zones: dict = field(default_factory=dict)


# ----- reading -----------------------------------------------------------------------------------------------------

def parse(text: str) -> Report:
    rep, heading = Report(), ""
    for raw in text.splitlines():
        line = raw.strip()
        m = _TIME.match(line)
        if m:
            rep.stamp, rep.last_ntp = m.group(1), m.group(3)
            rep.clock = m.group(1)
            continue
        m = _WIFI.match(line)
        if m:
            rep.wifi_state, rep.ssid = m.group(1).strip(), m.group(2)
            continue
        m = _RELIABILITY.match(line)
        if m:
            rep.reliability = tuple(g == "1" for g in m.groups())
            continue
        m = _ZONES.match(line)
        if m:
            rep.zones = {int(n): s == "ON" for n, s in _ZONE_STATE.findall(m.group(1))}
            continue
        m = _HEADING.match(line)
        if m:
            heading = m.group(1).lower()
            continue
        m = _SOIL.match(line)
        if m:
            rep.soil[int(m.group(1))] = m.group(2)
            continue
        m = _BME.match(line)
        if m:
            rep.bme = m.groups()
            continue
        m = _WATER.match(line)
        if m:
            rep.water = (m.group(1), m.group(2), m.group(3) == "1")
            continue
        m = _FETCHED.match(line)
        if m and heading.startswith("current"):
            rep.cur_fetched = m.group(1)
            continue
        m = _CURRENT.match(line)
        if m:
            rep.cur = m.groups()
            continue
        m = _DAY.match(line)
        if m:
            key, lo, hi, pop, rain = m.groups()
            rep.days.append(Day(key, float(lo), float(hi), float(pop), float(rain)))
    return rep


# ----- sections ----------------------------------------------------------------------------------------------------

def _stamp(text: str) -> Optional[datetime]:
    try:
        return datetime.strptime(text, "%Y-%m-%d %H:%M:%S")
    except ValueError:
        return None


def _today(rep: Report) -> date:
    t = _stamp(rep.stamp)
    return t.date() if t else date.today()


def _time_rows(rep: Report) -> List[Row]:
    t = _stamp(rep.stamp)
    hero = Row("", t.strftime("%H:%M:%S") if t else "--:--:--", "hero",
               caption=t.strftime("%A, %d %B %Y") if t else "The clock is not set")
    ntp = rep.last_ntp if _stamp(rep.last_ntp) else "Never"
    return [hero, Row("Last NTP sync", ntp)]


def _pct(label: str, number: str) -> Row:
    value = float(number)
    return Row(label, f"{value:g}%", "bar", percent=max(0.0, min(100.0, value)))


def _sensor_rows(rep: Report) -> List[Row]:
    rows = [_pct(f"Zone {i} soil humidity", rep.soil[i]) for i in sorted(rep.soil)]
    if rep.bme:
        temp, hum, press = rep.bme
        rows += [Row("Temperature", f"{temp}\u00b0C"), _pct("Humidity", hum), Row("Pressure", f"{press} hPa")]
    else:
        rows.append(Row("Air sensor", "Not available"))
    if rep.water:
        distance, percent, low = rep.water
        rows += [_pct("Water level", percent), Row("Distance to water", f"{distance} cm"),
                 Row("Water supply", "Low" if low else "OK", "status", active=not low)]
    else:
        rows.append(Row("Water sensor", "Not available"))
    return rows


def _weather_rows(rep: Report) -> List[Row]:
    if not rep.cur:
        return [Row("Weather data", "Not available")]
    temp, hum, press, wind, clouds, rain = rep.cur
    fetched = _stamp(rep.cur_fetched)
    if fetched is None:
        updated = "Never"
    elif fetched.date() == _today(rep):
        updated = fetched.strftime("%H:%M:%S")
    else:
        updated = rep.cur_fetched
    return [Row("Last updated", updated), Row("Temperature", f"{temp}\u00b0C"), _pct("Humidity", hum),
            Row("Pressure", f"{press} hPa"), Row("Wind", f"{wind} m/s"), _pct("Cloud cover", clouds),
            Row("Rain, last hour", f"{rain} mm")]


def _icon(day: Day) -> str:
    if day.pop >= 50 or day.rain >= 1.0:
        return "rain"
    if day.pop >= 20 or day.rain > 0:
        return "cloud"
    return "sun"


def _day_label(key: str, today: date) -> str:
    try:
        d = datetime.strptime(key, "%Y%m%d").date()
    except ValueError:
        return key
    return "Today" if d == today else d.strftime("%a")


def _forecast_rows(rep: Report) -> List[Row]:
    if not rep.days:
        return [Row("Forecast data", "Not available")]
    scale = (min(d.low for d in rep.days), max(d.high for d in rep.days))
    today = _today(rep)
    now = float(rep.cur[0]) if rep.cur else None
    rows = []
    for d in rep.days:
        label = _day_label(d.key, today)
        rows.append(Row(label, "", "day", percent=d.pop, icon=_icon(d), low=d.low, high=d.high, scale=scale,
                        marker=now if label == "Today" else None))
    return rows


def _other_rows(rep: Report) -> List[Row]:
    rows = []
    if rep.reliability:
        for name, ok in zip(("Sensor", "Weather", "Forecast"), rep.reliability):
            rows.append(Row(f"{name} reliability", kind="status", active=ok))
    online = rep.wifi_state.startswith("online") and bool(rep.ssid)
    rows.append(Row("WiFi connection", rep.ssid if online else "", "status", active=online))
    return rows


def _zone_rows(rep: Report) -> List[Row]:
    return [Row(f"Zone {n}", kind="status", active=rep.zones[n]) for n in sorted(rep.zones)]


def build(text: str) -> List[Section]:
    rep = parse(text)
    sections = [
        Section("Time", _time_rows(rep)),
        Section("Sensors", _sensor_rows(rep)),
        Section("Weather", _weather_rows(rep)),
        Section("Forecast", _forecast_rows(rep)),
        Section("Other", _other_rows(rep)),
        Section("Zones", _zone_rows(rep)),
    ]
    return [s for s in sections if s.rows]
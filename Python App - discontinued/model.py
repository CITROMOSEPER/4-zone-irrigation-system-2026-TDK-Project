"""Timetable and device settings data model, and the text formats shared with the ESP32 firmware.

Event line format (21 comma separated fields):
    days,start,end,zones,reliable,c0,...,c15
    days     7 chars of 0/1, Monday first
    start    HHMM          end  HHMM (0000 means 24:00 when it follows a later start)
    zones    4 chars of 0/1
    reliable 0/1
    cN       '*' (ignored) or 'min:max'
"""
import re
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

MAX_EVENTS = 24
DAY_NAMES = ["Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"]

# Same order as the firmware (TT PARAMS)
PARAMS = [
    ("SOIL1", "%"), ("SOIL2", "%"), ("SOIL3", "%"), ("SOIL4", "%"),
    ("TEMP", "C"), ("AIR_HUM", "%"), ("PRESSURE", "hPa"),
    ("W_TEMP", "C"), ("W_HUM", "%"), ("W_PRESSURE", "hPa"),
    ("W_WIND", "m/s"), ("W_CLOUDS", "%"), ("W_RAIN1H", "mm"),
    ("F_POP", "%"), ("F_RAIN", "mm"), ("F_TMAX", "C"),
]

Condition = Optional[Tuple[float, float]]


def fmt_clock(minutes: int) -> str:
    return f"{minutes // 60:02d}:{minutes % 60:02d}"


def parse_clock(text: str) -> int:
    text = text.strip()
    try:
        if ":" in text:
            h, m = (int(p) for p in text.split(":"))
        elif len(text) == 4:
            h, m = int(text[:2]), int(text[2:])
        else:
            raise ValueError
    except ValueError:
        raise ValueError(f"Invalid time '{text}' (use HH:MM)") from None
    if not (0 <= m < 60 and 0 <= h <= 24) or (h == 24 and m != 0):
        raise ValueError(f"Invalid time '{text}' (use HH:MM)")
    return h * 60 + m


def fmt_num(v: float) -> str:
    s = f"{v:.2f}".rstrip("0").rstrip(".")
    return "0" if s in ("", "-0") else s


@dataclass
class Event:
    days: List[bool] = field(default_factory=lambda: [False] * 7)
    start: int = 0       # minutes since midnight, 0..1439
    end: int = 60        # minutes, 1..1440 (smaller than start = crosses midnight)
    zones: List[bool] = field(default_factory=lambda: [True, False, False, False])
    reliable: bool = False
    conds: List[Condition] = field(default_factory=lambda: [None] * len(PARAMS))

    @property
    def wrapped(self) -> bool:
        return self.end < self.start

    def copy(self) -> "Event":
        return Event(list(self.days), self.start, self.end, list(self.zones), self.reliable, list(self.conds))

    def validate(self) -> None:
        if not any(self.days):
            raise ValueError("Select at least one day")
        if not any(self.zones):
            raise ValueError("Select at least one zone")
        if not (0 <= self.start < 1440 and 1 <= self.end <= 1440):
            raise ValueError("Time out of range")
        if self.start == self.end % 1440:
            raise ValueError("Start and end are the same time (24 h events are not supported by the firmware)")
        for (name, _), c in zip(PARAMS, self.conds):
            if c is not None and c[0] > c[1]:
                raise ValueError(f"{name}: min is larger than max")

    def to_line(self) -> str:
        parts = [
            "".join("1" if d else "0" for d in self.days),
            f"{self.start // 60:02d}{self.start % 60:02d}",
            f"{(self.end % 1440) // 60:02d}{self.end % 60:02d}",
            "".join("1" if z else "0" for z in self.zones),
            "1" if self.reliable else "0",
        ]
        parts += ["*" if c is None else f"{fmt_num(c[0])}:{fmt_num(c[1])}" for c in self.conds]
        return ",".join(parts)

    @classmethod
    def from_line(cls, line: str) -> "Event":
        f = [p.strip() for p in line.split(",")]
        if len(f) != 5 + len(PARAMS):
            raise ValueError(f"Expected {5 + len(PARAMS)} fields, found {len(f)}")
        if len(f[0]) != 7 or set(f[0]) - set("01"):
            raise ValueError("Bad days field")
        if len(f[3]) != 4 or set(f[3]) - set("01"):
            raise ValueError("Bad zones field")
        if f[4] not in ("0", "1"):
            raise ValueError("Bad reliable flag")
        conds: List[Condition] = []
        for name, tok in zip((p[0] for p in PARAMS), f[5:]):
            if tok == "*":
                conds.append(None)
                continue
            try:
                lo, hi = tok.split(":")
                conds.append((float(lo), float(hi)))
            except ValueError:
                raise ValueError(f"Bad condition for {name}: '{tok}'") from None
        end = parse_clock(f[2])
        ev = cls(
            days=[c == "1" for c in f[0]],
            start=parse_clock(f[1]),
            end=end if end != 0 else 1440,
            zones=[c == "1" for c in f[3]],
            reliable=f[4] == "1",
            conds=conds,
        )
        ev.validate()
        return ev


# ----- Device settings (API key, location, time zone, water tank, beeper) -----------------------------------------------------

# Common POSIX TZ strings, offered as presets in the settings dialog
TIMEZONE_PRESETS = [
    ("Central Europe (Budapest, Berlin, Paris)", "CET-1CEST,M3.5.0,M10.5.0/3"),
    ("Western Europe (London, Dublin)", "GMT0BST,M3.5.0/1,M10.5.0"),
    ("Western Europe (Lisbon)", "WET0WEST,M3.5.0/1,M10.5.0"),
    ("Eastern Europe (Athens, Helsinki, Kyiv)", "EET-2EEST,M3.5.0/3,M10.5.0/4"),
    ("UTC", "UTC0"),
    ("US Eastern", "EST5EDT,M3.2.0,M11.1.0"),
    ("US Central", "CST6CDT,M3.2.0,M11.1.0"),
    ("US Mountain", "MST7MDT,M3.2.0,M11.1.0"),
    ("US Pacific", "PST8PDT,M3.2.0,M11.1.0"),
]

_API_KEY_RE = re.compile(r"^[A-Za-z0-9_-]{1,64}$")

# Water tank. The HC-SR04+ sits on top of the tank and points down, so the HIGH level is the SHORT distance.
TANK_MIN_CM = 2.0        # range of the sensor
TANK_MAX_CM = 400.0
TANK_MIN_SPAN_CM = 5.0   # the high and the low level must be at least this far apart
DEFAULT_TANK_HIGH_CM = 10.0
DEFAULT_TANK_LOW_CM = 90.0


def check_tank(high: float, low: float) -> Optional[Tuple[Tuple[str, ...], str]]:
    """None when the two distances are fine, otherwise (fields at fault, message)."""
    for key, name, value in (("high", "High level", high), ("low", "Low level", low)):
        if not TANK_MIN_CM <= value <= TANK_MAX_CM:
            return (key,), (f"{name} must be between {fmt_num(TANK_MIN_CM)} and {fmt_num(TANK_MAX_CM)} cm, "
                            f"the range of the sensor")
    if low - high < TANK_MIN_SPAN_CM:
        return ("high", "low"), (f"The high level must be at least {fmt_num(TANK_MIN_SPAN_CM)} cm less than the low "
                                 f"level (a full tank is the shorter distance)")
    return None


def fmt_coord(v: float) -> str:
    s = f"{v:.6f}".rstrip("0").rstrip(".")
    return "0" if s in ("", "-0") else s


@dataclass
class DeviceConfig:
    api_key: str = ""
    lat: float = 0.0
    lon: float = 0.0
    timezone: str = ""
    tank_high: float = DEFAULT_TANK_HIGH_CM   # sensor to water when the tank is full, cm
    tank_low: float = DEFAULT_TANK_LOW_CM     # sensor to water when the tank is empty, cm
    beeper: bool = True                       # False = the buzzer never makes a sound

    def validate(self) -> None:
        if not _API_KEY_RE.match(self.api_key):
            raise ValueError("API key must be 1-64 characters (letters, digits, - or _)")
        if not -90.0 <= self.lat <= 90.0:
            raise ValueError("Latitude must be between -90 and 90")
        if not -180.0 <= self.lon <= 180.0:
            raise ValueError("Longitude must be between -180 and 180")
        tz = self.timezone
        if not 3 <= len(tz) <= 63:
            raise ValueError("Time zone must be 3-63 characters")
        if not (tz[0].isalpha() or tz[0] == "<") or any(not 0x21 <= ord(c) <= 0x7E or c == ";" for c in tz):
            raise ValueError("Time zone must be a POSIX TZ string without spaces, e.g. CET-1CEST,M3.5.0,M10.5.0/3")
        problem = check_tank(self.tank_high, self.tank_low)
        if problem:
            raise ValueError(problem[1])

    def to_payload(self) -> str:
        """One line for the ESP32: 'CFG SET key;lat;lon;timezone;high_cm;low_cm;beeper'"""
        self.validate()
        return (f"CFG SET {self.api_key};{fmt_coord(self.lat)};{fmt_coord(self.lon)};{self.timezone};"
                f"{fmt_num(self.tank_high)};{fmt_num(self.tank_low)};{'1' if self.beeper else '0'}")

    @classmethod
    def from_payload(cls, text: str) -> "DeviceConfig":
        text = text.strip()
        if text.upper().startswith("CFG SET"):
            text = text[7:].strip()
        parts = text.split(";")
        if len(parts) not in (4, 6, 7):   # older firmware sends only the first four or six fields
            raise ValueError(f"Expected 7 fields, found {len(parts)}")
        try:
            lat, lon = float(parts[1]), float(parts[2])
        except ValueError:
            raise ValueError("Latitude and longitude must be numbers") from None
        cfg = cls(api_key=parts[0].strip(), lat=lat, lon=lon, timezone=parts[3].strip())
        if len(parts) >= 6:
            try:
                cfg.tank_high, cfg.tank_low = float(parts[4]), float(parts[5])
            except ValueError:
                raise ValueError("The water levels must be numbers") from None
        if len(parts) == 7:
            if parts[6].strip() not in ("0", "1"):
                raise ValueError("The beeper flag must be 0 or 1")
            cfg.beeper = parts[6].strip() == "1"
        return cfg


def build_payload(events: List[Event]) -> str:
    """One line for the ESP32: 'TT SET n;event;event...'"""
    if len(events) > MAX_EVENTS:
        raise ValueError(f"The firmware stores at most {MAX_EVENTS} events")
    for e in events:
        e.validate()
    return f"TT SET {len(events)}" + "".join(";" + e.to_line() for e in events)


def parse_payload(text: str) -> List[Event]:
    text = text.strip()
    if text.upper().startswith("TT SET"):
        text = text[6:].strip()
    parts = text.split(";")
    n = int(parts[0])
    events = [Event.from_line(p) for p in parts[1:] if p.strip()]
    if len(events) != n:
        raise ValueError(f"Header says {n} events, found {len(events)}")
    return events


def save_file(path: str, events: List[Event]) -> None:
    with open(path, "w", encoding="utf-8") as fh:
        fh.write("# Irrigation TimeTable v1\n")
        fh.write("# days,start,end,zones,reliable," + ",".join(p[0] for p in PARAMS) + "\n")
        fh.write("# one event per line, '*' = ignored, 'min:max' = allowed range\n")
        for e in events:
            fh.write(e.to_line() + "\n")


def load_file(path: str) -> List[Event]:
    events = []
    with open(path, "r", encoding="utf-8") as fh:
        for no, raw in enumerate(fh, 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            try:
                events.append(Event.from_line(line))
            except ValueError as exc:
                raise ValueError(f"Line {no}: {exc}") from None
    if len(events) > MAX_EVENTS:
        raise ValueError(f"File has {len(events)} events, the firmware stores at most {MAX_EVENTS}")
    return events
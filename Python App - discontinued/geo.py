"""Coordinates --> POSIX TZ string (the format the ESP32 firmware wants).

Exact zones come from two optional packages (pip install timezonefinder tzdata):
  * timezonefinder maps a latitude / longitude to an IANA name such as "Europe/Budapest"
  * tzdata (or the operating system) holds the rules; the POSIX string is the footer of the zone's TZif file
Without them the app still works: the zone is then estimated from the longitude (whole hours, no daylight saving).
"""
import os
import threading
import zoneinfo
from dataclasses import dataclass
from typing import Optional

_finder = None
_finder_failed = False
_lock = threading.Lock()


@dataclass
class TzResult:
    posix: str
    name: Optional[str] = None   # IANA name, when it is known
    note: str = ""               # non-empty when the result is only an estimate


def _get_finder():
    global _finder, _finder_failed
    with _lock:
        if _finder is None and not _finder_failed:
            try:
                from timezonefinder import TimezoneFinder
                _finder = TimezoneFinder()
            except Exception:
                _finder_failed = True
        return _finder


def warm_up() -> None:
    """Load the (slow to start) lookup tables in the background so the first click on the map is instant."""
    threading.Thread(target=_get_finder, daemon=True).start()


def _tzif_bytes(name: str) -> Optional[bytes]:
    parts = name.split("/")
    for base in zoneinfo.TZPATH:
        path = os.path.join(base, *parts)
        if os.path.isfile(path):
            with open(path, "rb") as fh:
                return fh.read()
    try:
        import tzdata
        path = os.path.join(os.path.dirname(tzdata.__file__), "zoneinfo", *parts)
        if os.path.isfile(path):
            with open(path, "rb") as fh:
                return fh.read()
    except ImportError:
        pass
    return None


def posix_for(name: str) -> Optional[str]:
    """The POSIX TZ rule of an IANA zone, read from the footer of its TZif file (the last line)."""
    data = _tzif_bytes(name)
    if not data or data[:4] != b"TZif":
        return None
    footer = data.rstrip(b"\n").rsplit(b"\n", 1)[-1].decode("ascii", errors="ignore").strip()
    return footer or None


def estimate(lon: float) -> str:
    """Standard time from the longitude only: 15 degrees per hour, no daylight saving."""
    hours = max(-12, min(14, round(lon / 15.0)))
    if hours == 0:
        return "UTC0"
    return f"<{'+' if hours > 0 else '-'}{abs(hours):02d}>{-hours}"


def timezone_for(lat: float, lon: float) -> TzResult:
    finder = _get_finder()
    if finder is None:
        return TzResult(estimate(lon), None,
                        "Time zone estimated from the longitude. Install timezonefinder for exact zones.")
    try:
        name = finder.timezone_at(lat=lat, lng=lon)
    except Exception:
        name = None
    if not name:
        return TzResult(estimate(lon), None, "No time zone found here, estimated from the longitude.")
    posix = posix_for(name)
    if not posix:
        return TzResult(estimate(lon), name, f"No rules found for {name}. Install tzdata for exact zones.")
    return TzResult(posix, name)

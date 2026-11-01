"""Map field: an offline world map you can drag, zoom and tap to drop a pin.

Drawn on one Tk canvas in the same dark style as the rest of the app. The data is Natural Earth (public domain),
bundled in world_map.bin.gz next to this file, in three levels of detail that are swapped as you zoom:
  world     countries and their borders
  regional  + lakes, rivers, state / province borders
  local     + built-up areas, city limits (green), smaller rivers and lakes, and about 7000 towns
The local level is cut into a grid when the file is built, so only the pieces in view are ever drawn (and no
coordinate gets far outside the window, which some platforms cannot handle).

Behaviour, following the Apple design notes:
  * response: the map follows the pointer 1:1 while dragging; the pin is placed on release, and dragging away
    turns the tap into a pan
  * direct manipulation: dragging keeps the point you grabbed under the pointer (the 8 px of hysteresis is re-based)
  * momentum: a flick is carried on with Apple's exponential deceleration, at the exact speed of the release
  * interruptibility: grabbing the map stops any motion where it is, a wheel notch continues from the live value
  * springs: zoom, fly-to and the pin drop are springs (critically damped, the drop bounces because it falls)
  * the map stops at the top and bottom edge of the world (no stretching), horizontally it wraps around
  * zoom stays anchored on the pointer (wheel) or on the middle of the map (buttons)
  * reduced motion is handled by ui.Animation (values snap to their target)
"""
import gzip
import json
import math
import os
import sys
import threading
import time
import tkinter as tk
from array import array
from itertools import accumulate
from typing import Callable, List, Optional, Tuple

from ui import ACCENT, BG0, BG1, BG2, BG3, TXT, TXT2, WHITE, Animation, Spring, draw_icon, font, mix, rrect

DATA_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "world_map.bin.gz")

OCEAN = BG1
LAND = mix(BG2, BG3, .5)
BORDER = mix(LAND, BG0, .55)          # country borders
ADMIN = mix(LAND, BG1, .3)            # state and province borders (dashed)
URBAN = mix(LAND, WHITE, .13)         # built-up (metro) areas: a lighter gray
CITY = ACCENT                         # inside the city limits
CTL_FILL = BG2
CTL_LINE = mix(BG2, WHITE, .14)

MAX_LAT = 85.0511287798
S_MAX = 800000.0      # world width in pixels at the deepest zoom (about 50 m per pixel: a 30 km city fills the map)
LOD_SWITCH = (2400.0, 14000.0)   # world widths where the regional and the local level take over
SLOP = 8              # pixels the pointer may move before a tap becomes a drag
DECEL = 0.998         # Apple's scroll deceleration rate (per millisecond)
STOP_SPEED = 12.0     # px/s under which momentum ends

# ----- projection (Web Mercator, x and y run from 0 to 1 over the whole world) -------------------------------------------
def project(lat: float, lon: float) -> Tuple[float, float]:
    lat = max(-MAX_LAT, min(MAX_LAT, lat))
    x = (lon + 180.0) / 360.0
    y = 0.5 - math.log(math.tan(math.pi / 4 + math.radians(lat) / 2)) / (2 * math.pi)
    return x, y


def unproject(x: float, y: float) -> Tuple[float, float]:
    lon = ((x * 360.0 - 180.0 + 180.0) % 360.0) - 180.0
    lat = math.degrees(math.atan(math.sinh(math.pi * (1 - 2 * y))))
    return lat, lon


def decel_project(velocity: float) -> float:
    """Where a flick at `velocity` (px/s) comes to rest, measured from the release point."""
    return (velocity / 1000.0) * DECEL / (1.0 - DECEL)


# ----- data ---------------------------------------------------------------------------------------------------------------
class _Layer:
    """Items are (xmin, xmax, ymin, ymax, xs, ys, class) with the box in world units and xs / ys as integers of
    1 / Q of the world. An untiled layer keeps them in `items`, a tiled one in `cells[(column, row)]`."""

    def __init__(self, name: str, ring: bool):
        self.name, self.ring = name, ring
        self.items = []
        self.cells = {}


class _Lod:
    def __init__(self, tiled: bool, grid: int, layers: List[_Layer]):
        self.tiled, self.grid, self.layers = tiled, grid, layers


def load_world(places: list):
    """Generator: yields the levels of detail one by one (the coarse ones are ready long before the big one).
    The towns are put into `places` as (name, x, y, smallest world width at which the name shows)."""
    with gzip.open(DATA_FILE, "rb") as fh:
        size = int.from_bytes(fh.read(4), "little")
        head = json.loads(fh.read(size).decode("utf-8"))
        q, grid = float(head["Q"]), head["G"]
        for name, lat, lon, min_s in head["places"]:
            places.append((name, *project(lat, lon), min_s))
        for hl in head["lods"]:
            layers = []
            for spec in hl["layers"]:
                n, pts = spec["n"], spec["points"]
                lens, cls, cells, coords = array("I"), array("b"), array("H"), array("i")
                lens.frombytes(fh.read(4 * n))
                cls.frombytes(fh.read(n))
                if hl["tiled"]:
                    cells.frombytes(fh.read(4 * n))
                coords.frombytes(fh.read(8 * pts))
                if sys.byteorder == "big":
                    for a in (lens, cells, coords):
                        a.byteswap()
                xs, ys = array("i", accumulate(coords[0::2])), array("i", accumulate(coords[1::2]))   # undo the deltas
                layer = _Layer(spec["name"], spec["ring"])
                a = 0
                for i, length in enumerate(lens):
                    x, y = xs[a:a + length], ys[a:a + length]
                    a += length
                    item = (min(x) / q, max(x) / q, min(y) / q, max(y) / q, x, y, cls[i])
                    if hl["tiled"]:
                        layer.cells.setdefault((cells[2 * i], cells[2 * i + 1]), []).append(item)
                    else:
                        layer.items.append(item)
                layers.append(layer)
            yield _Lod(hl["tiled"], grid, layers)


# name --> (kind, colour, dash)
STYLE = {
    "land": ("fill", LAND, None), "lakes": ("fill", OCEAN, None), "urban": ("fill", URBAN, None), "city": ("fill", CITY, None),
    "rivers": ("line", OCEAN, None), "admin1": ("line", ADMIN, (4, 3)), "borders": ("line", BORDER, None),
}


# ----- the widget ---------------------------------------------------------------------------------------------------------
class MapField(tk.Canvas):
    owns_wheel = True   # the wheel zooms the map instead of scrolling the page (see ui.ScrollFrame.bind_wheel)
    R = 14              # corner radius, the same as the cards

    def __init__(self, master, on_pick: Callable[[float, float], None], height: int = 340, outer: str = BG0):
        super().__init__(master, height=height, bg=OCEAN, highlightthickness=0)
        self.on_pick, self.outer = on_pick, outer
        self.W, self.H = 600, height
        self.S, self.u, self.v = 600.0, 0.5, 0.5     # world width in px, centre of the view in world units
        self.pin: Optional[Tuple[float, float]] = None
        self._world: List[_Lod] = []   # levels of detail, filled by the loading thread
        self._places = []
        self._missing = False
        self._loaded = False
        self._shown = 0
        self._first = True
        self._pending = False
        self._anchor = None          # (world x, world y, screen x, screen y) kept under a point while zooming
        # gesture state
        self._press = None           # (x, y) where the pointer went down
        self._panning = False
        self._base = (0.0, 0.0, 0.0, 0.0)
        self._hist = []
        self._ctl_down = None
        self._ivx = self._ivy = 0.0
        self._ijob = None
        self._ilast = 0.0
        # springs: pan (u, v), zoom (ln of S) and the pin drop
        self._su, self._sv = Spring(0.5, 1.0, .5), Spring(0.5, 1.0, .5)
        self._sz = Spring(math.log(600.0), 1.0, .3)
        self._sd = Spring(0.0, .6, .4)
        self._au = Animation(self, self._su, self._set_u, eps=1e-6)
        self._av = Animation(self, self._sv, self._set_v, eps=1e-6)
        self._az = Animation(self, self._sz, self._set_zoom, eps=.002)
        self._ad = Animation(self, self._sd, lambda _h: self._invalidate(), eps=.3)

        self.bind("<Configure>", self._on_resize)
        self.bind("<ButtonPress-1>", self._on_press)
        self.bind("<B1-Motion>", self._on_motion)
        self.bind("<ButtonRelease-1>", self._on_release)
        self.bind("<Motion>", self._on_hover)
        for seq in ("<MouseWheel>", "<Button-4>", "<Button-5>"):
            self.bind(seq, self._on_wheel)

        threading.Thread(target=self._load, daemon=True).start()
        self.after(120, self._poll_ready)

    # ----- loading -----

    def _load(self):
        try:
            for lod in load_world(self._places):
                self._world.append(lod)
        except (OSError, ValueError, KeyError, EOFError):
            self._missing = not self._world
        self._loaded = True

    def _poll_ready(self):
        if len(self._world) != self._shown or self._missing:
            self._shown = len(self._world)
            self._invalidate()
        if not (self._loaded or self._missing):
            self.after(150, self._poll_ready)

    # ----- public -----

    def set_location(self, lat: float, lon: float, fly: bool = True) -> None:
        """Put the pin at a location that did not come from a tap (for example read from the ESP32)."""
        self.pin = (lat, lon)
        self._drop_pin()
        if fly:
            self._fly_to(lat, lon, 60000.0)
        self._invalidate()

    def clear_location(self) -> None:
        self.pin = None
        self._invalidate()

    # ----- geometry helpers -----

    @property
    def s_min(self) -> float:
        return float(max(self.W, self.H))

    def _vlimits(self, S: Optional[float] = None) -> Tuple[float, float]:
        half = self.H / (2.0 * (S or self.S))
        return half, 1.0 - half

    def _to_world(self, sx: float, sy: float) -> Tuple[float, float]:
        return self.u + (sx - self.W / 2) / self.S, self.v + (sy - self.H / 2) / self.S

    def _invalidate(self) -> None:
        if not self._pending:
            self._pending = True
            self.after_idle(self._paint)

    def _set_u(self, u):
        self.u = u
        self._invalidate()

    def _set_v(self, v):
        self.v = v
        self._invalidate()

    def _set_zoom(self, ln_s):
        self.S = math.exp(ln_s)
        if self._anchor:
            wx, wy, sx, sy = self._anchor
            self.u = wx - (sx - self.W / 2) / self.S
            lo, hi = self._vlimits()
            self.v = min(max(wy - (sy - self.H / 2) / self.S, lo), hi)
        self._invalidate()

    def _on_resize(self, e):
        self.W, self.H = max(e.width, 100), max(e.height, 100)
        if self._first:
            self._first = False
            self.S = self.s_min
            self.u = (10 + 180) / 360.0
            self.v = project(32, 0)[1]
        self.S = max(self.S, self.s_min)
        lo, hi = self._vlimits()
        self.v = min(max(self.v, lo), hi)
        self._invalidate()

    # ----- motion control -----

    def _stop_motion(self, keep_zoom: bool = False) -> None:
        """Grabbing the map stops whatever it was doing, exactly where it is. A wheel notch keeps a running zoom."""
        if self._ijob is not None:
            self.after_cancel(self._ijob)
            self._ijob = None
        self._ivx = self._ivy = 0.0
        for anim in (self._au, self._av) + (() if keep_zoom else (self._az,)):
            anim.stop()
        self._su.x, self._su.v = self.u, 0.0
        self._sv.x, self._sv.v = self.v, 0.0
        if not keep_zoom:
            self._sz.x, self._sz.v = math.log(self.S), 0.0
            self._anchor = None

    def _fly_to(self, lat: float, lon: float, min_scale: float) -> None:
        self._stop_motion()
        x, y = project(lat, lon)
        scale = min(S_MAX, max(self.S, min_scale))
        lo, hi = self._vlimits(scale)
        tu = x + round(self.u - x)   # the copy of the world nearest to the current view
        tv = min(max(y, lo), hi)
        self._au.eps = self._av.eps = 0.1 / scale
        self._au.go(tu, damping=1.0, response=.6)
        self._av.go(tv, damping=1.0, response=.6)
        self._az.go(math.log(scale), damping=1.0, response=.6)

    def _zoom_by(self, steps: float, sx: float, sy: float) -> None:
        base = self._sz.target if self._az.running else math.log(self.S)
        target = min(max(base + steps, math.log(self.s_min)), math.log(S_MAX))
        wx, wy = self._to_world(sx, sy)
        self._anchor = (wx, wy, sx, sy)
        if not self._az.running:
            self._sz.x = math.log(self.S)
        self._az.go(target, damping=1.0, response=.3, done=self._clear_anchor)

    def _clear_anchor(self) -> None:
        self._anchor = None

    def _drop_pin(self) -> None:
        self._sd.x, self._sd.v = 64.0, 0.0
        self._ad.go(0.0)

    # ----- momentum -----

    def _inertia_tick(self) -> None:
        self._ijob = None
        now = time.perf_counter()
        dt = min(now - self._ilast, .05)
        self._ilast = now
        decay = DECEL ** (dt * 1000.0)
        self._ivx *= decay
        self._ivy *= decay
        self.u -= self._ivx * dt / self.S
        lo, hi = self._vlimits()
        v = self.v - self._ivy * dt / self.S
        if v < lo or v > hi:
            v, self._ivy = min(max(v, lo), hi), 0.0   # reached the edge of the world: stop moving that way
        self.v = v
        self._invalidate()
        if math.hypot(self._ivx, self._ivy) < STOP_SPEED:
            self.u %= 1.0
            return
        self._ijob = self.after(8, self._inertia_tick)

    def _start_inertia(self, vx: float, vy: float) -> None:
        self._ivx, self._ivy = vx, vy
        self._ilast = time.perf_counter()
        self._ijob = self.after(1, self._inertia_tick)

    # ----- controls drawn on the map -----

    def _controls(self):
        """(name, box) of every button. The zoom pair is one capsule, the locate button a circle below it."""
        x1, size = self.W - 12, 38
        x0 = x1 - size
        items = [("zoom_in", (x0, 12, x1, 12 + size)), ("zoom_out", (x0, 12 + size, x1, 12 + 2 * size))]
        if self.pin:
            items.append(("locate", (x0, 12 + 2 * size + 10, x1, 12 + 3 * size + 10)))
        return items

    def _control_at(self, x, y) -> Optional[str]:
        for name, (a, b, c, d) in self._controls():
            if a <= x < c and b <= y < d:
                return name
        return None

    def _fire(self, name: str) -> None:
        if name == "zoom_in":
            self._stop_motion()
            self._zoom_by(math.log(2.0), self.W / 2, self.H / 2)
        elif name == "zoom_out":
            self._stop_motion()
            self._zoom_by(-math.log(2.0), self.W / 2, self.H / 2)
        elif name == "locate" and self.pin:
            self._fly_to(*self.pin, self.S)

    # ----- pointer -----

    def _on_press(self, e):
        self.focus_set()
        self._stop_motion()
        name = self._control_at(e.x, e.y)
        if name:
            self._ctl_down = name
            self._press = None
        else:
            self._ctl_down = None
            self._press = (e.x, e.y)
        self._panning = False
        self._hist = []
        self._invalidate()

    def _on_motion(self, e):
        if self._ctl_down:
            self._invalidate()
            return
        if self._press is None:
            return
        if not self._panning:
            if math.hypot(e.x - self._press[0], e.y - self._press[1]) < SLOP:
                return
            self._panning = True
            self._base = (e.x, e.y, self.u, self.v)   # re-based here, so the map does not jump when the slop ends
            self.configure(cursor="fleur")
        bx, by, bu, bv = self._base
        self.u = bu - (e.x - bx) / self.S
        lo, hi = self._vlimits()
        self.v = min(max(bv - (e.y - by) / self.S, lo), hi)
        now = time.perf_counter()
        self._hist.append((now, e.x, e.y))
        self._hist = [h for h in self._hist if now - h[0] < .1]
        self._invalidate()

    def _on_release(self, e):
        if self._ctl_down:
            name, self._ctl_down = self._ctl_down, None
            if self._control_at(e.x, e.y) == name:
                self._fire(name)
            self._invalidate()
            return
        if self._press is None:
            return
        press, self._press = self._press, None
        if not self._panning:
            self._place(*press)
            return
        self._panning = False
        self.configure(cursor="")
        vx = vy = 0.0
        now = time.perf_counter()
        if len(self._hist) > 1 and now - self._hist[-1][0] < .08:
            t0, x0, y0 = self._hist[0]
            t1, x1, y1 = self._hist[-1]
            if t1 - t0 > .01:
                vx, vy = (x1 - x0) / (t1 - t0), (y1 - y0) / (t1 - t0)
        if math.hypot(vx, vy) > 60:
            self._start_inertia(vx, vy)
        else:
            self.u %= 1.0

    def _place(self, sx: float, sy: float) -> None:
        x, y = self._to_world(sx, sy)
        if not 0.0 <= y <= 1.0:
            return
        lat, lon = unproject(x, y)
        self.pin = (lat, lon)
        self._drop_pin()
        self._invalidate()
        self.on_pick(lat, lon)

    def _on_hover(self, e):
        if self._press is None and self._ctl_down is None:
            self.configure(cursor="hand2" if self._control_at(e.x, e.y) else "")

    def _on_wheel(self, e):
        self._stop_motion(keep_zoom=True)
        if getattr(e, "num", 0) == 4:
            notches = 1.0
        elif getattr(e, "num", 0) == 5:
            notches = -1.0
        else:
            notches = max(-3.0, min(3.0, e.delta / 120.0))
        self._zoom_by(notches * 0.45, e.x, e.y)
        return "break"

    # ----- drawing -----

    def _paint(self) -> None:
        self._pending = False
        try:
            self._draw()
        except tk.TclError:
            pass

    def _draw(self) -> None:
        W, H, S, u, v = self.W, self.H, self.S, self.u, self.v
        self.delete("all")
        if not self._world:
            msg = "Map data not found (world_map.bin.gz)" if self._missing else "Loading map..."
            self.create_text(W / 2, H / 2, text=msg, fill=TXT2, font=font(11))
            self._draw_chrome()
            return
        level = sum(S >= t for t in LOD_SWITCH)
        lod = self._world[min(level, len(self._world) - 1)]
        for layer in lod.layers:
            self._draw_layer(lod, layer, S, W, H, u, v)
        self._draw_places(S, W, H, v)
        self._draw_pin(S, W, H, u, v)
        self._draw_chrome()

    def _draw_layer(self, lod, layer, S, W, H, u, v) -> None:
        kind, color, dash = STYLE[layer.name]
        x_lo, x_hi, y_lo, y_hi = u - W / (2 * S), u + W / (2 * S), v - H / (2 * S), v + H / (2 * S)
        ox, oy = W / 2 - u * S, H / 2 - v * S
        if not lod.tiled:
            for k in range(math.floor(x_lo - 2), math.floor(x_hi) + 1):
                for item in layer.items:
                    self._draw_item(item, k, S, ox, oy, W, H, kind, color, dash, layer.name)
            return
        G = lod.grid
        for col in range(math.floor(x_lo * G), math.floor(x_hi * G) + 1):
            base = col % G
            for cell_col in (base, base + G):     # a piece that crosses the date line is stored in the second half
                k = (col - cell_col) // G
                for row in range(max(0, math.floor(y_lo * G)), min(G - 1, math.floor(y_hi * G)) + 1):
                    for item in layer.cells.get((cell_col, row), ()):
                        self._draw_item(item, k, S, ox, oy, W, H, kind, color, dash, layer.name)

    def _draw_item(self, item, k, S, ox, oy, W, H, kind, color, dash, name) -> None:
        xmin, xmax, ymin, ymax, xs, ys, cls = item
        x0, x1 = (xmin + k) * S + ox, (xmax + k) * S + ox
        y0, y1 = ymin * S + oy, ymax * S + oy
        if x1 < -4 or x0 > W + 4 or y1 < -4 or y0 > H + 4 or (x1 - x0 < 1.2 and y1 - y0 < 1.2):
            return
        q, kx = S / (1 << 24), k * S + ox
        pts = [0.0] * (2 * len(xs))
        pts[0::2] = [x * q + kx for x in xs]
        pts[1::2] = [y * q + oy for y in ys]
        if kind == "fill":
            self.create_polygon(pts, fill=color, outline=color)   # same colour outline: no hairline between pieces
        elif dash:
            self.create_line(pts, fill=color, width=1, dash=dash)
        else:
            width = 2 if name == "rivers" and cls <= 4 and S >= 30000 else 1
            self.create_line(pts, fill=color, width=width)

    def _screen_x(self, x: float) -> float:
        """Screen x of a world x, using the copy of the world nearest to the middle of the view."""
        return ((x - self.u + 0.5) % 1.0 - 0.5) * self.S + self.W / 2

    def _draw_places(self, S, W, H, v) -> None:
        """Town names, biggest first. A name that would overlap one already placed is left out."""
        taken = []
        for name, x, y, min_s in self._places:
            if S < min_s:
                continue
            sy = (y - v) * S + H / 2
            if not 0 < sy < H:
                continue
            sx = self._screen_x(x)
            if not -40 < sx < W + 40:
                continue
            big = min_s <= 4000
            box = (sx - 4, sy - 9, sx + 12 + len(name) * (6.4 if big else 5.8), sy + 9)
            if any(box[0] < b[2] and box[2] > b[0] and box[1] < b[3] and box[3] > b[1] for b in taken):
                continue
            taken.append(box)
            r = 3 if big else 2.2
            self.create_oval(sx - r, sy - r, sx + r, sy + r, fill=TXT2, outline="")
            self.create_text(sx + 7, sy, text=name, anchor="w", fill=TXT2,
                             font=font(10 if big else 9))

    def _draw_pin(self, S, W, H, u, v) -> None:
        if not self.pin:
            return
        x, y = project(*self.pin)
        px, py = self._screen_x(x), (y - v) * S + H / 2
        if not (-30 < px < W + 30 and -60 < py < H + 30):
            return
        lift = max(0.0, self._sd.x)
        shade = 6 + lift * .05
        self.create_oval(px - shade, py - shade * .45, px + shade, py + shade * .45, fill=mix(LAND, BG0, .6), outline="")
        top = py - lift
        hx, hy = px, top - 26
        self.create_polygon(hx - 11, hy + 6, hx + 11, hy + 6, px, top + 1, fill=WHITE, outline=WHITE, width=2,
                            joinstyle="round")
        self.create_oval(hx - 14, hy - 14, hx + 14, hy + 14, fill=WHITE, outline="")
        self.create_polygon(hx - 9, hy + 6, hx + 9, hy + 6, px, top - 3, fill=ACCENT, outline=ACCENT, width=2,
                            joinstyle="round")
        self.create_oval(hx - 11, hy - 11, hx + 11, hy + 11, fill=ACCENT, outline="")
        self.create_oval(hx - 4, hy - 4, hx + 4, hy + 4, fill=WHITE, outline="")

    def _draw_chrome(self) -> None:
        """Buttons on top of the map, then the rounded corners."""
        for name, (x0, y0, x1, y1) in self._controls():
            down = self._ctl_down == name
            fill = BG3 if down else CTL_FILL
            if name == "locate":
                self.create_oval(x0, y0, x1, y1, fill=fill, outline=CTL_LINE)
                cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
                self.create_oval(cx - 8, cy - 8, cx + 8, cy + 8, outline=TXT, width=2)
                self.create_oval(cx - 3, cy - 3, cx + 3, cy + 3, fill=TXT, outline="")
                for dx, dy in ((0, -1), (0, 1), (-1, 0), (1, 0)):
                    self.create_line(cx + dx * 8, cy + dy * 8, cx + dx * 13, cy + dy * 13, fill=TXT, width=2,
                                     capstyle="round")
                continue
        x0, y0, x1, y1 = self._controls()[0][1][0], 12, self._controls()[0][1][2], 12 + 76
        rrect(self, x0, y0, x1, y1, 19, fill=CTL_FILL, outline=CTL_LINE)
        for name, (a, b, c_, d) in self._controls()[:2]:
            if self._ctl_down == name:
                rrect(self, a + 2, b + 2, c_ - 2, d - 2, 17, fill=BG3)
            cx, cy = (a + c_) / 2, (b + d) / 2
            if name == "zoom_in":
                draw_icon(self, "plus", cx, cy, 15, TXT, width=2)
            else:
                self.create_line(cx - 6, cy, cx + 6, cy, fill=TXT, width=2, capstyle="round")
        self.create_line(x0 + 9, 12 + 38, x1 - 9, 12 + 38, fill=CTL_LINE)
        self._corners()

    def _corners(self) -> None:
        """Tk canvases have square corners: paint the page colour over them to round the map like the cards."""
        r, W, H = self.R, self.W, self.H
        for cx, cy, sx, sy in ((r, r, -1, -1), (W - r, r, 1, -1), (W - r, H - r, 1, 1), (r, H - r, -1, 1)):
            pts = [cx + sx * (r + 1), cy + sy * (r + 1), cx, cy + sy * (r + 1)]
            for i in range(9):
                t = i / 8
                vx, vy = t * sx, (1 - t) * sy
                n = math.hypot(vx, vy)
                pts += [cx + vx / n * r, cy + vy / n * r]
            pts += [cx + sx * (r + 1), cy]
            self.create_polygon(pts, fill=self.outer, outline="")
"""The four screens of the app. Each one is a Frame that the navigator slides in and out."""
import math
import queue
import threading
import time
import tkinter as tk
from tkinter import font as tkfont

from model import (DEFAULT_TANK_HIGH_CM, DEFAULT_TANK_LOW_CM, DeviceConfig, check_tank, fmt_coord, fmt_num)
from serial_link import SerialError
import geo
from map_view import MapField
from sheets import Popover
from report import build as build_sections
from timetable_view import TimetableView
from ui import (ACCENT, BG0, BG1, BG2, BG3, LINE, TXT, TXT2, TXT3, WARN, WHITE, Animation, Button, Card,
                RoundEntry, ScrollFrame, Spring, Switch, draw_icon, font, footer_label, form_row,
                mix, rrect, section_label, separator)


# ----- home -------------------------------------------------------------------------------------------------------------
class Tile(tk.Canvas):
    """Large home button: vector icon in the middle, title below it. Shrinks on press, springs back on release."""

    def __init__(self, master, icon, title, subtitle, command):
        super().__init__(master, bg=BG0, highlightthickness=0, cursor="hand2")
        self.icon, self.title, self.subtitle, self.cmd = icon, title, subtitle, command
        self.down = self.inside = self.hover = False
        self.spring = Spring(1.0, 1.0, 0.25)
        self.anim = Animation(self, self.spring, lambda _s: self._draw(), eps=0.0005)
        self.bind("<Configure>", lambda e: self._draw())
        self.bind("<ButtonPress-1>", self._press)
        self.bind("<ButtonRelease-1>", self._release)
        self.bind("<Enter>", self._enter)
        self.bind("<Leave>", self._leave)

    def _press(self, _e):
        self.down = self.inside = True
        self.anim.go(0.965)
        self._draw()

    def _enter(self, _e):
        self.hover = True
        if self.down:
            self.inside = True
            self.anim.go(0.965)
        self._draw()

    def _leave(self, _e):
        self.hover = False
        if self.down:
            self.inside = False
            self.anim.go(1.0)
        self._draw()

    def _release(self, _e):
        fire = self.down and self.inside
        self.down = False
        self.anim.go(1.0)
        self._draw()
        if fire:
            self.cmd()

    def _draw(self):
        self.delete("all")
        w, h = self.winfo_width(), self.winfo_height()
        m = 10
        cw = w - 2 * m
        ch = min(h - 2 * m, cw * 1.15)
        if cw < 40 or ch < 40:
            return
        s = self.spring.x
        cx, cy = w / 2, h / 2
        fill = BG2 if (self.down and self.inside) else (mix(BG1, BG2, .5) if self.hover else BG1)
        rrect(self, cx - cw * s / 2, cy - ch * s / 2, cx + cw * s / 2, cy + ch * s / 2, 26 * s, fill=fill)
        u = min(cw, ch) * s
        r = u * 0.2
        iy = cy - u * 0.1
        self.create_oval(cx - r, iy - r, cx + r, iy + r, fill=ACCENT, outline="")
        draw_icon(self, self.icon, cx, iy, r * 1.15, WHITE, bg=ACCENT, width=max(2, r * .09))
        fs = max(13, min(20, int(u * .07)))
        ty = cy + u * 0.2
        self.create_text(cx, ty, text=self.title, fill=TXT, font=font(fs, "bold"))
        self.create_text(cx, ty + fs + 10, text=self.subtitle, fill=TXT2, anchor="n",
                         font=font(max(10, min(12, int(u * .045)))), width=cw * .75, justify="center")


class HomeScreen(tk.Frame):
    def __init__(self, master, app):
        super().__init__(master, bg=BG0)
        tk.Label(self, text="Irrigation controller", bg=BG0, fg=TXT, font=font(26, "bold")).grid(
            row=0, column=0, columnspan=3, pady=(44, 2))
        tk.Label(self, text="4-zone irrigation system", bg=BG0, fg=TXT2, font=font(13)).grid(
            row=1, column=0, columnspan=3, pady=(0, 8))
        self.rowconfigure(2, weight=1)
        for col, (icon, title, sub, name) in enumerate((
                ("calendar", "Timetable editor", "Plan the weekly watering events", "timetable"),
                ("monitor", "Monitoring", "Live data from the controller", "monitor"),
                ("gear", "Device settings", "API key, location, time zone and water tank", "settings"))):
            self.columnconfigure(col, weight=1, uniform="tile")
            Tile(self, icon, title, sub, lambda n=name: app.open_screen(n)).grid(
                row=2, column=col, sticky="nsew", padx=10, pady=(0, 36))


# ----- timetable --------------------------------------------------------------------------------------------------------
class TimetableScreen(tk.Frame):
    def __init__(self, master, app):
        super().__init__(master, bg=BG0)
        self.app = app
        self.file_btn = None
        self.view = TimetableView(self, app.events, app.edit_event, app.mark_dirty, app.alert)
        self.view.pack(fill="both", expand=True)

    def build_actions(self, parent):
        """File, Upload and Download live in the top bar, right of the Home button."""
        self.file_btn = Button(parent, text="File", style="link", command=self.open_file_menu, bg=BG1)
        self.file_btn.pack(side="left")
        Button(parent, text="Upload", style="link", command=self.app.upload, bg=BG1).pack(side="left")
        Button(parent, text="Download", style="link", command=self.app.download, bg=BG1).pack(side="left")

    def open_file_menu(self):
        a = self.app
        Popover(a, self.file_btn, [("New", a.file_new), ("Open...", a.file_open), ("Save", a.file_save),
                                   ("Save As...", a.file_save_as)])


# ----- monitoring -------------------------------------------------------------------------------------------------------
class Meter(tk.Canvas):
    """Slim progress bar for percentages."""
    W, H = 84, 6

    def __init__(self, master, bg):
        super().__init__(master, width=self.W, height=self.H, bg=bg, highlightthickness=0)

    def set(self, percent):
        self.delete("all")
        rrect(self, 0, 0, self.W, self.H, self.H / 2, fill=BG3)
        if percent and percent > 0:
            rrect(self, 0, 0, max(self.H, self.W * percent / 100), self.H, self.H / 2, fill=ACCENT)


# Temperature --> colour of the range bar, from cold teal to warm orange (like the Apple Weather app)
HEAT = [(-10, "#5e9bff"), (0, "#5ac8fa"), (10, "#64d2a0"), (18, "#c7d95a"), (24, "#ffd60a"), (30, "#ff9f0a"),
        (38, "#ff453a")]
ICON_COLOR = {"sun": "#ffd60a", "cloud": "#d1d1d6", "rain": "#aeaeb2"}
RAIN_BLUE = "#5ac8fa"


def heat(t):
    if t <= HEAT[0][0]:
        return HEAT[0][1]
    for (t0, c0), (t1, c1) in zip(HEAT, HEAT[1:]):
        if t <= t1:
            return mix(c0, c1, (t - t0) / (t1 - t0))
    return HEAT[-1][1]


class DayRow(tk.Canvas):
    """One forecast line, drawn like the Apple Weather app: day, icon with the chance of rain, low, a range bar
    on a scale shared by all days, and the high. Today also gets a dot for the current temperature."""
    H = 58
    BAR = 6

    def __init__(self, master, row, bg):
        super().__init__(master, height=self.H, bg=bg, highlightthickness=0)
        self.row, self.rowbg = row, bg
        self.bind("<Configure>", lambda e: self.draw())

    def set(self, row):
        self.row = row
        self.draw()

    def draw(self):
        self.delete("all")
        w, r = self.winfo_width(), self.row
        if w < 100:
            return
        cy, bh = self.H / 2, self.BAR
        self.create_text(16, cy, text=r.label, anchor="w", fill=TXT, font=font(13))
        ix = w * 0.30
        if r.percent:
            draw_icon(self, r.icon, ix, cy - 7, 24, ICON_COLOR[r.icon], bg=self.rowbg)
            self.create_text(ix, cy + 14, text=f"{r.percent:g}%", fill=RAIN_BLUE, font=font(9, "bold"))
        else:
            draw_icon(self, r.icon, ix, cy, 26, ICON_COLOR[r.icon], bg=self.rowbg)
        self.create_text(w - 16, cy, text=f"{round(r.high)}\u00b0", anchor="e", fill=TXT, font=font(13))
        bar_w = min(150.0, w * 0.34)
        x1 = w - 16 - 40
        x0 = x1 - bar_w
        self.create_text(x0 - 10, cy, text=f"{round(r.low)}\u00b0", anchor="e", fill=TXT2, font=font(13))
        rrect(self, x0, cy - bh / 2, x1, cy + bh / 2, bh / 2, fill=BG3)
        lo, hi = r.scale
        span = max(hi - lo, 1.0)
        xa = x0 + (r.low - lo) / span * bar_w
        xb = max(x0 + (r.high - lo) / span * bar_w, xa + bh)
        xb = min(xb, x1)
        xa = min(xa, xb - bh)
        # the coloured range is one capsule made of 1 px columns: a column is as tall as the circle at that
        # distance from the end, so both ends are truly round and nothing is drawn twice
        rad = bh / 2.0
        for px in range(int(math.floor(xa)), int(math.ceil(xb))):
            x = px + 0.5
            if x < xa + rad:
                half = math.sqrt(max(0.0, rad * rad - (xa + rad - x) ** 2))
            elif x > xb - rad:
                half = math.sqrt(max(0.0, rad * rad - (x - (xb - rad)) ** 2))
            else:
                half = rad
            if half < 0.4:
                continue
            self.create_line(px, cy - half, px, cy + half, fill=heat(lo + (x - x0) / bar_w * span))
        if r.marker is not None:
            m = min(max(r.marker, r.low), r.high)
            mx = min(max(x0 + (m - lo) / span * bar_w, xa + bh / 2), xb - bh / 2)
            self.create_oval(mx - 4, cy - 4, mx + 4, cy + 4, fill=WHITE, outline=self.rowbg, width=2)


class RowView:
    """One line of the Monitoring screen. Built once, then only its texts change, so nothing flickers."""

    def __init__(self, parent, row):
        bg = BG2
        self.kind = row.kind
        self.meter = self.badge = self.caption = self.text = self.day = None
        if row.kind == "hero":
            self.frame = tk.Frame(parent, bg=bg)
            self.frame.pack(fill="x")
            self.value = tk.Label(self.frame, bg=bg, fg=TXT, font=font(32), anchor="w")
            self.value.pack(fill="x", padx=16, pady=(10, 0))
            self.caption = tk.Label(self.frame, bg=bg, fg=TXT2, font=font(11), anchor="w")
            self.caption.pack(fill="x", padx=16, pady=(0, 14))
            self.update(row)
            return
        if row.kind == "day":
            self.day = self.frame = DayRow(parent, row, bg)
            self.day.pack(fill="x")
            return
        right = form_row(parent, row.label, bg=bg)
        self.frame = right.master
        if row.kind == "status":
            self.text = tk.Label(right, bg=bg, fg=TXT2, font=font(12))
            self.text.pack(side="left", padx=(0, 10))
            self.badge = tk.Canvas(right, width=24, height=24, bg=bg, highlightthickness=0)
            self.badge.pack(side="left")
        else:
            if row.kind == "bar":
                self.meter = Meter(right, bg)
                self.meter.pack(side="left", padx=(0, 12))
            self.value = tk.Label(right, bg=bg, fg=TXT, font=font(12), **({"width": 7, "anchor": "e"} if self.meter else {}))
            self.value.pack(side="left")   # a fixed width keeps all the bars in one column
        self.update(row)

    def update(self, row):
        if self.kind == "day":
            self.day.set(row)
        elif self.kind == "status":
            self.text.configure(text=row.value)
            b = self.badge
            b.delete("all")
            fill = ACCENT if row.active else WARN
            b.create_oval(1, 1, 23, 23, fill=fill, outline="")
            draw_icon(b, "check" if row.active else "cross", 12, 12, 12, WHITE, bg=fill, width=2)
        else:
            self.value.configure(text=row.value)
            if self.caption is not None:
                self.caption.configure(text=row.caption)
            if self.meter is not None:
                self.meter.set(row.percent)


class MonitorScreen(tk.Frame):
    """Live view of the ESP32 internal data. Runs PRINT once a second while the screen is visible and lays the
    report out in grouped cards (see report.py for how a report is split into sections)."""
    INTERVAL = 1.0

    def __init__(self, master, app):
        super().__init__(master, bg=BG0)
        self.app, self.link = app, app.link
        self.reports = queue.Queue()
        self.halt = threading.Event()
        self.paused = False        # plain attribute: the worker thread must not touch Tk variables
        self.last_report = None
        self.last_error = None
        self.pause_btn = None
        self._after_id = None
        self._signature = None
        self._views = []

        self.scroll = ScrollFrame(self, bg=BG0, max_width=640)
        self.scroll.pack(fill="both", expand=True)
        self._message("Waiting for the first report...")

    def build_actions(self, parent):
        """Pause and Copy sit in the top bar, right of the Home button (like File / Upload / Download)."""
        self.pause_btn = Button(parent, icon="play" if self.paused else "pause", style="link",
                                command=self._toggle_pause, bg=BG1)
        self.pause_btn.pack(side="left")
        Button(parent, text="Copy", style="link", command=self._copy, bg=BG1).pack(side="left")

    def on_show(self):
        self.halt = threading.Event()
        threading.Thread(target=self._worker, args=(self.halt,), daemon=True).start()
        self._poll()

    def on_hide(self):
        self.halt.set()
        if self._after_id is not None:
            self.after_cancel(self._after_id)
            self._after_id = None

    # ----- actions -----

    def _toggle_pause(self):
        self.paused = not self.paused
        self.pause_btn.set_icon("play" if self.paused else "pause")
        self.app.say("Monitoring paused" if self.paused else "Monitoring resumed")

    def _copy(self):
        if not self.last_report:
            self.app.say("Nothing to copy yet", warn=True)
            return
        self.clipboard_clear()
        self.clipboard_append(self.last_report)
        self.app.say("Copied the report to the clipboard")

    # ----- reading -----

    def _worker(self, halt):
        while not halt.is_set():
            if not self.paused:
                try:
                    self.reports.put(("report", self.link.read_status()))
                except SerialError as exc:
                    self.reports.put(("error", str(exc)))
            halt.wait(self.INTERVAL)

    def _poll(self):
        try:
            while True:
                kind, value = self.reports.get_nowait()
                if kind == "report":
                    self.last_error = None
                    self._show(value)
                elif value != self.last_error:
                    self.last_error = value
                    self.app.say(f"Monitoring: {value}", warn=True)
        except queue.Empty:
            pass
        self._after_id = self.after(100, self._poll)

    # ----- drawing -----

    def _message(self, text):
        self._signature = None
        for child in self.scroll.inner.winfo_children():
            child.destroy()
        tk.Label(self.scroll.inner, text=text, bg=BG0, fg=TXT2, font=font(12)).pack(pady=60)

    def _show(self, report):
        if report == self.last_report:
            return   # nothing changed, leave the screen alone
        self.last_report = report
        sections = build_sections(report)
        if not sections:
            self._message("The report is empty.")
            return
        signature = [s.signature() for s in sections]
        if signature == self._signature:   # same layout: only the values changed
            rows = [r for s in sections for r in s.rows]
            for view, row in zip(self._views, rows):
                view.update(row)
            return
        self._signature = signature
        first = self.scroll.canvas.yview()[0]
        inner = self.scroll.inner
        for child in inner.winfo_children():
            child.destroy()
        self._views = []
        for section in sections:
            section_label(inner, section.title)
            card = Card(inner, fill=BG2, outer=BG0)
            card.pack(fill="x", padx=16)
            for i, row in enumerate(section.rows):
                if i:
                    separator(card.inner, bg=BG2)
                self._views.append(RowView(card.inner, row))
        tk.Frame(inner, bg=BG0, height=28).pack()
        self.scroll.bind_wheel()
        self.update_idletasks()
        self.scroll.canvas.yview_moveto(first)


# ----- device settings --------------------------------------------------------------------------------------------------
class TankDiagram(tk.Canvas):
    """Side view of the water tank: the ultrasonic sensor on top, the water, and the two levels the user sets.
    It follows the entries while they are typed in, and the markers glide to their new place on springs (they can
    be re-targeted at any moment). The red zone is where the level is below 10 % and the error LED stays on."""
    H = 196
    BARREL_W = 120
    TOP = 46
    LED_PERCENT = 0.10   # same threshold as the firmware (WATER_LOW_ON_PERCENT)

    def __init__(self, master, bg):
        super().__init__(master, height=self.H, bg=bg, highlightthickness=0)
        self.high, self.low = DEFAULT_TANK_HIGH_CM, DEFAULT_TANK_LOW_CM
        self.hi_spring, self.lo_spring = Spring(0.0, 1.0, 0.3), Spring(0.0, 1.0, 0.3)
        self.hi_anim = Animation(self, self.hi_spring, lambda _x: self.draw(), eps=0.0005)
        self.lo_anim = Animation(self, self.lo_spring, lambda _x: self.draw(), eps=0.0005)
        self.bind("<Configure>", lambda _e: self.draw())
        self.set_levels(self.high, self.low, animate=False)

    @staticmethod
    def _fractions(high, low):
        """Where the two levels sit along the barrel, 0 = at the sensor, 1 = bottom. The scale follows the low level,
        so the barrel is always filled by the range that matters."""
        depth = low * 1.12
        return high / depth, low / depth

    def set_levels(self, high, low, animate=True):
        self.high, self.low = high, low
        hf, lf = self._fractions(high, low)
        if animate:
            self.hi_anim.go(hf)
            self.lo_anim.go(lf)
        else:
            for sp, value in ((self.hi_spring, hf), (self.lo_spring, lf)):
                sp.x = sp.target = value
                sp.v = 0.0
            self.draw()

    def draw(self):
        self.delete("all")
        w, bg = self.winfo_width(), self.cget("bg")
        if w < 200:
            return
        cx = w * 0.34
        x0, x1 = cx - self.BARREL_W / 2, cx + self.BARREL_W / 2
        top, bot = self.TOP, self.H - 14
        span = bot - top
        y_hi = top + self.hi_spring.x * span
        y_lo = top + self.lo_spring.x * span
        # the LED zone starts where the level drops below 10 %: the low level minus 10 % of the high-to-low distance
        y_zone = top + (self.lo_spring.x - self.LED_PERCENT * (self.lo_spring.x - self.hi_spring.x)) * span

        # barrel: air, water, and the red zone at the bottom
        rrect(self, x0, top, x1, bot, 10, fill=BG1, outline="")
        inner_l, inner_r = x0 + 2, x1 - 2
        self.create_rectangle(inner_l, y_hi, inner_r, y_zone, fill=ACCENT, outline="")
        red = mix(BG1, WARN, .75)
        r = max(1.0, min(8.0, (bot - 2 - y_zone) / 2))
        rrect(self, inner_l, y_zone, inner_r, bot - 2, r, fill=red, outline="")
        self.create_rectangle(inner_l, y_zone, inner_r, y_zone + r, fill=red, outline="")
        rrect(self, x0, top, x1, bot, 10, fill="", outline=TXT3, width=2)

        # the measuring beam from the sensor to the water
        self.create_line(cx, top + 2, cx, y_hi - 1, fill=TXT2, dash=(2, 3), arrow="last", arrowshape=(6, 7, 3))

        # the sensor, on the lid, pointing down
        rrect(self, cx - 28, top - 24, cx + 28, top - 3, 5, fill=BG3, outline="")
        for ex in (-14, 14):
            self.create_oval(cx + ex - 7, top - 21, cx + ex + 7, top - 6, fill=BG2, outline=TXT2, width=2)
        self.create_text(cx + 40, top - 14, text="Ultrasonic sensor", anchor="w", fill=TXT3, font=font(9))

        # the two levels, labelled where they are (the low label is pushed down if the two would touch)
        label_x = x1 + 34
        y_lab_hi = y_hi
        y_lab_lo = max(y_lo, y_lab_hi + 20)
        bold = tkfont.Font(font=font(11, "bold"))
        for y, ly, title, value in ((y_hi, y_lab_hi, "High level", self.high), (y_lo, y_lab_lo, "Low level", self.low)):
            self.create_line(x0 - 8, y, x1 + 12, y, fill=WHITE, width=2, dash=(5, 3))
            self.create_line(x1 + 12, y, x1 + 22, ly, label_x - 6, ly, fill=TXT3)
            self.create_text(label_x, ly, text=title, anchor="w", fill=TXT, font=font(11, "bold"))
            self.create_text(label_x + bold.measure(title) + 8, ly, text=f"{fmt_num(value)} cm", anchor="w",
                             fill=TXT2, font=font(11))


class SettingsScreen(tk.Frame):
    """Reads and writes the ESP32 settings: OpenWeatherMap API key, location, time zone, water tank levels and beeper.
    The location is chosen on a map; latitude, longitude and the POSIX time zone follow from the pin.
    The tank levels are the distances the ultrasonic sensor reads when the tank is full and when it is empty."""

    def __init__(self, master, app):
        super().__init__(master, bg=BG0)
        self.app = app
        self.key_var, self.lat_var = tk.StringVar(), tk.StringVar()
        self.lon_var, self.tz_var = tk.StringVar(), tk.StringVar()
        self.high_var = tk.StringVar(value=fmt_num(DEFAULT_TANK_HIGH_CM))
        self.low_var = tk.StringVar(value=fmt_num(DEFAULT_TANK_LOW_CM))
        self.tz_name = ""   # IANA name of the zone found for the last pin, shown next to the POSIX string
        geo.warm_up()

        scroll = ScrollFrame(self, bg=BG0, max_width=640)
        scroll.pack(fill="both", expand=True)
        inner = scroll.inner

        section_label(inner, "OpenWeatherMap")
        c = Card(inner, fill=BG2, outer=BG0)
        c.pack(fill="x", padx=16)
        right = form_row(c.inner, "API key", bg=BG2)
        self.key_entry = RoundEntry(right, self.key_var, width=348, show="*", bg=BG2)
        self.key_entry.pack()
        separator(c.inner, bg=BG2)
        right = form_row(c.inner, "Show API key", bg=BG2)
        Switch(right, False, command=lambda v: self.key_entry.configure(show="" if v else "*"), bg=BG2).pack()

        section_label(inner, "Location", "Click the map to choose where the controller is installed. "
                                         "Drag to move, scroll to zoom.")
        self.map = MapField(inner, self._picked, outer=BG0)
        self.map.pack(fill="x", padx=16)
        self.readout = tk.Label(inner, bg=BG0, fg=TXT3, font=font(10), anchor="w", justify="left", wraplength=500)
        self.readout.pack(fill="x", padx=(32, 16), pady=(8, 0))
        tk.Frame(inner, bg=BG0, height=8).pack()
        footer_label(inner, "Changing the location or time zone clears the cached weather and forecast on the ESP32, "
                            "it fetches fresh data right after the settings are written.")

        section_label(inner, "Water tank", "The ultrasonic sensor sits on top of the tank and points down, so the fuller "
                                           "the tank, the shorter the distance. Enter what it reads when the tank is "
                                           "full (high level) and when it is empty (low level).")
        card = Card(inner, fill=BG2, outer=BG0)
        card.pack(fill="x", padx=16)
        self.tank = TankDiagram(card.inner, BG2)
        self.tank.pack(fill="x")
        separator(card.inner, bg=BG2)
        right = form_row(card.inner, "High level", sub="Distance when the tank is full", bg=BG2)
        self.high_entry = self._cm_entry(right, self.high_var)
        separator(card.inner, bg=BG2)
        right = form_row(card.inner, "Low level", sub="Distance when the tank is empty", bg=BG2)
        self.low_entry = self._cm_entry(right, self.low_var)
        self.tank_hint = tk.Label(inner, bg=BG0, fg=TXT3, font=font(10), anchor="w", justify="left", wraplength=500)
        self.tank_hint.pack(fill="x", padx=(32, 16), pady=(6, 0))

        section_label(inner, "Beeper")
        card = Card(inner, fill=BG2, outer=BG0)
        card.pack(fill="x", padx=16)
        right = form_row(card.inner, "Beeper", sub="Chimes and beeps of the controller", bg=BG2)
        self.beeper_switch = Switch(right, True, bg=BG2)   # on by default, like on the ESP32
        self.beeper_switch.pack()
        footer_label(inner, "Untick it and the controller never makes a sound: no boot chime, no irrigation beeps and "
                            "no connection chimes. The choice is saved in the ESP32's flash.")
        tk.Frame(inner, bg=BG0, height=28).pack()
        scroll.bind_wheel()
        for var in (self.lat_var, self.lon_var, self.tz_var):
            var.trace_add("write", lambda *_: self._refresh_readout())
        for var in (self.high_var, self.low_var):
            var.trace_add("write", lambda *_: self._check_tank())
        self._refresh_readout()
        self._check_tank()

    def build_actions(self, parent):
        """Upload and Download live in the top bar, right of the Home button (like in the timetable editor)."""
        Button(parent, text="Upload", style="link", command=self._write, bg=BG1).pack(side="left")
        Button(parent, text="Download", style="link", command=self._read, bg=BG1).pack(side="left")

    def on_show(self):
        if self.app.link.connected:
            self.after(100, self._read)   # show what is currently on the device

    # ----- map --------------------------------------------------------------------------------------------------------

    def _picked(self, lat: float, lon: float):
        """The user tapped the map: the coordinates and the time zone are solved from the pin."""
        lat, lon = round(lat, 4), round(lon, 4)
        result = geo.timezone_for(lat, lon)
        self.tz_name = result.name or ""
        self.lat_var.set(fmt_coord(lat))
        self.lon_var.set(fmt_coord(lon))
        self.tz_var.set(result.posix)
        self._refresh_readout()
        if result.note:
            self.app.say(result.note, warn=True)
        else:
            self.app.say(f"Location set, time zone {result.name or result.posix}")

    def _refresh_readout(self):
        tz = self.tz_var.get().strip()
        if tz and self.tz_name:
            tz = f"{tz} ({self.tz_name})"
        self.readout.configure(text=f"Latitude: {self.lat_var.get().strip() or 'Not set'}\n"
                                    f"Longitude: {self.lon_var.get().strip() or 'Not set'}\n"
                                    f"Time zone: {tz or 'Not set'}")

    # ----- water tank -------------------------------------------------------------------------------------------------

    @staticmethod
    def _cm_entry(parent, var):
        entry = RoundEntry(parent, var, width=84, justify="right", bg=BG2)
        entry.pack(side="left")
        tk.Label(parent, text="cm", bg=BG2, fg=TXT2, font=font(12)).pack(side="left", padx=(8, 0))
        return entry

    @staticmethod
    def _parse_cm(text):
        try:
            value = float(text.strip().replace(",", "."))
        except ValueError:
            return None
        return value if math.isfinite(value) else None

    def _check_tank(self):
        """Inline validation while typing: the fields at fault get a red ring and the hint under the card says why.
        The diagram only follows values that are valid."""
        texts = {"high": self.high_var.get(), "low": self.low_var.get()}
        names = {"high": "High level", "low": "Low level"}
        values = {key: self._parse_cm(text) for key, text in texts.items()}
        bad, message = set(), ""
        for key in ("high", "low"):
            if values[key] is None:
                bad.add(key)
                message = message or (f"Enter the {names[key].lower()} in centimeters" if not texts[key].strip()
                                      else f"{names[key]} must be a number")
        if not bad:
            problem = check_tank(values["high"], values["low"])
            if problem:
                bad, message = set(problem[0]), problem[1]
        self.high_entry.set_invalid("high" in bad)
        self.low_entry.set_invalid("low" in bad)
        if bad:
            self.tank_hint.configure(text=message, fg=WARN)
        else:
            self.tank_hint.configure(text="Below 10 % (the red zone) the error LED on the controller stays on.", fg=TXT3)
            self.tank.set_levels(values["high"], values["low"])

    # ----- ESP32 ------------------------------------------------------------------------------------------------------

    def _read(self):
        self.app.run_async("Downloading settings from the ESP32...", self.app.link.download_config, self._loaded)

    def _loaded(self, cfg: DeviceConfig):
        self.app.say("Downloaded the settings from the ESP32")
        self.tz_name = ""
        self.key_var.set(cfg.api_key)
        self.lat_var.set(fmt_coord(cfg.lat))
        self.lon_var.set(fmt_coord(cfg.lon))
        self.tz_var.set(cfg.timezone)
        self.map.set_location(cfg.lat, cfg.lon)
        self.high_var.set(fmt_num(cfg.tank_high))
        self.low_var.set(fmt_num(cfg.tank_low))
        self.beeper_switch.set(cfg.beeper)

    def _write(self):
        try:
            if not self.lat_var.get().strip() or not self.lon_var.get().strip():
                raise ValueError("Choose a location on the map first")
            cfg = DeviceConfig(
                api_key=self.key_var.get().strip(),
                lat=self._number(self.lat_var.get(), "Latitude"),
                lon=self._number(self.lon_var.get(), "Longitude"),
                timezone=self.tz_var.get().strip(),
                tank_high=self._number(self.high_var.get(), "High level"),
                tank_low=self._number(self.low_var.get(), "Low level"),
                beeper=self.beeper_switch.value,
            )
            cfg.validate()
        except ValueError as exc:
            self.app.alert("Invalid settings", str(exc))
            return
        self.app.run_async("Uploading settings to the ESP32...", lambda: self.app.link.upload_config(cfg),
                           lambda _: self.app.say("Uploaded the settings to the ESP32"))

    @staticmethod
    def _number(text: str, name: str) -> float:
        try:
            return float(text.strip().replace(",", "."))
        except ValueError:
            raise ValueError(f"{name} must be a number") from None
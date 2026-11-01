"""Everything that used to be a pop-up window now lives inside the main window.

Sheet     modal bottom sheet (not movable), slides up from the bottom edge and leaves the same way
Alert     centred iOS style alert with stacked or side by side buttons
Popover   dropdown menu that grows out of the button that opened it
Wheel     Clock app style scroll wheel with momentum
"""
import math
import time
import tkinter as tk
from tkinter import font as tkfont

from model import DAY_NAMES, PARAMS, Event, fmt_num
from serial_link import SerialError, list_port_info
from ui import (ACCENT, BG0, BG1, BG2, BG3, CAN_CLIP, LINE, TXT, TXT2, TXT3, WARN, WHITE, Animation, Button, Card,
                Chip, ListRow, ScrollFrame, Spring, Switch, clip_round, draw_icon, font, footer_label, form_row,
                make_entry, mix, rrect, rrect_points, section_label, separator)


def _decel_project(velocity: float, rate: float = 0.998) -> float:
    """Where a flick will come to rest (same units as velocity per second)."""
    return (velocity / 1000.0) * rate / (1.0 - rate)


# ----- bottom sheet -----------------------------------------------------------------------------------------------------
class Sheet(tk.Canvas):
    """Modal sheet centred in the main window. It rises from the bottom edge of the window on a critically damped
    spring and leaves the same way (enter and exit use the same path). A click outside it cancels."""
    R = 18
    HEAD = 54
    ENTER_RESPONSE = 0.42
    EXIT_RESPONSE = 0.34

    def __init__(self, app, title, width=560, height=680, left="Cancel", right=None, scrim_dismiss=True,
                 on_done=None):
        rw, rh = app.winfo_width(), app.winfo_height()
        self.app, self.on_done, self.scrim_dismiss = app, on_done, scrim_dismiss
        self.w, self.h = min(width, rw - 24), min(height, rh - 60)
        super().__init__(app, width=self.w, height=self.h, bg=BG0, highlightthickness=0)
        self._closing = False
        self._bg = rrect(self, 0, 0, self.w, self.h, self.R, fill=BG1, outline=mix(BG1, WHITE, .14))
        self.create_line(0, self.HEAD, self.w, self.HEAD, fill=LINE)
        self.create_text(self.w / 2, self.HEAD / 2, text=title, fill=TXT, font=font(14, "bold"))
        left_btn = Button(self, text=left, style="link", command=self.cancel, bg=BG1)
        self.create_window(12, self.HEAD / 2 + 1, window=left_btn, anchor="w")
        if right:
            self.right_btn = Button(self, text=right, style="link", command=self.confirm, bg=BG1)
            self.create_window(self.w - 12, self.HEAD / 2 + 1, window=self.right_btn, anchor="e")
        self.body = ScrollFrame(self, bg=BG1)
        # the body runs all the way to the bottom edge: the rounded window clips its corners
        self._body_win = self.create_window(0, self.HEAD + 1, window=self.body, anchor="nw", width=self.w,
                                            height=self.h - self.HEAD - 1)
        self.banner = tk.Label(self, bg=BG2, fg=WARN, font=font(11, "bold"), pady=7, wraplength=self.w - 60)
        self._banner_win = self.create_window(self.w / 2, self.HEAD + 8, window=self.banner, anchor="n",
                                              width=self.w - 32, state="hidden")
        self.bind("<Button-1>", self._click)

        self._spring = Spring(self._offscreen(), 1.0, self.ENTER_RESPONSE)
        self._anim = Animation(self, self._spring, self._slide, eps=0.5)
        self.place(relx=.5, rely=.5, anchor="center", y=int(self._spring.x))   # parked below the window
        clip_round(self, self.w, self.h, self.R)
        app.push_modal(self)
        self.after_idle(self._enter)   # start once the content has been built and laid out

    # ----- motion -----

    def _offscreen(self) -> float:
        """Vertical offset (from the centred position) at which the sheet is just below the window."""
        return (self.app.winfo_height() + self.h) / 2

    def _slide(self, y):
        try:
            self.place_configure(y=int(y))
        except tk.TclError:
            return
        if self._closing and y >= self._offscreen() - 1:
            self.destroy()

    def _enter(self):
        if self._closing:
            return
        self._spring.x, self._spring.v = self._offscreen(), 0.0
        self.place_configure(y=int(self._spring.x))
        self._anim.go(0.0, damping=1.0, response=self.ENTER_RESPONSE)

    def fit_height(self):
        """Shrink the sheet to its content (never taller than the window allows). Call after the body is built."""
        self.update_idletasks()
        need = self.HEAD + 1 + self.body.inner.winfo_reqheight()
        self.h = min(need, self.app.winfo_height() - 60)
        self.configure(height=self.h)
        self.coords(self._bg, *rrect_points(0, 0, self.w, self.h, self.R))
        self.itemconfigure(self._body_win, height=self.h - self.HEAD - 1)
        self._spring.x = self._offscreen()
        self.place_configure(y=int(self._spring.x))
        clip_round(self, self.w, self.h, self.R)

    # ----- content helpers -----

    def build_done(self):
        """Call once the body is filled so the mouse wheel scrolls it."""
        self.body.bind_wheel()

    def show_error(self, text):
        self.banner.configure(text=text)
        self.itemconfigure(self._banner_win, state="normal")

    def clear_error(self):
        self.itemconfigure(self._banner_win, state="hidden")

    def _click(self, e):
        if self.scrim_dismiss and not (0 <= e.x < self.w and 0 <= e.y < self.h):
            self.cancel()

    def confirm(self):
        self.close(True)

    def cancel(self):
        self.close(None)

    def close(self, result):
        if self._closing:
            return
        self._closing = True
        self.app.pop_modal(self)
        if self.on_done:
            self.on_done(result)
        # aim a little past the window edge so it is still moving at full speed when it disappears
        self._anim.go(self._offscreen() + 120, damping=1.0, response=self.EXIT_RESPONSE)


# ----- alert ------------------------------------------------------------------------------------------------------------
class Alert(tk.Canvas):
    """buttons: list of (label, value, style) with style 'default', 'cancel' or 'destructive'."""
    W = 300
    FILL = BG3                    # same material as the File menu, so it stands out from the sheets (BG1 / BG2)
    SEP = mix(BG3, WHITE, .16)    # hairlines that stay visible on that fill

    def __init__(self, app, title, message, buttons, on_done):
        super().__init__(app, width=self.W, height=120, bg=BG0, highlightthickness=0)
        self.app, self.on_done, self.buttons = app, on_done, buttons
        self.cancel_value = next((v for _, v, s in buttons if s == "cancel"), buttons[-1][1])
        W = self.W
        t = self.create_text(W / 2, 0, text=title, width=W - 40, font=font(14, "bold"), anchor="n",
                             justify="center", fill=TXT)
        m = self.create_text(W / 2, 0, text=message, width=W - 40, font=font(12), anchor="n", justify="center",
                             fill=TXT2) if message else None

        def height(item):
            b = self.bbox(item)
            return b[3] - b[1]

        y = 20
        self.coords(t, W / 2, y)
        y += height(t) + 6
        if m:
            self.coords(m, W / 2, y)
            y += height(m)
        y += 20
        row_h = 44
        side = len(buttons) == 2
        total = y + (row_h if side else row_h * len(buttons))
        self.configure(height=total)
        bg = rrect(self, 0, 0, W, total, 16, fill=self.FILL, outline=mix(self.FILL, WHITE, .12))
        self.tag_lower(bg)
        self.regions = []
        for i, (label, value, style) in enumerate(buttons):
            if side:
                box = (i * W / 2, y, (i + 1) * W / 2, y + row_h)
            else:
                box = (0, y + i * row_h, W, y + (i + 1) * row_h)
            self.regions.append((box, value))
            fg = {"destructive": WARN, "cancel": TXT2}.get(style, TXT)
            self.create_text((box[0] + box[2]) / 2, (box[1] + box[3]) / 2, text=label, fill=fg,
                             font=font(13, "normal" if style == "cancel" else "bold"))
            if not side and i:
                self.create_line(0, box[1], W, box[1], fill=self.SEP)
        self.create_line(0, y, W, y, fill=self.SEP)
        if side:
            self.create_line(W / 2, y, W / 2, total, fill=self.SEP)
        self._hot = None
        self.bind("<ButtonPress-1>", self._press)
        self.bind("<B1-Motion>", self._move)
        self.bind("<ButtonRelease-1>", self._release)

        self._spring = Spring(-18.0, 1.0, 0.3)
        self._anim = Animation(self, self._spring, lambda y: self.place_configure(y=int(y)), eps=0.4)
        self.place(relx=.5, rely=.5, anchor="center", y=-18)
        clip_round(self, W, total, 16)
        app.push_modal(self)
        self._anim.go(0.0)

    def _region_at(self, e):
        for i, (box, _v) in enumerate(self.regions):
            if box[0] <= e.x < box[2] and box[1] <= e.y < box[3]:
                return i
        return None

    def _press(self, e):
        self._hot = self._region_at(e)
        self._paint()

    def _move(self, e):
        self._paint(self._region_at(e) == self._hot)

    def _paint(self, active=True):
        self.delete("hl")
        if self._hot is not None and active:
            x0, y0, x1, y1 = self.regions[self._hot][0]
            item = rrect(self, x0 + 4, y0 + 4, x1 - 4, y1 - 4, 10, fill=mix(self.FILL, WHITE, .12), tags="hl")
            items = self.find_all()
            self.tag_lower(item, items[1])   # above the card, below the labels

    def _release(self, e):
        idx = self._region_at(e)
        fire = idx is not None and idx == self._hot
        self._hot = None
        self._paint()
        if fire:
            self.close(self.regions[idx][1])

    def cancel(self):
        self.close(self.cancel_value)

    def close(self, value):
        self.app.pop_modal(self)
        self.on_done(value)
        self.destroy()


# ----- popover menu -----------------------------------------------------------------------------------------------------
class Popover(tk.Canvas):
    ROW = 38

    def __init__(self, app, anchor, items, width=210):
        """items: (label, command) tuples or None for a separator."""
        self.app, self.items, self.w = app, items, width
        self.layout = []
        y = 8
        for item in items:
            if item is None:
                self.layout.append((y, y + 9, None))
                y += 9
            else:
                self.layout.append((y, y + self.ROW, item))
                y += self.ROW
        self.full_h = y + 8
        super().__init__(app, width=width, height=self.full_h, bg=BG0, highlightthickness=0)
        self._hot = None
        self._closing = False
        self._draw()
        self.bind("<Motion>", self._motion)
        self.bind("<ButtonPress-1>", self._press)
        self.bind("<ButtonRelease-1>", self._release)
        self.bind("<Leave>", lambda e: self._set_hot(None))
        x = anchor.winfo_rootx() - app.winfo_rootx()
        top = anchor.winfo_rooty() - app.winfo_rooty() + anchor.winfo_height() + 6
        self._spring = Spring(0.0, 1.0, 0.3)   # grows out of the button, from its top left corner
        self._anim = Animation(self, self._spring, self._grow, eps=0.5)
        self.place(x=x, y=top, width=width, height=1)
        app.push_modal(self)
        self._anim.go(float(self.full_h))

    def _grow(self, h):
        h = max(1, int(h))
        self.place_configure(height=h)
        clip_round(self, self.w, h, 14)

    def _draw(self):
        self.delete("all")
        rrect(self, 0, 0, self.w, self.full_h, 14, fill=BG3, outline=mix(BG3, WHITE, .12))
        for y0, y1, item in self.layout:
            if item is None:
                self.create_line(14, (y0 + y1) / 2, self.w - 14, (y0 + y1) / 2, fill=LINE)
                continue
            if self._hot is item:
                rrect(self, 6, y0 + 1, self.w - 6, y1 - 1, 8, fill=mix(BG3, WHITE, .12))
            self.create_text(18, (y0 + y1) / 2, text=item[0], anchor="w", fill=TXT, font=font(12))

    def _item_at(self, y):
        for y0, y1, item in self.layout:
            if item is not None and y0 <= y < y1:
                return item
        return None

    def _set_hot(self, item):
        if item is not self._hot:
            self._hot = item
            self._draw()

    def _motion(self, e):
        self._set_hot(self._item_at(e.y))

    def _press(self, e):
        if not (0 <= e.x < self.w and 0 <= e.y < self.full_h):
            self.cancel()
            return
        self._set_hot(self._item_at(e.y))

    def _release(self, e):
        item = self._item_at(e.y)
        if item is not None and item is self._hot and 0 <= e.x < self.w:
            self.app.pop_modal(self)
            self.destroy()
            self.app.after(10, item[1])

    def cancel(self):
        if self._closing:
            return
        self._closing = True
        self.app.pop_modal(self)
        self._anim.go(0.0, done=self.destroy)   # folds back into the button


# ----- wheel picker -----------------------------------------------------------------------------------------------------
class Wheel(tk.Canvas):
    """Looping scroll wheel. Tracks the pointer 1:1, throws with momentum and settles on a value on a spring."""
    ROW = 36
    ROWS = 5
    owns_wheel = True

    def __init__(self, master, count, value=0, bg=BG2, width=64):
        super().__init__(master, width=width, height=self.ROW * self.ROWS, bg=bg, highlightthickness=0,
                         cursor="sb_v_double_arrow")
        self.count, self.bgc, self.wpx = count, bg, width
        self.offset = float(value)
        self._target = int(value)
        self._spring = Spring(self.offset, 0.85, 0.35)
        self._anim = Animation(self, self._spring, self._set_offset, eps=0.003)
        self._hist = []
        self._drag = None
        self.bind("<ButtonPress-1>", self._press)
        self.bind("<B1-Motion>", self._move)
        self.bind("<ButtonRelease-1>", self._release)
        self.bind("<MouseWheel>", self._wheel)
        self.bind("<Button-4>", self._wheel)
        self.bind("<Button-5>", self._wheel)
        self._draw()

    @property
    def value(self) -> int:
        return int(round(self.offset)) % self.count

    def _set_offset(self, x):
        self.offset = x
        self._draw()

    def _press(self, e):
        self._anim.stop()               # grabbing the wheel stops it where it is
        self._spring.v = 0.0
        self._drag = (e.y, self.offset)
        self._hist = [(time.perf_counter(), self.offset)]
        self._moved = False

    def _move(self, e):
        if self._drag is None:
            return
        y0, base = self._drag
        if abs(e.y - y0) > 3:
            self._moved = True
        self.offset = base - (e.y - y0) / self.ROW
        now = time.perf_counter()
        self._hist.append((now, self.offset))
        self._hist = [h for h in self._hist if now - h[0] < 0.12]
        self._draw()

    def _release(self, e):
        if self._drag is None:
            return
        self._drag = None
        self._spring.x = self.offset
        if not self._moved:   # a tap on a row brings it to the middle
            rows = round((e.y - self.ROW * self.ROWS / 2) / self.ROW)
            self._target = int(round(self.offset)) + rows
            self._anim.go(self._target, velocity=0.0, damping=1.0)
            return
        now = time.perf_counter()
        v = 0.0
        if self._hist and now - self._hist[-1][0] < 0.08 and len(self._hist) > 1:
            t0, o0 = self._hist[0]
            t1, o1 = self._hist[-1]
            if t1 - t0 > 0.01:
                v = (o1 - o0) / (t1 - t0)   # rows per second
        self._target = int(round(self.offset + _decel_project(v)))
        self._anim.go(self._target, velocity=v, damping=0.85 if abs(v) > 2 else 1.0)

    def _wheel(self, e):
        step = -1 if (getattr(e, "num", 0) == 4 or e.delta > 0) else 1
        self._spring.x = self.offset
        base = self._target if self._anim.running else int(round(self.offset))
        self._target = base + step
        self._anim.go(self._target, damping=1.0)
        return "break"

    def _draw(self):
        self.delete("all")
        w, h = self.wpx, self.ROW * self.ROWS
        cy = h / 2
        rrect(self, 2, cy - self.ROW / 2 + 2, w - 2, cy + self.ROW / 2 - 2, 10, fill=BG3)
        base = math.floor(self.offset)
        for k in range(-3, 5):
            idx = base + k
            d = idx - self.offset
            a = abs(d)
            if a > 2.6:
                continue
            color = mix(TXT, self.bgc, min(1.0, a / 2.5))
            size, weight = (22, "bold") if a < 0.5 else (18, "normal")
            self.create_text(w / 2, cy + d * self.ROW, text=f"{idx % self.count:02d}", fill=color,
                             font=font(size, weight))


class CondRow(tk.Frame):
    """One start condition. The - button arms the row: the content slides left and the button asks 'Are you sure?'."""
    H = 46

    def __init__(self, master, name, unit, lo, hi, owner, index):
        super().__init__(master, bg=BG2, height=self.H)
        self.owner, self.index, self.armed = owner, index, False
        self.content = tk.Frame(self, bg=BG2)
        self.content.place(x=0, y=0, height=self.H, width=300)
        top = tk.Frame(self.content, bg=BG2)
        top.pack(side="left", padx=(16, 8))
        tk.Label(top, text=name, bg=BG2, fg=TXT, font=font(12)).pack(side="left")
        tk.Label(top, text=f" [{unit}]", bg=BG2, fg=TXT3, font=font(10)).pack(side="left")
        make_entry(self.content, hi, 7, "center").pack(side="right", padx=(6, 6), ipady=4)
        tk.Label(self.content, text="to", bg=BG2, fg=TXT3, font=font(10)).pack(side="right")
        make_entry(self.content, lo, 7, "center").pack(side="right", padx=(0, 6), ipady=4)
        self.btn = Button(self, text="\u2212", style="warnlink", command=self._tapped, bg=BG2, pad=12)
        self.btn.place(relx=1.0, x=-8, rely=.5, anchor="e")
        self.normal_w = int(self.btn["width"])
        self._spring = Spring(0.0, 1.0, 0.3)
        self._slide = Animation(self, self._spring, lambda x: self.content.place_configure(x=int(x)), eps=0.5)
        self.bind("<Configure>", lambda e: self.content.place_configure(width=max(100, e.width - self.normal_w - 16)))

    def _tapped(self):
        if self.armed:
            self.owner.remove(self)
        else:
            self.owner.arm(self)

    def arm(self):
        self.armed = True
        self.btn.set_text("Are you sure?")
        self._slide.go(-(int(self.btn["width"]) - self.normal_w))

    def disarm(self):
        self.armed = False
        self.btn.set_text("\u2212")
        self._slide.go(0.0)

    def collapse(self, done):
        spring = Spring(float(self.H), 1.0, 0.28)
        self._collapse = Animation(self, spring, lambda v: self.configure(height=max(1, int(v))), eps=0.8)
        self._collapse.go(0.0, done=done)


# ----- event editor -----------------------------------------------------------------------------------------------------
# Cards without an inset (see ui.Card) need a little air around free-form content such as chips and wheels
PAD = 8 if CAN_CLIP else 4


class EventSheet(Sheet):
    """Edits a copy of an event. on_done receives an Event, 'delete' or None (cancelled)."""

    def __init__(self, app, event: Event, on_done):
        super().__init__(app, "Irrigation event", width=560, height=720, left="Cancel", right="Save", on_done=on_done)
        inner = self.body.inner

        def card():
            c = Card(inner, fill=BG2, outer=BG1)
            c.pack(fill="x", padx=16)
            return c

        section_label(inner, "Days")
        c = card()
        row = tk.Frame(c.inner, bg=BG2)
        row.pack(fill="x", padx=6, pady=PAD)
        self.day_chips = []
        for i, name in enumerate(DAY_NAMES):
            row.columnconfigure(i, weight=1)
            chip = Chip(row, name[:3], event.days[i], width=58, height=46, bg=BG2, size=10)
            chip.grid(row=0, column=i)
            self.day_chips.append(chip)

        section_label(inner, "Time")
        c = card()
        cols = tk.Frame(c.inner, bg=BG2)
        cols.pack(fill="x", pady=(0, PAD))
        self.wheels = {}
        for k, (label, minutes) in enumerate((("Start", event.start), ("End", event.end % 1440))):
            col = tk.Frame(cols, bg=BG2)
            col.grid(row=0, column=k)
            cols.columnconfigure(k, weight=1)
            tk.Label(col, text=label, bg=BG2, fg=TXT2, font=font(11, "bold")).pack(pady=(4, 0))
            pair = tk.Frame(col, bg=BG2)
            pair.pack()
            hours = Wheel(pair, 24, minutes // 60)
            hours.pack(side="left")
            tk.Label(pair, text=":", bg=BG2, fg=TXT, font=font(20, "bold")).pack(side="left", padx=2)
            mins = Wheel(pair, 60, minutes % 60)
            mins.pack(side="left")
            self.wheels[label] = (hours, mins)
        footer_label(inner, "An end before the start crosses midnight. An end of 00:00 means midnight.")

        section_label(inner, "Zones")
        c = card()
        row = tk.Frame(c.inner, bg=BG2)
        row.pack(fill="x", padx=10, pady=PAD)
        self.zone_chips = []
        for i in range(4):
            row.columnconfigure(i, weight=1)
            chip = Chip(row, f"Zone {i + 1}", event.zones[i], width=106, height=38, circle=False, bg=BG2)
            chip.grid(row=0, column=i)
            self.zone_chips.append(chip)

        section_label(inner, "Data quality")
        c = card()
        self.rel = Switch(form_row(c.inner, "Only run if all data is reliable",
                                   sub="Sensors, current weather and forecast"), event.reliable, bg=BG2)
        self.rel.pack()

        section_label(inner, "Start conditions", "The event only runs while every condition below is inside its range.")
        self.cond_card = Card(inner, fill=BG2, outer=BG1)
        self.cond_card.pack(fill="x", padx=16)
        self.cond_vars = {}   # parameter index --> (min var, max var); parameters not in here are ignored
        for i, cur in enumerate(event.conds):
            if cur is not None:
                self.cond_vars[i] = (tk.StringVar(value=fmt_num(cur[0])), tk.StringVar(value=fmt_num(cur[1])))
        self._armed = None
        self._outside_bind = None
        self._render_conditions()

        tk.Frame(inner, bg=BG1, height=20).pack()
        c = card()
        ListRow(c.inner, "Delete event", command=lambda: self.close("delete"), fg=WARN, center=True).pack(fill="x")
        tk.Frame(inner, bg=BG1, height=16).pack()   # same margin as on the left and right
        self.build_done()

    # ----- start conditions -----

    DEFAULT_RANGE = {"%": (0, 100), "C": (-40, 60), "hPa": (800, 1100), "m/s": (0, 50), "mm": (0, 100)}

    def _render_conditions(self):
        self._disarm()
        box = self.cond_card.inner
        for child in box.winfo_children():
            child.destroy()
        for n, i in enumerate(sorted(self.cond_vars)):
            if n:
                separator(box)
            lo, hi = self.cond_vars[i]
            CondRow(box, PARAMS[i][0], PARAMS[i][1], lo, hi, self, i).pack(fill="x")
        if not self.cond_vars:
            tk.Label(box, text="No conditions. The event runs whenever its time comes.", bg=BG2, fg=TXT2,
                     font=font(11), anchor="w").pack(fill="x", padx=16, pady=(14, 14) if CAN_CLIP else (12, 4))
        if len(self.cond_vars) < len(PARAMS):
            if self.cond_vars:
                separator(box)
            row = tk.Frame(box, bg=BG2, height=46)
            row.pack(fill="x")
            row.pack_propagate(False)
            Button(row, text="Add condition", trailing="plus", style="link", command=self._add_condition,
                   bg=BG2).pack(side="left", padx=4, pady=7)
        self.build_done()

    def _add_condition(self):
        remaining = [i for i in range(len(PARAMS)) if i not in self.cond_vars]
        names = [f"{PARAMS[i][0]}  ({PARAMS[i][1]})" for i in remaining]

        def chosen(k):
            if k is None:
                return
            i = remaining[k]
            lo, hi = self.DEFAULT_RANGE.get(PARAMS[i][1], (0, 100))
            self.cond_vars[i] = (tk.StringVar(value=fmt_num(lo)), tk.StringVar(value=fmt_num(hi)))
            self._render_conditions()

        PickerSheet(self.app, "Add condition", names, None, chosen)

    def arm(self, row):
        if self._armed is not None and self._armed is not row:
            self._armed.disarm()
        self._armed = row
        row.arm()
        if self._outside_bind is None:
            self._outside_bind = self.bind_all("<ButtonPress-1>", self._outside, add="+")

    def _outside(self, e):
        if self._armed is not None and e.widget is not self._armed.btn:
            self._disarm()

    def _disarm(self):
        if self._armed is not None:
            try:
                self._armed.disarm()
            except tk.TclError:
                pass
        self._armed = None

    def remove(self, row):
        self._armed = None
        index = row.index
        row.collapse(lambda: (self.cond_vars.pop(index, None), self._render_conditions()))

    def close(self, result):
        if self._outside_bind is not None:
            self.unbind_all("<ButtonPress-1>")
            self._outside_bind = None
        super().close(result)

    def confirm(self):
        self.clear_error()
        try:
            sh, sm = self.wheels["Start"]
            eh, em = self.wheels["End"]
            conds = []
            for i, (name, _) in enumerate(PARAMS):
                if i in self.cond_vars:
                    lo, hi = self.cond_vars[i]
                    try:
                        conds.append((float(lo.get()), float(hi.get())))
                    except ValueError:
                        raise ValueError(f"{name}: min and max must be numbers") from None
                else:
                    conds.append(None)   # not added --> ignored
            ev = Event(
                days=[c.value for c in self.day_chips],
                start=sh.value * 60 + sm.value,
                end=eh.value * 60 + em.value,
                zones=[c.value for c in self.zone_chips],
                reliable=self.rel.value,
                conds=conds,
            )
            if ev.end == 0:
                ev.end = 1440
            ev.validate()
        except ValueError as exc:
            self.show_error(str(exc))
            return
        self.close(ev)


# ----- serial port sheet ------------------------------------------------------------------------------------------------
class PortSheet(Sheet):
    """Manual port choice and the auto connect switch. app.port_changed(disconnected) is called afterwards."""

    def __init__(self, app):
        super().__init__(app, "Serial port", width=520, height=600, left="Close", right="Connect",
                         scrim_dismiss=True)
        self.link = app.link
        self.ports = []
        self.sel = None
        inner = self.body.inner

        section_label(inner, "Connection")
        c = Card(inner, fill=BG2, outer=BG1)
        c.pack(fill="x", padx=16)
        right = form_row(c.inner, "Auto connect", sub="Search all serial ports for the controller")
        Switch(right, app.auto_var.get(), bg=BG2, command=self._auto).pack()

        section_label(inner, "Available ports")
        self.list_card = Card(inner, fill=BG2, outer=BG1)
        self.list_card.pack(fill="x", padx=16)

        c = Card(inner, fill=BG2, outer=BG1)
        c.pack(fill="x", padx=16, pady=(16, 0))
        ListRow(c.inner, "Refresh ports", command=self.refresh).pack(fill="x")
        separator(c.inner)
        ListRow(c.inner, "Disconnect", command=self._disconnect,
                fg=WARN if self.link.connected else TXT3).pack(fill="x")
        tk.Frame(inner, bg=BG1, height=24).pack()
        self.refresh()

    def _auto(self, value):
        self.app.auto_var.set(value)
        self.app.on_auto_toggle()

    def refresh(self):
        self.ports = list_port_info()
        names = [dev for dev, _ in self.ports]
        if self.sel is None or self.sel >= len(names):
            self.sel = names.index(self.link.port) if self.link.port in names else (0 if names else None)
        self._render()

    def _render(self):
        for child in self.list_card.inner.winfo_children():
            child.destroy()
        if not self.ports:
            ListRow(self.list_card.inner, "No serial ports found", fg=TXT2).pack(fill="x")
        for i, (dev, desc) in enumerate(self.ports):
            if i:
                separator(self.list_card.inner)
            ListRow(self.list_card.inner, dev, sub=desc, selected=(i == self.sel),
                    command=lambda i=i: self._select(i)).pack(fill="x")
        self.build_done()

    def _select(self, i):
        self.sel = i
        self._render()

    def confirm(self):
        self.clear_error()
        if self.sel is None:
            self.show_error("No port selected.")
            return
        try:
            self.link.connect(self.ports[self.sel][0])
        except SerialError as exc:
            self.show_error(str(exc))
            return
        self.app.port_changed(False)
        self.close(True)

    def _disconnect(self):
        if not self.link.connected:
            return
        self.link.disconnect()
        self.app.port_changed(True)
        self.close(True)


class PickerSheet(Sheet):
    """Pick one entry from a list. on_done receives the index or None."""

    def __init__(self, app, title, options, selected, on_done):
        super().__init__(app, title, width=520, height=700, left="Cancel", on_done=on_done, scrim_dismiss=True)
        c = Card(self.body.inner, fill=BG2, outer=BG1)
        c.pack(fill="x", padx=16, pady=(20, 20))
        for i, name in enumerate(options):
            if i:
                separator(c.inner)
            ListRow(c.inner, name, selected=(i == selected), command=lambda i=i: self.close(i)).pack(fill="x")
        self.fit_height()
        self.build_done()
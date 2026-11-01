"""Week grid: days on the X axis, time on the Y axis (top to bottom), styled like Apple Calendar."""
import datetime
import tkinter as tk

from model import DAY_NAMES, MAX_EVENTS, Event, fmt_clock
from ui import (ACCENT, BG0, BG1, BG2, LINE, TXT, TXT2, WHITE, Scrollbar, SmoothScroll, draw_icon, font, rrect)

HOUR_H = 48       # pixels per hour
TOP = 12          # space above 00:00 so its label fits
AXIS_W = 64       # width of the time column
HEADER_H = 40     # height of the day title row
SNAP = 15         # minutes
EDGE = 7          # pixels of the draggable top/bottom edge
MIN_LEN = 15      # minimum event length in minutes
STEP = 16         # how far an overlapping event is moved in so the bar of the one below stays visible
SIDE_MIN = 30     # events starting within this many minutes of each other sit side by side
TOTAL_H = 24 * HOUR_H + 2 * TOP


def event_summary(ev: Event) -> str:
    zones = ",".join(str(i + 1) for i, z in enumerate(ev.zones) if z)
    return f"Zone {zones}, {fmt_clock(ev.start)} - {fmt_clock(ev.end)}"


class TimetableView(tk.Frame):
    def __init__(self, master, events, on_edit, on_change, on_warn):
        super().__init__(master, bg=BG0)
        self.events = events          # shared list, edited in place
        self.on_edit = on_edit        # called with an Event (double click on it)
        self.on_change = on_change    # called after any modification, with a short description of what happened
        self.on_warn = on_warn        # called with (title, message)
        self.selected = None
        self.col_w = 100.0
        self.segments = []            # (x0, y0, x1, y1, event, kind)
        self.drag = None              # (event, 'top' | 'bottom')
        self.dragged = False
        self._scrolled = False
        self._today = datetime.datetime.now().weekday()

        self.corner = tk.Canvas(self, width=AXIS_W, height=HEADER_H, highlightthickness=0, bg=BG0)
        self.header = tk.Canvas(self, height=HEADER_H, highlightthickness=0, bg=BG0)
        self.axis = tk.Canvas(self, width=AXIS_W, highlightthickness=0, bg=BG0, scrollregion=(0, 0, AXIS_W, TOTAL_H))
        self.canvas = tk.Canvas(self, bg=BG0, highlightthickness=0)
        self.vbar = Scrollbar(self, command=self.canvas.yview, bg=BG0)
        self.canvas.configure(yscrollcommand=self._on_scroll)
        self.scroller = SmoothScroll(self.canvas, lambda: TOTAL_H)

        self.corner.grid(row=0, column=0, sticky="nsew")
        self.header.grid(row=0, column=1, sticky="ew")
        self.axis.grid(row=1, column=0, sticky="ns")
        self.canvas.grid(row=1, column=1, sticky="nsew")
        self.vbar.grid(row=1, column=2, sticky="ns")
        self.columnconfigure(1, weight=1)
        self.rowconfigure(1, weight=1)
        self.corner.create_line(0, HEADER_H - 1, AXIS_W, HEADER_H - 1, fill=LINE)

        self.canvas.bind("<Configure>", self._on_resize)
        self.canvas.bind("<Button-1>", self._on_press)
        self.canvas.bind("<B1-Motion>", self._on_drag)
        self.canvas.bind("<ButtonRelease-1>", self._on_release)
        self.canvas.bind("<Motion>", self._on_hover)
        self.canvas.bind("<Double-Button-1>", self._on_double)
        self.canvas.bind("<Delete>", self._on_delete)
        self.canvas.bind("<BackSpace>", self._on_delete)
        for seq in ("<MouseWheel>", "<Button-4>", "<Button-5>"):
            self.canvas.bind(seq, self.scroller.wheel)
            self.axis.bind(seq, self.scroller.wheel)

        self._draw_axis()
        self.after(30000, self._tick)

    # ----- geometry --------------------------------------------------------------------------------------------------

    @staticmethod
    def _y(minutes) -> float:
        return TOP + minutes / 60 * HOUR_H

    @staticmethod
    def _now_minutes() -> float:
        n = datetime.datetime.now()
        return n.hour * 60 + n.minute + n.second / 60

    # ----- scrolling and layout --------------------------------------------------------------------------------------

    def _on_scroll(self, first, last):
        self.vbar.set(first, last)
        self.axis.yview_moveto(first)

    def _on_resize(self, e):
        self.col_w = max(40.0, e.width / 7)
        self.canvas.configure(scrollregion=(0, 0, e.width, TOTAL_H))
        self._draw_header()
        self.redraw()
        if not self._scrolled and e.height > 1:
            self._scrolled = True   # first time: bring the current time into view
            y = max(0.0, min(self._y(self._now_minutes()) - e.height / 3, TOTAL_H - e.height))
            self.scroller.jump(y)

    def _tick(self):
        try:
            if datetime.datetime.now().weekday() != self._today:
                self._today = datetime.datetime.now().weekday()
                self._draw_header()
                self.redraw()
            else:
                self._draw_now()
                self._draw_axis()
            self.after(30000, self._tick)
        except tk.TclError:
            pass

    # ----- drawing ---------------------------------------------------------------------------------------------------

    def _draw_axis(self):
        a = self.axis
        a.delete("all")
        now_y = self._y(self._now_minutes())
        for h in range(24):
            y = self._y(h * 60)
            if abs(y - now_y) < 12:
                continue   # the current time label takes its place
            a.create_text(AXIS_W - 10, y, text=f"{h:02d}:00", anchor="e", fill=TXT2, font=font(8))
        n = datetime.datetime.now()
        rrect(a, 8, now_y - 9, AXIS_W - 6, now_y + 9, 9, fill=ACCENT)
        a.create_text((8 + AXIS_W - 6) / 2, now_y, text=f"{n.hour:02d}:{n.minute:02d}", fill=WHITE, font=font(8, "bold"))

    def _draw_header(self):
        h = self.header
        h.delete("all")
        short = self.col_w < 95
        for d, name in enumerate(DAY_NAMES):
            cx = (d + 0.5) * self.col_w
            label = name[:3] if short else name
            if d == self._today:
                width = min(self.col_w - 8, len(label) * 8 + 26)
                rrect(h, cx - width / 2, HEADER_H / 2 - 13, cx + width / 2, HEADER_H / 2 + 13, 13, fill=ACCENT)
                h.create_text(cx, HEADER_H / 2, text=label, fill=WHITE, font=font(11, "bold"))
            else:
                h.create_text(cx, HEADER_H / 2, text=label, fill=WHITE, font=font(11))
        h.create_line(0, HEADER_H - 1, self.col_w * 7, HEADER_H - 1, fill=LINE)

    def _compute_segments(self):
        """Apple Calendar style layout. Events that start together share the width side by side; an event that
        starts while another one is running is moved in from the left, so the bar of the one below stays visible.
        Returns (day, start, end, event, kind, depth, column, columns), lowest layer first."""
        per_day = [[] for _ in range(7)]
        for ev in self.events:
            for d in range(7):
                if not ev.days[d]:
                    continue
                if ev.wrapped:
                    per_day[d].append((ev.start, 1440, ev, "head"))
                    per_day[(d + 1) % 7].append((0, ev.end, ev, "tail"))
                else:
                    per_day[d].append((ev.start, ev.end, ev, "full"))
        out = []
        for d, segs in enumerate(per_day):
            segs.sort(key=lambda s: (s[0], -(s[1] - s[0])))
            groups = []
            for s in segs:
                g = groups[-1] if groups else None
                if g and s[0] - g["start"] < SIDE_MIN and s[0] < g["end"]:
                    g["members"].append(s)
                    g["end"] = max(g["end"], s[1])
                else:
                    groups.append({"start": s[0], "end": s[1], "members": [s], "depth": 0})
            for i, g in enumerate(groups):
                g["depth"] = max([h["depth"] + 1 for h in groups[:i] if h["end"] > g["start"]], default=0)
                for k, (m0, m1, ev, kind) in enumerate(g["members"]):
                    out.append((d, m0, m1, ev, kind, g["depth"], k, len(g["members"])))
        return out

    @staticmethod
    def _trim(text, px):
        n = max(3, int(px / 6.3))
        return text if len(text) <= n else text[:max(1, n - 3)] + "..."

    def _draw_label(self, ev, x0, y0, x1, y1, sel):
        h = y1 - y0
        if h < 16:
            return
        c = self.canvas
        zones = ",".join(str(i + 1) for i, z in enumerate(ev.zones) if z)
        times = f"{fmt_clock(ev.start)} - {fmt_clock(ev.end)}" + ("  Reliable" if ev.reliable else "")
        px = x1 - x0 - 18
        if h >= 36:
            c.create_text(x0 + 14, y0 + 5, text=self._trim(f"Zone {zones}", px), anchor="nw",
                          fill=WHITE if sel else TXT, font=font(9, "bold"))
            c.create_text(x0 + 14, y0 + 20, text=self._trim(times, px), anchor="nw",
                          fill=WHITE if sel else TXT2, font=font(9))
        else:
            c.create_text(x0 + 14, y0 + 3, text=self._trim(f"Zone {zones}  {fmt_clock(ev.start)}", px), anchor="nw",
                          fill=WHITE if sel else TXT, font=font(9, "bold"))

    def _draw_now(self):
        c = self.canvas
        c.delete("now")
        y = self._y(self._now_minutes())
        d = datetime.datetime.now().weekday()
        c.create_line(d * self.col_w, y, (d + 1) * self.col_w, y, fill=ACCENT, width=2, tags="now")
        c.create_oval(d * self.col_w - 4, y - 4, d * self.col_w + 4, y + 4, fill=ACCENT, outline="", tags="now")
        c.tag_raise("now")

    def redraw(self):
        c = self.canvas
        c.delete("all")
        w = self.col_w * 7
        c.create_rectangle(self._today * self.col_w, 0, (self._today + 1) * self.col_w, TOTAL_H, fill=BG1, outline="")
        for h in range(25):
            y = self._y(h * 60)
            c.create_line(0, y, w, y, fill=LINE)
        for d in range(8):
            c.create_line(d * self.col_w, 0, d * self.col_w, TOTAL_H, fill=LINE)

        self.segments = []
        for d, m0, m1, ev, kind, depth, k, n in self._compute_segments():
            left = d * self.col_w + 3 + min(depth * STEP, self.col_w * 0.45)
            each = ((d + 1) * self.col_w - 3 - left) / n
            x0, x1 = left + k * each + (1 if k else 0), left + (k + 1) * each - (1 if k < n - 1 else 0)
            y0, y1 = self._y(m0) + 1, self._y(m1) - 1
            sel = ev is self.selected
            r = min(8, (y1 - y0) / 2)
            rrect(c, x0, y0, x1, y1, r, fill=ACCENT if sel else BG2, outline=TXT if sel else BG0)
            # the bar sits inside the body with a margin on all sides, like Apple Calendar
            inset = 3 if y1 - y0 < 22 else 4
            rrect(c, x0 + inset, y0 + inset, x0 + inset + 4, y1 - inset, 2, fill=TXT if sel else ACCENT)
            self._draw_label(ev, x0, y0, x1, y1, sel)
            if sel:   # grab handles, like Apple Calendar (an event over midnight: start on its first day, end on its second)
                handles = {"full": ((x1 - 16, y0), (x0 + 16, y1)), "head": ((x1 - 16, y0),), "tail": ((x0 + 16, y1),)}
                for hx, hy in handles[kind]:
                    c.create_oval(hx - 4, hy - 4, hx + 4, hy + 4, fill=TXT, outline=BG0)
            self.segments.append((x0, y0, x1, y1, ev, kind))
        self._draw_now()
        self._draw_axis()

    # ----- hit testing and interaction -------------------------------------------------------------------------------

    def _pos(self, e):
        return self.canvas.canvasx(e.x), self.canvas.canvasy(e.y)

    def _hit(self, x, y):
        for x0, y0, x1, y1, ev, kind in reversed(self.segments):
            if x0 <= x <= x1 and y0 <= y <= y1:
                if kind in ("full", "head") and y - y0 < EDGE:
                    return ev, "top"      # the start of the event
                if kind in ("full", "tail") and y1 - y < EDGE:
                    return ev, "bottom"   # the end of the event
                return ev, "body"
        return None, None

    @staticmethod
    def _snap(y):
        return max(0, min(1440, int(round((y - TOP) / HOUR_H * 60 / SNAP)) * SNAP))

    def _on_press(self, e):
        self.canvas.focus_set()
        ev, part = self._hit(*self._pos(e))
        self.selected = ev
        self.drag = (ev, part) if part in ("top", "bottom") else None
        self.dragged = False
        self.redraw()

    def _on_drag(self, e):
        if not self.drag:
            return
        ev, part = self.drag
        m = self._snap(self._pos(e)[1])
        if ev.wrapped:
            # Over midnight: the start sits in the first day's column, the end in the next day's column.
            # Each edge may move freely as long as the event keeps crossing midnight.
            if part == "top":
                start, end = max(ev.end + MIN_LEN, min(m, 1440 - MIN_LEN)), ev.end
            else:
                start, end = ev.start, max(MIN_LEN, min(m, ev.start - MIN_LEN))
        elif part == "top":
            start, end = max(0, min(m, ev.end - MIN_LEN)), ev.end
        else:
            start, end = ev.start, min(1440, max(m, ev.start + MIN_LEN))
        if start == 0 and end == 1440:
            return   # the firmware cannot represent a 24 h event
        if (start, end) != (ev.start, ev.end):
            ev.start, ev.end = start, end
            self.dragged = True
            self.redraw()

    def _on_release(self, e):
        if self.drag and self.dragged:
            self.on_change(f"Changed event to {event_summary(self.drag[0])}")
        self.drag = None

    def _on_hover(self, e):
        _, part = self._hit(*self._pos(e))
        self.canvas.configure(cursor="sb_v_double_arrow" if part in ("top", "bottom") else "")

    def _on_double(self, e):
        x, y = self._pos(e)
        ev, _ = self._hit(x, y)
        if ev is not None:
            self.on_edit(ev)
            return
        if len(self.events) >= MAX_EVENTS:
            self.on_warn("Limit reached", f"The firmware stores at most {MAX_EVENTS} events.")
            return
        day = max(0, min(6, int(x // self.col_w)))
        start = min(max(int((y - TOP) // HOUR_H), 0) * 60, 23 * 60)
        new = Event(start=start, end=start + 60)
        new.days[day] = True
        self.events.append(new)
        self.selected = new
        self.on_change(f"Added event on {DAY_NAMES[day]} at {fmt_clock(start)}")
        self.redraw()

    def _on_delete(self, e):
        if self.selected is not None:
            self.events[:] = [ev for ev in self.events if ev is not self.selected]
            self.selected = None
            self.on_change("Deleted event")
            self.redraw()
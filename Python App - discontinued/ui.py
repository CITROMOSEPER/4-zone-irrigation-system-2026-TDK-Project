"""Look and feel of the editor: palette, fonts, spring animation and the small widgets every screen shares.

Everything is drawn on plain Tk canvases so the whole app follows one dark, Apple style design:
  * one accent colour, one warning colour, everything else is a shade of Apple's dark mode grays
  * feedback on press (not on release), springs that can be interrupted, no fixed-duration animations
"""
import math
import sys
import time
import tkinter as tk
from tkinter import font as tkfont

# ----- palette ----------------------------------------------------------------------------------------------------------
ACCENT = "#0f612f"
WARN = "#c42b1c"
BG0 = "#000000"    # app background
BG1 = "#1c1c1e"    # bars and sheets
BG2 = "#2c2c2e"    # grouped cells
BG3 = "#3a3a3c"    # controls and fills
LINE = "#38383a"   # hairlines
TXT = "#f2f2f7"
TXT2 = "#98989f"
TXT3 = "#636366"
WHITE = "#ffffff"


def mix(a: str, b: str, t: float) -> str:
    """Blend colour a towards colour b (t = 0 gives a, t = 1 gives b)."""
    pa = [int(a[i:i + 2], 16) for i in (1, 3, 5)]
    pb = [int(b[i:i + 2], 16) for i in (1, 3, 5)]
    return "#%02x%02x%02x" % tuple(round(x + (y - x) * t) for x, y in zip(pa, pb))


# ----- fonts ------------------------------------------------------------------------------------------------------------
_families = {}


def font(size: int, weight: str = "normal", mono: bool = False):
    """The platform's own UI font (it ships the optical sizing and tracking tuning)."""
    key = "mono" if mono else "ui"
    if key not in _families:
        _families[key] = tkfont.nametofont("TkFixedFont" if mono else "TkDefaultFont").actual("family")
    return (_families[key], size, weight) if weight != "normal" else (_families[key], size)


# ----- motion -----------------------------------------------------------------------------------------------------------
def _reduced_motion() -> bool:
    if sys.platform.startswith("win"):
        try:
            import ctypes
            flag = ctypes.c_int(1)
            if ctypes.windll.user32.SystemParametersInfoW(0x1042, 0, ctypes.byref(flag), 0):   # SPI_GETCLIENTAREAANIMATION
                return not flag.value
        except Exception:
            pass
    return False


REDUCED_MOTION = _reduced_motion()


class Spring:
    """Damped spring described by a damping ratio (1.0 = no overshoot) and a response time in seconds."""

    def __init__(self, value=0.0, damping=1.0, response=0.35):
        self.x = value
        self.v = 0.0
        self.target = value
        self.damping = damping
        self.response = response

    def step(self, dt: float) -> None:
        k = (2 * math.pi / self.response) ** 2
        c = 2 * self.damping * math.sqrt(k)
        n = max(1, int(math.ceil(dt * 240)))
        h = dt / n
        for _ in range(n):
            self.v += (k * (self.target - self.x) - c * self.v) * h
            self.x += self.v * h

    def settled(self, eps: float) -> bool:
        return abs(self.target - self.x) < eps and abs(self.v) < eps * 10


class Animation:
    """Runs a Spring from the Tk event loop. It can be re-targeted at any moment and continues from the live value
    and velocity, so nothing ever jumps. With reduced motion switched on, values snap to their target."""

    def __init__(self, widget, spring: Spring, apply, eps=0.001):
        self.w = widget
        self.s = spring
        self.apply = apply
        self.eps = eps
        self.running = False
        self._job = None
        self._last = 0.0
        self._done = None

    def go(self, target, velocity=None, damping=None, response=None, done=None) -> None:
        s = self.s
        s.target = target
        if velocity is not None:
            s.v = velocity
        if damping is not None:
            s.damping = damping
        if response is not None:
            s.response = response
        self._done = done
        if REDUCED_MOTION:
            self.stop()
            s.x, s.v = target, 0.0
            self.apply(s.x)
            if done:
                done()
            return
        self.running = True
        if self._job is None:
            self._last = time.perf_counter()
            self._job = self.w.after(1, self._tick)

    def stop(self) -> None:
        if self._job is not None:
            try:
                self.w.after_cancel(self._job)
            except tk.TclError:
                pass
        self._job = None
        self.running = False

    def _tick(self) -> None:
        self._job = None
        try:
            if not self.w.winfo_exists():
                return
            now = time.perf_counter()
            dt = min(now - self._last, 0.05)
            self._last = now
            s = self.s
            s.step(dt)
            if s.settled(self.eps):
                s.x, s.v = s.target, 0.0
                self.running = False
                self.apply(s.x)
                done, self._done = self._done, None
                if done:
                    done()
                return
            self.apply(s.x)
            self._job = self.w.after(8, self._tick)
        except tk.TclError:
            self.running = False


def wheel_px(e) -> float:
    """Mouse wheel event to a scroll distance in pixels (positive = down)."""
    if getattr(e, "num", 0) == 4:
        return -48.0
    if getattr(e, "num", 0) == 5:
        return 48.0
    return -e.delta / 120 * 48 if abs(e.delta) >= 120 else -e.delta * 6.0


class SmoothScroll:
    """Wheel scrolling for a canvas that glides on a spring instead of jumping."""

    def __init__(self, canvas: tk.Canvas, extent):
        self.c = canvas
        self.extent = extent   # callable: total scrollable height in pixels
        self.spring = Spring(0.0, 1.0, 0.2)
        self.anim = Animation(canvas, self.spring, self._apply, eps=0.3)

    def _apply(self, y: float) -> None:
        total = self.extent()
        self.c.yview_moveto(y / total if total else 0.0)

    def wheel(self, e):
        total = self.extent()
        maxy = max(0.0, total - self.c.winfo_height())
        current = self.c.canvasy(0)
        base = self.spring.target if self.anim.running else current
        self.spring.x = current   # always continue from what is on screen
        self.anim.go(min(max(base + wheel_px(e), 0.0), maxy))
        return "break"

    def jump(self, y: float) -> None:
        self.anim.stop()
        self._apply(y)


# ----- drawing helpers --------------------------------------------------------------------------------------------------
CAN_CLIP = sys.platform.startswith("win")   # real rounded clipping of child windows is available


def clip_round(widget, width: int, height: int, radius: float) -> bool:
    """Cut the corners off a widget (and everything inside it) so what is behind it shows through.
    Tk has no transparency, so a canvas that only *draws* a rounded rectangle keeps square, opaque corners.
    On Windows every Tk widget is a real child window, which can be given a rounded region.
    Returns False where that is not available (the widget then simply keeps its square corners)."""
    if not CAN_CLIP:
        return False
    try:
        import ctypes
        from ctypes import wintypes
        gdi32, user32 = ctypes.windll.gdi32, ctypes.windll.user32
        gdi32.CreateRoundRectRgn.argtypes = [ctypes.c_int] * 6
        gdi32.CreateRoundRectRgn.restype = wintypes.HRGN
        user32.SetWindowRgn.argtypes = [wintypes.HWND, wintypes.HRGN, wintypes.BOOL]
        d = int(round(radius * 2))
        region = gdi32.CreateRoundRectRgn(0, 0, int(width) + 1, int(height) + 1, d, d)
        user32.SetWindowRgn(widget.winfo_id(), region, True)   # the system owns the region from here on
        return True
    except Exception:
        return False
def rrect_points(x0, y0, x1, y1, r):
    """Rounded rectangle outline built from real circular arcs, so the radius is the same on both axes."""
    r = max(0.0, min(r, (x1 - x0) / 2, (y1 - y0) / 2))
    if r < 1:
        return [x0, y0, x1, y0, x1, y1, x0, y1]
    steps = max(4, min(14, int(r * 0.7)))
    pts = []
    for cx, cy, start in ((x1 - r, y0 + r, -90), (x1 - r, y1 - r, 0), (x0 + r, y1 - r, 90), (x0 + r, y0 + r, 180)):
        for i in range(steps + 1):
            a = math.radians(start + 90 * i / steps)
            pts += [cx + r * math.cos(a), cy + r * math.sin(a)]
    return pts


def rrect(c: tk.Canvas, x0, y0, x1, y1, r, **kw):
    kw.setdefault("outline", "")
    return c.create_polygon(rrect_points(x0, y0, x1, y1, r), **kw)


def draw_icon(c: tk.Canvas, name: str, cx: float, cy: float, size: float, color: str, bg: str = BG0,
              tag: str = "icon", width=None) -> None:
    """Vector icons built from lines and polygons. `size` is the full height of the glyph; `bg` is the colour
    behind it (needed for the hole in the gear)."""
    s = size
    w = width or max(1.6, s * 0.075)
    line = dict(fill=color, width=w, capstyle="round", joinstyle="round", tags=tag)

    def poly(*pts):
        return [v for x, y in pts for v in (cx + x * s, cy + y * s)]

    if name == "calendar":
        c.create_polygon(rrect_points(cx - .5 * s, cy - .42 * s, cx + .5 * s, cy + .5 * s, .12 * s), smooth=False,
                         fill="", outline=color, width=w, tags=tag)
        c.create_line(cx - .5 * s, cy - .1 * s, cx + .5 * s, cy - .1 * s, **line)
        for dx in (-.22, .22):
            c.create_line(cx + dx * s, cy - .55 * s, cx + dx * s, cy - .3 * s, **line)
        for row in range(2):
            for col in range(3):
                x, y, r = cx + (col - 1) * .24 * s, cy + (.1 + row * .24) * s, .045 * s
                c.create_rectangle(x - r, y - r, x + r, y + r, fill=color, outline="", tags=tag)
    elif name == "monitor":
        c.create_polygon(rrect_points(cx - .52 * s, cy - .42 * s, cx + .52 * s, cy + .3 * s, .1 * s), smooth=False,
                         fill="", outline=color, width=w, tags=tag)
        c.create_line(cx - .2 * s, cy + .48 * s, cx + .2 * s, cy + .48 * s, **line)
        c.create_line(*poly((-.36, -.06), (-.18, -.06), (-.08, -.24), (.06, .1), (.16, -.12), (.2, -.06), (.36, -.06)),
                      **line)
    elif name == "gear":
        pts = []
        for i in range(8):
            a = math.radians(i * 45)
            for da, r in ((-15, .38), (-9, .5), (9, .5), (15, .38)):
                ang = a + math.radians(da)
                pts += [cx + math.cos(ang) * r * s, cy + math.sin(ang) * r * s]
        c.create_polygon(pts, fill=color, outline=color, width=w * .6, joinstyle="round", tags=tag)
        r = .17 * s
        c.create_oval(cx - r, cy - r, cx + r, cy + r, fill=bg, outline="", tags=tag)
    elif name == "up":
        c.create_line(*poly((0, .4), (0, -.4)), **line)
        c.create_line(*poly((-.3, -.1), (0, -.4), (.3, -.1)), **line)
    elif name == "down":
        c.create_line(*poly((0, -.4), (0, .4)), **line)
        c.create_line(*poly((-.3, .1), (0, .4), (.3, .1)), **line)
    elif name == "chevron_left":
        c.create_line(*poly((.15, -.4), (-.2, 0), (.15, .4)), **line)
    elif name == "chevron_right":
        c.create_line(*poly((-.15, -.4), (.2, 0), (-.15, .4)), **line)
    elif name == "chevron_down":
        c.create_line(*poly((-.4, -.15), (0, .25), (.4, -.15)), **line)
    elif name == "check":
        c.create_line(*poly((-.4, .02), (-.12, .3), (.4, -.3)), **line)
    elif name == "cross":
        c.create_line(*poly((-.32, -.32), (.32, .32)), **line)
        c.create_line(*poly((-.32, .32), (.32, -.32)), **line)
    elif name == "plus":
        c.create_line(*poly((-.4, 0), (.4, 0)), **line)
        c.create_line(*poly((0, -.4), (0, .4)), **line)
    elif name == "play":
        c.create_polygon(*poly((-.26, -.42), (.4, 0), (-.26, .42)), fill=color, outline=color, width=w * .6,
                         joinstyle="round", tags=tag)
    elif name == "pause":
        for dx in (-.2, .2):
            rrect(c, cx + (dx - .1) * s, cy - .4 * s, cx + (dx + .1) * s, cy + .4 * s, .05 * s, fill=color, tags=tag)
    elif name == "copy":
        c.create_polygon(rrect_points(cx - .1 * s, cy - .1 * s, cx + .45 * s, cy + .45 * s, .08 * s), smooth=False,
                         fill="", outline=color, width=w, tags=tag)
        c.create_line(*poly((-.2, .2), (-.45, .2), (-.45, -.45), (.1, -.45), (.1, -.2)), **line)
    elif name == "sun":
        r = .2 * s
        c.create_oval(cx - r, cy - r, cx + r, cy + r, fill=color, outline="", tags=tag)
        for i in range(8):
            a = math.radians(i * 45)
            c.create_line(cx + math.cos(a) * .32 * s, cy + math.sin(a) * .32 * s,
                          cx + math.cos(a) * .46 * s, cy + math.sin(a) * .46 * s, **line)
    elif name in ("cloud", "rain"):
        dy = -.1 * s if name == "rain" else 0
        for x0, y0, x1, y1 in ((-.5, -.02, -.1, .38), (-.3, -.3, .2, .2), (.02, -.12, .5, .38)):
            c.create_oval(cx + x0 * s, cy + dy + y0 * s, cx + x1 * s, cy + dy + y1 * s, fill=color, outline="", tags=tag)
        c.create_rectangle(cx - .3 * s, cy + dy + .06 * s, cx + .3 * s, cy + dy + .38 * s, fill=color, outline="", tags=tag)
        if name == "rain":
            for dx in (-.24, 0, .24):
                c.create_line(cx + dx * s, cy + .36 * s, cx + (dx - .06) * s, cy + .5 * s,
                              **dict(line, fill="#5ac8fa"))


# ----- buttons and controls ---------------------------------------------------------------------------------------------
class Button(tk.Canvas):
    """Capsule button. It reacts on press, fires on release, and can be cancelled by dragging away."""
    STYLES = {"plain": (BG3, TXT), "accent": (ACCENT, WHITE), "ghost": (None, TXT), "link": (None, ACCENT), "warnlink": (None, WARN), "danger": (BG3, WARN)}

    def __init__(self, master, text="", command=None, style="plain", icon=None, trailing=None, height=32,
                 size=12, bg=None, pad=14):
        bg = bg or master.cget("bg")
        isz = size + 2
        width = self._calc_width(text, icon, trailing, size, pad)
        super().__init__(master, width=width, height=height, bg=bg, highlightthickness=0, cursor="hand2")
        self.cmd, self.style, self.text, self.icon, self.trailing = command, style, text, icon, trailing
        self.size, self.pad, self.isz = size, pad, isz
        self.down = self.inside = self.hover = False
        self.bind("<ButtonPress-1>", self._press)
        self.bind("<ButtonRelease-1>", self._release)
        self.bind("<Enter>", self._enter)
        self.bind("<Leave>", self._leave)
        self._draw()

    @staticmethod
    def _calc_width(text, icon, trailing, size, pad):
        isz = size + 2
        width = pad * 2 + (tkfont.Font(font=font(size, "bold")).measure(text) if text else 0)
        width += (isz + (6 if text else 0)) if icon else 0
        width += (isz - 4 + 6) if trailing else 0
        return width

    def set_icon(self, icon):
        self.icon = icon
        self._draw()

    def set_text(self, text):
        self.text = text
        self.configure(width=self._calc_width(text, self.icon, self.trailing, self.size, self.pad))
        self._draw()

    def _draw(self):
        self.delete("all")
        w, h = int(self["width"]), int(self["height"])
        base, fg = self.STYLES[self.style]
        if base is None:
            base = self["bg"]
        if self.down and self.inside:
            fill = mix(base, BG0, .3) if self.style == "accent" else mix(base, WHITE, .16 if base != self["bg"] else .0)
            if base == self["bg"]:
                fill = BG3
        elif self.hover and not self.down:
            fill = BG2 if base == self["bg"] else mix(base, WHITE, .07)
        else:
            fill = base
        rrect(self, 0, 0, w, h, h / 2, fill=fill)
        x = self.pad
        if self.icon:
            draw_icon(self, self.icon, x + self.isz / 2, h / 2, self.isz, fg, bg=fill)
            x += self.isz + (6 if self.text else 0)
        if self.text:
            self.create_text(x, h / 2, text=self.text, anchor="w", fill=fg, font=font(self.size, "bold"))
            x += tkfont.Font(font=font(self.size, "bold")).measure(self.text) + 6
        if self.trailing:
            draw_icon(self, self.trailing, x + (self.isz - 4) / 2, h / 2 + 1, self.isz - 4, fg, bg=fill)

    def _press(self, _e):
        self.down = self.inside = True
        self._draw()

    def _enter(self, _e):
        self.hover = True
        if self.down:
            self.inside = True
        self._draw()

    def _leave(self, _e):
        self.hover = False
        if self.down:
            self.inside = False
        self._draw()

    def _release(self, _e):
        fire = self.down and self.inside
        self.down = False
        self._draw()
        if fire and self.cmd:
            self.cmd()


class Switch(tk.Canvas):
    W, H = 51, 31

    def __init__(self, master, value=False, command=None, bg=None):
        super().__init__(master, width=self.W, height=self.H, bg=bg or master.cget("bg"), highlightthickness=0,
                         cursor="hand2")
        self.value = bool(value)
        self.command = command
        self._spring = Spring(1.0 if self.value else 0.0, 1.0, 0.28)
        self._anim = Animation(self, self._spring, lambda _x: self._draw(), eps=0.002)
        self.bind("<ButtonPress-1>", self._press)   # flips on press, nothing changes while it is held
        self._draw()

    def set(self, value, notify=False):
        self.value = bool(value)
        self._anim.go(1.0 if self.value else 0.0)
        if notify and self.command:
            self.command(self.value)

    def _press(self, _e):
        self.set(not self.value, notify=True)

    def _draw(self):
        self.delete("all")
        t = min(1.0, max(0.0, self._spring.x))
        rrect(self, 0, 0, self.W, self.H, self.H / 2, fill=mix(BG3, ACCENT, t))
        d = self.H - 4
        x = 2 + t * (self.W - self.H)
        self.create_oval(x, 2, x + d, 2 + d, fill=WHITE, outline="")


class Segmented(tk.Canvas):
    """Segmented control with a thumb that slides on a spring."""

    def __init__(self, master, labels, index=0, command=None, seg_w=54, height=30, bg=None):
        self.labels, self.seg_w, self.command = labels, seg_w, command
        self.index = index
        super().__init__(master, width=seg_w * len(labels) + 4, height=height, bg=bg or master.cget("bg"),
                         highlightthickness=0, cursor="hand2")
        self._spring = Spring(float(index), 1.0, 0.3)
        self._anim = Animation(self, self._spring, lambda _x: self._draw(), eps=0.002)
        self.bind("<ButtonPress-1>", self._press)
        self._draw()

    def select(self, index, notify=False):
        self.index = index
        self._anim.go(float(index))
        if notify and self.command:
            self.command(index)

    def _press(self, e):
        self.select(min(len(self.labels) - 1, max(0, int((e.x - 2) // self.seg_w))), notify=True)

    def _draw(self):
        self.delete("all")
        w, h = int(self["width"]), int(self["height"])
        rrect(self, 0, 0, w, h, 10, fill=BG3)
        x = 2 + self._spring.x * self.seg_w
        rrect(self, x, 2, x + self.seg_w, h - 2, 8, fill=TXT3)
        for i, label in enumerate(self.labels):
            sel = abs(i - self._spring.x) < 0.5
            self.create_text(2 + (i + .5) * self.seg_w, h / 2, text=label, fill=TXT if sel else TXT2,
                             font=font(11, "bold" if sel else "normal"))


class Chip(tk.Canvas):
    """Round (or capsule) toggle, used for the days and zones. It flips on press and does not change while held."""

    def __init__(self, master, text, value=False, command=None, width=44, height=40, circle=True, bg=None, size=11):
        super().__init__(master, width=width, height=height, bg=bg or master.cget("bg"), highlightthickness=0,
                         cursor="hand2")
        self.text, self.value, self.command, self.circle, self.size = text, bool(value), command, circle, size
        self.bind("<ButtonPress-1>", self._press)
        self._draw()

    def _press(self, _e):
        self.value = not self.value
        self._draw()
        if self.command:
            self.command(self.value)

    def _draw(self):
        self.delete("all")
        w, h = int(self["width"]), int(self["height"])
        fill = ACCENT if self.value else BG3
        if self.circle:
            d = min(w, h) - 4
            self.create_oval((w - d) / 2, (h - d) / 2, (w + d) / 2, (h + d) / 2, fill=fill, outline="")
        else:
            rrect(self, 2, 2, w - 2, h - 2, (h - 4) / 2, fill=fill)
        self.create_text(w / 2, h / 2, text=self.text, fill=WHITE if self.value else TXT2, font=font(self.size, "bold"))


class Scrollbar(tk.Canvas):
    """Slim overlay scroll indicator, draggable."""

    def __init__(self, master, command=None, orient="vertical", thickness=10, bg=None):
        self.vertical = orient == "vertical"
        kw = {"width": thickness} if self.vertical else {"height": thickness}
        super().__init__(master, bg=bg or master.cget("bg"), highlightthickness=0, **kw)
        self.command, self.first, self.last, self.t = command, 0.0, 1.0, thickness
        self._grab = None
        self._hot = False
        self.bind("<Configure>", lambda e: self._draw())
        self.bind("<ButtonPress-1>", self._press)
        self.bind("<B1-Motion>", self._drag)
        self.bind("<ButtonRelease-1>", self._release)
        self.bind("<Enter>", lambda e: self._hover(True))
        self.bind("<Leave>", lambda e: self._hover(False))

    def set(self, first, last):
        self.first, self.last = float(first), float(last)
        self._draw()

    def _hover(self, hot):
        self._hot = hot
        self._draw()

    def _span(self):
        return max(1, self.winfo_height() if self.vertical else self.winfo_width())

    def _thumb(self):
        span = self._span()
        a, b = self.first * span, self.last * span
        if b - a < 28:
            b = a + 28
        return a, b

    def _draw(self):
        self.delete("all")
        if self.last - self.first >= 0.999:
            return
        a, b = self._thumb()
        t = self.t
        color = TXT2 if (self._hot or self._grab is not None) else TXT3
        if self.vertical:
            rrect(self, 2, a + 1, t - 2, b - 1, (t - 4) / 2, fill=color)
        else:
            rrect(self, a + 1, 2, b - 1, t - 2, (t - 4) / 2, fill=color)

    def _pos(self, e):
        return e.y if self.vertical else e.x

    def _press(self, e):
        a, b = self._thumb()
        p = self._pos(e)
        self._grab = p - a if a <= p <= b else (b - a) / 2
        if not a <= p <= b:
            self._drag(e)
        self._draw()

    def _drag(self, e):
        if self._grab is not None and self.command:
            self.command("moveto", (self._pos(e) - self._grab) / self._span())

    def _release(self, _e):
        self._grab = None
        self._draw()


class ScrollFrame(tk.Frame):
    """Vertically scrolling container (smooth wheel, overlay scroll indicator). Put content in `.inner`."""

    def __init__(self, master, bg, max_width=None):
        super().__init__(master, bg=bg)
        self.max_width = max_width
        self.canvas = tk.Canvas(self, bg=bg, highlightthickness=0)
        self.canvas.pack(fill="both", expand=True)
        self.bar = Scrollbar(self, command=self.canvas.yview, bg=bg)
        self.bar.place(relx=1.0, rely=0.0, relheight=1.0, anchor="ne")
        self.canvas.configure(yscrollcommand=self.bar.set)
        self.inner = tk.Frame(self.canvas, bg=bg)
        self.win = self.canvas.create_window(0, 0, window=self.inner, anchor="nw")
        self.scroller = SmoothScroll(self.canvas, lambda: self._total)
        self._total = 1
        self.canvas.bind("<Configure>", self._layout)
        self.inner.bind("<Configure>", self._layout)

    def _layout(self, _e=None):
        cw, ch = self.canvas.winfo_width(), self.canvas.winfo_height()
        width = min(self.max_width, cw) if self.max_width else cw
        self.canvas.coords(self.win, (cw - width) / 2, 0)
        self.canvas.itemconfigure(self.win, width=width)
        self._total = max(self.inner.winfo_reqheight(), ch)
        self.canvas.configure(scrollregion=(0, 0, cw, self._total))

    def bind_wheel(self, widget=None):
        """Call after the content is built: the wheel scrolls everywhere except on widgets that use it themselves."""
        widget = widget or self.inner
        if not getattr(widget, "owns_wheel", False):
            for seq in ("<MouseWheel>", "<Button-4>", "<Button-5>"):
                widget.bind(seq, self.scroller.wheel)
        for child in widget.winfo_children():
            self.bind_wheel(child)
        if widget is self.inner:
            for seq in ("<MouseWheel>", "<Button-4>", "<Button-5>"):
                self.canvas.bind(seq, self.scroller.wheel)


# ----- grouped form building blocks ------------------------------------------------------------------------------------
class Card(tk.Frame):
    """Rounded grouped cell container. Put rows in `.inner`.
    Where corners can be clipped for real (Windows) the rows run edge to edge, with no empty strip above the first
    row or below the last one. Elsewhere the rows are inset by the radius so they never poke out of the corners."""

    def __init__(self, master, fill=BG2, outer=None, radius=12):
        outer = outer or master.cget("bg")
        super().__init__(master, bg=outer)
        self.fill, self.r = fill, radius
        self._size = None
        self.back = tk.Canvas(self, bg=outer, highlightthickness=0)
        self.back.place(x=0, y=0, relwidth=1, relheight=1)
        self.inner = tk.Frame(self, bg=fill)
        self.inner.pack(fill="both", expand=True, pady=0 if CAN_CLIP else radius)
        self.bind("<Configure>", self._paint)

    def _paint(self, e):
        if (e.width, e.height) == self._size:
            return
        self._size = (e.width, e.height)
        self.back.delete("all")
        rrect(self.back, 0, 0, e.width, e.height, self.r, fill=self.fill)
        clip_round(self, e.width, e.height, self.r)


def section_label(master, text, hint=None):
    bg = master.cget("bg")
    tk.Label(master, text=text.upper(), bg=bg, fg=TXT2, font=font(10, "bold"), anchor="w").pack(
        fill="x", padx=(32, 16), pady=(20, 0 if hint else 6))
    if hint:
        tk.Label(master, text=hint, bg=bg, fg=TXT3, font=font(10), anchor="w", justify="left", wraplength=480).pack(
            fill="x", padx=(32, 16), pady=(2, 6))


def footer_label(master, text):
    tk.Label(master, text=text, bg=master.cget("bg"), fg=TXT3, font=font(10), anchor="w", justify="left",
             wraplength=500).pack(fill="x", padx=(32, 16), pady=(6, 0))


def separator(master, bg=BG2):
    row = tk.Frame(master, bg=bg, height=1)
    row.pack(fill="x")
    tk.Frame(row, bg=LINE, height=1).pack(fill="x", padx=(16, 0))


def form_row(master, text, sub=None, unit=None, height=46, bg=BG2):
    """A cell with a label on the left. Returns the frame on the right where controls go."""
    row = tk.Frame(master, bg=bg, height=height + (10 if sub else 0))
    row.pack(fill="x")
    row.pack_propagate(False)
    left = tk.Frame(row, bg=bg)
    left.pack(side="left", padx=(16, 8))
    top = tk.Frame(left, bg=bg)
    top.pack(anchor="w")
    tk.Label(top, text=text, bg=bg, fg=TXT, font=font(12)).pack(side="left")
    if unit:
        tk.Label(top, text=f" {unit}", bg=bg, fg=TXT3, font=font(10)).pack(side="left")
    if sub:
        tk.Label(left, text=sub, bg=bg, fg=TXT2, font=font(10)).pack(anchor="w")
    right = tk.Frame(row, bg=bg)
    right.pack(side="right", padx=(8, 16))
    return right


def make_entry(master, var, width=8, justify="left", show=None):
    return tk.Entry(master, textvariable=var, width=width, justify=justify, show=show or "", bg=BG3, fg=TXT,
                    insertbackground=TXT, relief="flat", bd=0, highlightthickness=1, highlightbackground=BG3,
                    highlightcolor=ACCENT, disabledbackground=BG2, disabledforeground=TXT3,
                    selectbackground=ACCENT, selectforeground=WHITE, font=font(12))


class RoundEntry(tk.Canvas):
    """Text field with rounded corners. The text is centred vertically; focus shows an accent ring, and
    `set_invalid(True)` shows a red one that stays (inline validation).
    `.entry` is the real tk.Entry, `.configure(show=...)` is passed on to it."""

    def __init__(self, master, var, width=310, height=30, justify="left", show=None, bg=None, fill=BG3, radius=9):
        super().__init__(master, width=width, height=height, bg=bg or master.cget("bg"), highlightthickness=0)
        self.shape = rrect(self, 1, 1, width - 1, height - 1, radius, fill=fill, outline="", width=2)
        self.entry = tk.Entry(self, textvariable=var, justify=justify, show=show or "", bg=fill, fg=TXT,
                              insertbackground=TXT, relief="flat", bd=0, highlightthickness=0,
                              selectbackground=ACCENT, selectforeground=WHITE, font=font(12))
        self.create_window(width / 2, height / 2, window=self.entry, width=width - 2 * 12, anchor="center")
        self.invalid = self.focused = False
        self.entry.bind("<FocusIn>", lambda _e: self._focus(True))
        self.entry.bind("<FocusOut>", lambda _e: self._focus(False))
        self.bind("<ButtonPress-1>", lambda _e: self.entry.focus_set())   # a tap on the rim focuses it too

    def _focus(self, focused):
        self.focused = focused
        self._ring()

    def _ring(self):
        self.itemconfigure(self.shape, outline=WARN if self.invalid else (ACCENT if self.focused else ""))

    def set_invalid(self, invalid):
        if invalid != self.invalid:
            self.invalid = invalid
            self._ring()

    def configure(self, cnf=None, **kw):
        if "show" in kw:
            self.entry.configure(show=kw.pop("show"))
        if cnf or kw:
            return super().configure(cnf, **kw)

    config = configure


class ListRow(tk.Canvas):
    """Tappable cell: title, optional subtitle, and a check mark, chevron or value on the right."""

    def __init__(self, master, text, sub=None, command=None, right=None, selected=False, fg=TXT, bg=BG2,
                 height=46, center=False):
        super().__init__(master, height=height + (10 if sub else 0), bg=bg, highlightthickness=0, cursor="hand2")
        self.text, self.sub, self.cmd, self.right, self.selected = text, sub, command, right, selected
        self.fg, self.rowbg, self.center = fg, bg, center
        self.down = self.inside = self.hover = False
        self.bind("<Configure>", lambda e: self._draw())
        self.bind("<ButtonPress-1>", self._press)
        self.bind("<ButtonRelease-1>", self._release)
        self.bind("<Enter>", self._enter)
        self.bind("<Leave>", self._leave)

    def set_right(self, text):
        self.right = text
        self._draw()

    def _draw(self):
        self.delete("all")
        w, h = self.winfo_width(), int(self["height"])
        fill = self.rowbg
        if self.down and self.inside:
            fill = BG3
        elif self.hover and not self.down:
            fill = mix(self.rowbg, BG3, .4)
        self.create_rectangle(0, 0, w, h, fill=fill, outline="")
        if self.center:
            self.create_text(w / 2, h / 2, text=self.text, fill=self.fg, font=font(12))
            return
        if self.sub:
            self.create_text(16, h / 2 - 8, text=self.text, anchor="w", fill=self.fg, font=font(12))
            self.create_text(16, h / 2 + 9, text=self.sub, anchor="w", fill=TXT2, font=font(10))
        else:
            self.create_text(16, h / 2, text=self.text, anchor="w", fill=self.fg, font=font(12))
        if self.selected:
            self.create_oval(w - 36, h / 2 - 10, w - 16, h / 2 + 10, fill=ACCENT, outline="")
            draw_icon(self, "check", w - 26, h / 2, 12, WHITE, bg=ACCENT, width=2)
        elif self.right == "chevron":
            draw_icon(self, "chevron_right", w - 22, h / 2, 14, TXT3, width=2)
        elif self.right:
            self.create_text(w - 38, h / 2, text=self.right, anchor="e", fill=TXT2, font=font(12))
            draw_icon(self, "chevron_right", w - 22, h / 2, 14, TXT3, width=2)

    def _press(self, _e):
        self.down = self.inside = True
        self._draw()

    def _enter(self, _e):
        self.hover = True
        if self.down:
            self.inside = True
        self._draw()

    def _leave(self, _e):
        self.hover = False
        if self.down:
            self.inside = False
        self._draw()

    def _release(self, _e):
        fire = self.down and self.inside
        self.down = False
        self._draw()
        if fire and self.cmd:
            self.cmd()
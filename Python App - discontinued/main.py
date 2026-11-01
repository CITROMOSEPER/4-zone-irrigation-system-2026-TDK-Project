"""Irrigation timetable editor. Run with:  python main.py   (needs: pip install pyserial)

The window icon is read from  textures/app_icon.ico  (next to where you start the program, or next to this file).
"""
import os
import queue
import threading
import time
import tkinter as tk
from tkinter import filedialog
from tkinter import font as tkfont

import model
from connection import Supervisor
from serial_link import SerialError, SerialLink
from sheets import Alert, EventSheet, PortSheet
from screens import HomeScreen, MonitorScreen, SettingsScreen, TimetableScreen
from timetable_view import event_summary
from ui import (ACCENT, BG0, BG1, BG2, BG3, LINE, TXT, TXT2, TXT3, WARN, WHITE, Animation, Button, Spring,
                draw_icon, font, mix, rrect)

FILE_TYPES = [("TimeTable", "*.TimeTable"), ("All files", "*.*")]
TITLES = {"timetable": "Timetable editor", "monitor": "Monitoring", "settings": "Device settings"}


class ConnPill(tk.Canvas):
    """Connection indicator (top right). Clicking it opens the serial port sheet."""

    def __init__(self, master, command):
        super().__init__(master, height=30, bg=BG1, highlightthickness=0, cursor="hand2")
        self.cmd, self.state, self.text = command, "idle", "Not connected"
        self.down = self.inside = False
        self.bind("<ButtonPress-1>", self._press)
        self.bind("<ButtonRelease-1>", self._release)
        self.bind("<Enter>", lambda e: setattr(self, "inside", True))
        self.bind("<Leave>", lambda e: setattr(self, "inside", False))
        self.set("idle", self.text)

    def set(self, state, text):
        self.state, self.text = state, text
        w = tkfont.Font(font=font(11, "bold")).measure(text) + 44
        self.configure(width=w)
        self._draw()

    def _draw(self):
        self.delete("all")
        w, h = int(self["width"]), 30
        fill, fg = {"connected": (ACCENT, WHITE), "lost": (WARN, WHITE)}.get(self.state, (BG3, TXT2))
        if self.down and self.inside:
            fill = mix(fill, BG0, .3) if self.state in ("connected", "lost") else mix(fill, WHITE, .16)
        rrect(self, 0, 0, w, h, h / 2, fill=fill)
        if self.state in ("connected", "lost"):
            self.create_oval(12, 11, 20, 19, fill=WHITE, outline="")
        elif self.state == "searching":
            self.create_oval(12, 11, 20, 19, fill="", outline=TXT2, width=2)
        else:
            self.create_oval(12, 11, 20, 19, fill=TXT3, outline="")
        self.create_text(28, h / 2, text=self.text, anchor="w", fill=fg, font=font(11, "bold"))

    def _press(self, _e):
        self.down = self.inside = True
        self._draw()

    def _release(self, _e):
        fire = self.down and self.inside
        self.down = False
        self._draw()
        if fire:
            self.cmd()


class Navigator:
    """Home sits at position 0, the open screen at position 1. One spring moves between them, so a tap on Back
    in the middle of the slide simply reverses it. The screen underneath moves slower, like on iOS."""

    def __init__(self, container, home):
        self.c, self.home, self.detail = container, home, None
        self.spring = Spring(0.0, 1.0, 0.42)
        self.anim = Animation(container, self.spring, self._apply, eps=0.0005)
        self._apply(0.0)

    def push(self, frame):
        if self.detail is not None and self.detail is not frame:
            self.detail.place_forget()
        self.detail = frame
        frame.lift()
        self.anim.go(1.0)

    def pop(self):
        self.anim.go(0.0)

    def _apply(self, s):
        w = self.c.winfo_width()
        self.home.place(x=-int(s * w * 0.3), y=0, relwidth=1, relheight=1)
        if self.detail is not None:
            if s <= 0.0005:
                self.detail.place_forget()
            else:
                self.detail.place(x=int((1 - s) * w), y=0, relwidth=1, relheight=1)
                self.detail.lift()


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.configure(bg=BG0)
        self.geometry("1000x780")
        self.minsize(780, 560)
        self._set_icon()
        self.events = []
        self.path = None
        self.dirty = False
        self.link = SerialLink()
        self.busy = False
        self.results = queue.Queue()
        self.link_events = queue.Queue()   # from the supervisor thread: ('connected', port) / ('lost', reason)
        self.auto_var = tk.BooleanVar(value=True)
        self.modals = []
        self.lost = False
        self._exiting = False
        self.msg_var = tk.StringVar(value="Ready")
        self.time_var = tk.StringVar()

        # Action reminder bar (bottom)
        bottom = tk.Frame(self, bg=BG1, height=34)
        bottom.pack(side="bottom", fill="x")
        bottom.pack_propagate(False)
        tk.Frame(self, bg=LINE, height=1).pack(side="bottom", fill="x")
        self.glyph = tk.Canvas(bottom, width=18, height=18, bg=BG1, highlightthickness=0)
        self.glyph.pack(side="left", padx=(16, 8))
        self.msg_lbl = tk.Label(bottom, textvariable=self.msg_var, bg=BG1, fg=ACCENT, font=font(11), anchor="w")
        self.msg_lbl.pack(side="left", fill="x", expand=True)
        tk.Label(bottom, textvariable=self.time_var, bg=BG1, fg=TXT3, font=font(10)).pack(side="right", padx=16)

        # Top bar: back button, page title, connection indicator
        top = tk.Frame(self, bg=BG1, height=52)
        top.pack(side="top", fill="x")
        top.pack_propagate(False)
        tk.Frame(self, bg=LINE, height=1).pack(side="top", fill="x")
        self.left = tk.Frame(top, bg=BG1)   # Home button plus the buttons of the open screen
        self.back_btn = Button(self.left, text="Home", icon="chevron_left", style="link", command=self.go_home, bg=BG1)
        self.back_btn.pack(side="left")
        self.actions = tk.Frame(self.left, bg=BG1)
        self.actions.pack(side="left")
        self.title_lbl = tk.Label(top, text="", bg=BG1, fg=TXT, font=font(14, "bold"))
        self.title_lbl.place(relx=.5, rely=.5, anchor="center")
        self.pill = ConnPill(top, self.choose_port)
        self.pill.place(relx=1.0, x=-16, rely=.5, anchor="e")

        # Screens
        container = tk.Frame(self, bg=BG0)
        container.pack(fill="both", expand=True)
        self.screens = {
            "timetable": TimetableScreen(container, self),
            "monitor": MonitorScreen(container, self),
            "settings": SettingsScreen(container, self),
        }
        self.view = self.screens["timetable"].view
        self.nav = Navigator(container, HomeScreen(container, self))
        self.current = None

        self.say("Ready")
        self.update_conn_label()
        self.update_title()
        self.protocol("WM_DELETE_WINDOW", self.on_exit)
        self.bind("<Escape>", self.on_escape)
        self.after(50, self.poll_results)

        self.supervisor = Supervisor(self.link, self.link_events)
        self.supervisor.start()

    def _set_icon(self):
        for base in (os.getcwd(), os.path.dirname(os.path.abspath(__file__))):
            path = os.path.join(base, "textures", "app_icon.ico")
            if not os.path.isfile(path):
                continue
            try:
                self.iconbitmap(default=path)   # applies to every window of the app (Windows)
                return
            except tk.TclError:
                try:                            # other platforms: needs Pillow
                    from PIL import Image, ImageTk
                    self._icon_image = ImageTk.PhotoImage(Image.open(path))
                    self.iconphoto(True, self._icon_image)
                    return
                except Exception:
                    pass

    # ----- navigation ------------------------------------------------------------------------------------------------

    def open_screen(self, name):
        if self.current is not None and hasattr(self.current, "on_hide"):
            self.current.on_hide()
        self.current = self.screens[name]
        self.title_lbl.configure(text=TITLES[name])
        for child in self.actions.winfo_children():
            child.destroy()
        if hasattr(self.current, "build_actions"):
            self.current.build_actions(self.actions)
        self.left.place(x=10, rely=.5, anchor="w")
        self.nav.push(self.current)
        if hasattr(self.current, "on_show"):
            self.current.on_show()

    def go_home(self):
        if self.current is not None and hasattr(self.current, "on_hide"):
            self.current.on_hide()
        self.current = None
        self.title_lbl.configure(text="")
        self.left.place_forget()
        self.nav.pop()

    # ----- modal layer -----------------------------------------------------------------------------------------------

    def push_modal(self, widget):
        self.modals.append(widget)
        tk.Misc.lift(widget)
        self._grab(widget)

    def pop_modal(self, widget):
        if widget in self.modals:
            self.modals.remove(widget)
        try:
            widget.grab_release()
        except tk.TclError:
            pass
        if self.modals:
            self._grab(self.modals[-1])

    def _grab(self, widget, tries=10):
        try:
            widget.grab_set()
            widget.focus_set()
        except tk.TclError:
            if tries and widget.winfo_exists():
                self.after(30, lambda: self._grab(widget, tries - 1))

    def on_escape(self, _e=None):
        if self.modals:
            self.modals[-1].cancel()

    def ask(self, title, message, buttons):
        """Blocking in-window alert. buttons: (label, value, style) tuples; returns the value of the pressed one."""
        result = []
        flag = tk.BooleanVar(value=False)

        def done(value):
            result.append(value)
            flag.set(True)

        Alert(self, title, message, buttons, done)
        self.wait_variable(flag)
        return result[0] if result else None

    def alert(self, title, message):
        self.ask(title, message, [("OK", True, "default")])

    # ----- state -----------------------------------------------------------------------------------------------------

    def say(self, text, warn=False):
        """Action reminder bar: confirms the last thing that happened."""
        self.msg_var.set(text)
        self.msg_lbl.configure(fg=WARN if warn else ACCENT)
        self.time_var.set(time.strftime("%H:%M:%S"))
        g = self.glyph
        g.delete("all")
        if warn:
            g.create_oval(1, 1, 17, 17, fill=WARN, outline="")
            g.create_text(9, 9, text="!", fill=WHITE, font=font(10, "bold"))
        else:
            g.create_oval(1, 1, 17, 17, fill=ACCENT, outline="")
            draw_icon(g, "check", 9, 9, 10, WHITE, bg=ACCENT, width=1.8)

    def update_title(self):
        name = os.path.basename(self.path) if self.path else "untitled"
        self.title(f"Timetable Editor - {name}{'*' if self.dirty else ''}")

    def mark_dirty(self, message=None):
        self.dirty = True
        self.update_title()
        if message:
            self.say(message)

    def update_conn_label(self):
        if self.link.connected:
            self.pill.set("connected", f"Connected {self.link.port}")
        elif self.lost:
            self.pill.set("lost", "Connection lost")
        elif self.auto_var.get():
            self.pill.set("searching", "Searching...")
        else:
            self.pill.set("idle", "Not connected")

    def confirm_discard(self):
        return not self.dirty or self.ask("Discard changes?", "Your unsaved changes to this timetable will be lost.",
                                          [("Cancel", False, "cancel"), ("Discard", True, "destructive")])

    def replace_events(self, events, path=None):
        self.events[:] = events
        self.view.selected = None
        self.view.redraw()
        self.path = path
        self.dirty = False
        self.update_title()

    # ----- file menu -------------------------------------------------------------------------------------------------

    def file_new(self):
        if self.confirm_discard():
            self.replace_events([])
            self.say("Started a new timetable")

    def file_open(self):
        if not self.confirm_discard():
            return
        path = filedialog.askopenfilename(filetypes=FILE_TYPES, parent=self)
        if not path:
            return
        try:
            events = model.load_file(path)
        except (OSError, ValueError) as exc:
            self.say("Open failed", warn=True)
            self.alert("Open failed", str(exc))
            return
        self.replace_events(events, path)
        self.say(f"Loaded {len(events)} events from {path}")

    def file_save(self):
        if self.path is None:
            return self.file_save_as()
        try:
            for e in self.events:
                e.validate()
            model.save_file(self.path, self.events)
        except (OSError, ValueError) as exc:
            self.say("Save failed", warn=True)
            self.alert("Save failed", str(exc))
            return False
        self.dirty = False
        self.update_title()
        self.say(f"Saved {len(self.events)} events to {self.path}")
        return True

    def file_save_as(self):
        path = filedialog.asksaveasfilename(filetypes=FILE_TYPES, defaultextension=".TimeTable", parent=self)
        if not path:
            return False
        self.path = path
        return self.file_save()

    def on_exit(self):
        if self._exiting:
            return
        self._exiting = True
        try:
            if self.confirm_discard():
                self.supervisor.halt()
                self.link.disconnect()
                self.destroy()
        finally:
            self._exiting = False

    # ----- editing ---------------------------------------------------------------------------------------------------

    def edit_event(self, event):
        def done(result):
            if result is None:
                return
            if result == "delete":
                self.events[:] = [e for e in self.events if e is not event]
                self.view.selected = None
                self.mark_dirty("Deleted event")
            else:
                for i, e in enumerate(self.events):
                    if e is event:
                        self.events[i] = result
                self.view.selected = result
                self.mark_dirty(f"Updated event: {event_summary(result)}")
            self.view.redraw()

        EventSheet(self, event, done)

    # ----- ESP32 -----------------------------------------------------------------------------------------------------

    def choose_port(self):
        if not any(isinstance(m, PortSheet) for m in self.modals):
            PortSheet(self)

    def port_changed(self, disconnected):
        self.lost = False
        if disconnected:
            # The user wants to be disconnected, so do not search again until Auto connect is switched on
            self.auto_var.set(False)
            self.supervisor.auto = False
            self.say("Disconnected. Switch Auto connect on to search for the controller again.")
        else:
            self.say(f"Connected to {self.link.port}")
        self.update_conn_label()

    def on_auto_toggle(self):
        self.supervisor.auto = self.auto_var.get()
        if self.supervisor.auto and not self.link.connected:
            self.say("Searching all serial ports for the controller...")
        else:
            self.say("Auto connect is on" if self.supervisor.auto else "Auto connect is off")
        self.update_conn_label()

    def poll_link_events(self):
        while True:
            try:
                kind, value = self.link_events.get_nowait()
            except queue.Empty:
                return
            if kind == "connected":
                self.lost = False
                self.say(f"Controller found and connected on {value}")
            elif kind == "lost":
                self.lost = True
                self.say(f"Connection lost ({value})", warn=True)
            self.update_conn_label()

    def run_async(self, text, work, done):
        """Run a blocking serial job in a thread so the window stays responsive."""
        if not self.link.connected:
            self.alert("Not connected", "Open the connection indicator in the top right corner and choose a COM port.")
            return
        if self.busy:
            return
        self.busy = True
        self.say(text)

        def job():
            try:
                self.results.put((done, work(), None))
            except SerialError as exc:
                self.results.put((done, None, exc))

        threading.Thread(target=job, daemon=True).start()

    def poll_results(self):
        self.after(50, self.poll_results)
        self.poll_link_events()
        try:
            done, value, error = self.results.get_nowait()
        except queue.Empty:
            return
        self.busy = False
        if error is not None:
            self.say("Transfer failed", warn=True)
            self.alert("Transfer failed", str(error))
        else:
            done(value)

    def upload(self):
        try:
            for e in self.events:
                e.validate()
        except ValueError as exc:
            self.say("Invalid timetable", warn=True)
            self.alert("Invalid timetable", str(exc))
            return
        events = [e.copy() for e in self.events]
        self.run_async("Uploading to the ESP32...", lambda: self.link.upload(events),
                       lambda _: self.say(f"Uploaded {len(events)} events to the ESP32"))

    def download(self):
        if self.events and not self.ask("Replace timetable?",
                                        "Replace the timetable in the editor with the one on the ESP32?",
                                        [("Cancel", False, "cancel"), ("Replace", True, "default")]):
            return

        def done(events):
            self.replace_events(events)
            self.mark_dirty()
            self.say(f"Downloaded {len(events)} events from the ESP32")

        self.run_async("Downloading from the ESP32...", self.link.download, done)


if __name__ == "__main__":
    App().mainloop()
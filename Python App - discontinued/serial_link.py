"""Serial link to the ESP32.

Protocol (one text line per command):
    ID                 --> "ID IRRIGATION-4ZONE <fw>"   identifies the controller (used by the port scan)
    HELLO / BYE        --> "OK HELLO" / "OK BYE"        the ESP32 plays the connect / disconnect chime
    PING               --> "PONG 1" or "PONG 0"         keep-alive (0 = the ESP32 lost the link state, HELLO again)
    PRINT              --> report block between two "====" lines
    TT GET / TT SET, CFG GET / CFG SET                  timetable and settings
"""
import threading
import time
from typing import List, Optional, Tuple

import serial
from serial.tools import list_ports

from model import DeviceConfig, Event, MAX_EVENTS, build_payload, parse_payload

BAUD = 921600
DEVICE_ID = "IRRIGATION-4ZONE"   # must match DEVICE_ID in the firmware
PROBE_WINDOW = 1.6               # seconds a port gets to answer the ID command


class SerialError(Exception):
    pass


class SerialTimeout(SerialError):
    """The port works, but the device did not answer in time."""


def list_port_info() -> List[Tuple[str, str]]:
    return [(p.device, p.description) for p in sorted(list_ports.comports())]


def _close_quietly(ser) -> None:
    try:
        ser.close()
    except (serial.SerialException, OSError):
        pass


class SerialLink:
    def __init__(self):
        self.ser = None
        self.port = ""
        self._rx = b""                       # incomplete line received so far
        self._lock = threading.Lock()        # one conversation with the device at a time
        self._open_lock = threading.Lock()   # connect() and port probing never open ports at the same time

    @property
    def connected(self) -> bool:
        return self.ser is not None and self.ser.is_open

    # ----- opening and closing ---------------------------------------------------------------------------------------

    @staticmethod
    def _open_port(port: str, timeout: float):
        ser = serial.Serial()
        ser.port = port
        ser.baudrate = BAUD
        ser.timeout = timeout
        ser.write_timeout = 0.5
        ser.dtr = False   # avoid resetting the ESP32 when the port opens
        ser.rts = False
        ser.open()
        return ser

    def connect(self, port: str) -> None:
        """Open a port chosen by hand (the device is not verified)."""
        with self._open_lock:
            self.disconnect()
            try:
                ser = self._open_port(port, 0.3)
            except (serial.SerialException, OSError, ValueError) as exc:
                raise SerialError(str(exc)) from None
            self.ser = ser
            self.port = port
        self._hello()

    def try_connect(self, port: str) -> bool:
        """Open `port`, ask it to identify itself and keep the connection only if it is our controller."""
        with self._open_lock:
            if self.connected:
                return False
            ser = None
            try:
                ser = self._open_port(port, 0.15)
                if not self._identify(ser):
                    _close_quietly(ser)
                    return False
            except (serial.SerialException, OSError, ValueError):
                if ser is not None:
                    _close_quietly(ser)
                return False
            ser.timeout = 0.3
            self.ser = ser
            self.port = port
        self._hello()
        return True

    @staticmethod
    def _identify(ser) -> bool:
        deadline = time.time() + PROBE_WINDOW
        next_send = 0.0
        ser.reset_input_buffer()
        buf = b""
        while time.time() < deadline:
            if time.time() >= next_send:
                ser.write(b"\nID\n")   # the leading newline throws away any half received line
                ser.flush()
                next_send = time.time() + 0.4   # the ESP32 may still be booting, so ask again
            buf += ser.readline()
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                if line.decode("ascii", errors="replace").strip().startswith("ID " + DEVICE_ID):
                    return True
        return False

    def _hello(self) -> None:
        """Quiet the autoprint and announce the connection (the ESP32 plays its chime). Best effort."""
        try:
            self._command("AUTOPRINT 0", lambda l: l.startswith(("OK AUTOPRINT", "ERR")), 0.8)
            self._command("HELLO", lambda l: l.startswith(("OK HELLO", "ERR")), 1.2)
        except SerialError:
            pass

    def disconnect(self, notify: bool = True) -> None:
        """Close the port. With notify the ESP32 is told first, so it plays the disconnect chime."""
        with self._lock:
            if self.ser is not None:
                try:
                    if notify and self.ser.is_open:
                        self.ser.write(b"BYE\n")
                        self.ser.flush()
                        time.sleep(0.05)
                except (serial.SerialException, OSError):
                    pass
                _close_quietly(self.ser)
            self.ser = None
            self.port = ""
            self._rx = b""

    # ----- low level -------------------------------------------------------------------------------------------------

    def _read_line(self) -> Optional[str]:
        """One complete line, or None when no complete line arrived within the port timeout."""
        raw = self.ser.readline()
        if raw:
            self._rx += raw
        if self._rx.endswith(b"\n"):
            line, self._rx = self._rx, b""
            return line.decode("ascii", errors="replace").strip()
        return None

    def _exchange(self, text: str, accept, timeout: float) -> str:
        """Send one line and wait for the first answer line that `accept` likes. The caller holds self._lock."""
        if not self.connected:
            raise SerialError("Not connected to a device")
        try:
            self.ser.reset_input_buffer()
            self._rx = b""
            self.ser.write((text + "\n").encode("ascii"))
            self.ser.flush()
            deadline = time.time() + timeout
            while time.time() < deadline:
                line = self._read_line()
                if line is not None and accept(line):
                    return line
        except (serial.SerialException, OSError) as exc:
            raise SerialError(str(exc)) from None
        raise SerialTimeout("The device did not answer in time")

    def _command(self, text: str, accept, timeout: float) -> str:
        with self._lock:
            return self._exchange(text, accept, timeout)

    # ----- keep-alive and monitoring ---------------------------------------------------------------------------------

    def ping(self, timeout: float = 1.0) -> Optional[bool]:
        """True = the device answered, False = it stayed silent, None = busy with another transfer.
        Raises SerialError when the port itself failed (for example the cable was unplugged)."""
        if not self._lock.acquire(blocking=False):
            return None
        try:
            try:
                reply = self._exchange("PING", lambda l: l.startswith(("PONG", "ERR")), timeout)
            except SerialTimeout:
                return False
            if reply == "PONG 0":
                # The ESP32 restarted and forgot the link: announce ourselves again
                try:
                    self._exchange("HELLO", lambda l: l.startswith(("OK HELLO", "ERR")), 1.0)
                except SerialTimeout:
                    pass
            return True
        finally:
            self._lock.release()

    def read_status(self) -> str:
        """Run PRINT once and return the report (the lines between the two ==== separators)."""
        with self._lock:
            if not self.connected:
                raise SerialError("Not connected to a device")
            try:
                self.ser.reset_input_buffer()
                self._rx = b""
                self.ser.write(b"PRINT\n")
                self.ser.flush()
                deadline = time.time() + 3.0
                inside = False
                lines = []
                while time.time() < deadline:
                    line = self._read_line()
                    if line is None:
                        continue
                    if line.startswith("===="):
                        if inside:
                            return "\n".join(lines)
                        inside = True
                    elif inside and not line.startswith(("[", "WARN")):   # skip log lines mixed into the report
                        lines.append(line)
            except (serial.SerialException, OSError) as exc:
                raise SerialError(str(exc)) from None
        raise SerialTimeout("The device did not send a report in time")

    # ----- timetable and settings ------------------------------------------------------------------------------------

    def download(self) -> List[Event]:
        """ESP32 --> PC"""
        line = self._command("TT GET", lambda l: l.startswith("TT SET"), 5.0)
        try:
            return parse_payload(line)
        except ValueError as exc:
            raise SerialError(f"Bad data from device: {exc}") from None

    def upload(self, events: List[Event]) -> None:
        """PC --> ESP32"""
        if len(events) > MAX_EVENTS:
            raise SerialError(f"The firmware stores at most {MAX_EVENTS} events")
        try:
            payload = build_payload(events)
        except ValueError as exc:
            raise SerialError(str(exc)) from None
        reply = self._command(payload, lambda l: l.startswith(("OK TT SET", "ERR")), 8.0)
        if reply.startswith("ERR"):
            raise SerialError(reply)

    def download_config(self) -> DeviceConfig:
        """Read API key, location and time zone from the ESP32."""
        line = self._command("CFG GET", lambda l: l.startswith("CFG SET"), 5.0)
        try:
            return DeviceConfig.from_payload(line)
        except ValueError as exc:
            raise SerialError(f"Bad data from device: {exc}") from None

    def upload_config(self, cfg: DeviceConfig) -> None:
        """Write API key, location and time zone to the ESP32 (saved in its flash)."""
        try:
            payload = cfg.to_payload()
        except ValueError as exc:
            raise SerialError(str(exc)) from None
        reply = self._command(payload, lambda l: l.startswith(("OK CFG SET", "ERR")), 8.0)
        if reply.startswith("ERR"):
            raise SerialError(reply)
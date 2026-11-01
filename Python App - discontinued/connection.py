"""Background supervisor: finds the controller on any serial port and keeps the link alive with PING / PONG."""
import queue
import threading
import time

from serial_link import SerialError, SerialLink, list_port_info

SCAN_INTERVAL = 3.0       # seconds between two scans while no device is connected
REJECT_COOLDOWN = 20.0    # a port that did not identify itself is left alone for this long
PING_INTERVAL = 1.5       # seconds between two pings
PING_TIMEOUT = 1.0        # seconds to wait for a PONG
MAX_MISSED = 3            # this many silent pings in a row mean the connection is gone


class Supervisor(threading.Thread):
    """Posts ('connected', port) and ('lost', reason) tuples to `events`."""

    def __init__(self, link: SerialLink, events: queue.Queue):
        super().__init__(daemon=True)
        self.link = link
        self.events = events
        self.auto = True              # scan for the device while disconnected
        self._halt = threading.Event()

    def halt(self) -> None:
        self._halt.set()

    def run(self) -> None:
        missed = 0
        rejected = {}
        while not self._halt.is_set():
            if self.link.connected:
                if self._halt.wait(PING_INTERVAL):
                    break
                missed = self._check(missed)
            elif self.auto:
                missed = 0
                if self._scan(rejected):
                    continue
                self._halt.wait(SCAN_INTERVAL)
            else:
                missed = 0
                self._halt.wait(0.5)

    def _scan(self, rejected: dict) -> bool:
        """Probe every serial port once. True if the controller was found and connected."""
        for device, description in list_port_info():
            if self._halt.is_set() or not self.auto or self.link.connected:
                return self.link.connected
            if "bluetooth" in description.lower():
                continue   # Bluetooth serial ports can block for a long time when opened
            if time.time() - rejected.get(device, 0.0) < REJECT_COOLDOWN:
                continue
            if self.link.try_connect(device):
                self.events.put(("connected", device))
                return True
            rejected[device] = time.time()
        return False

    def _check(self, missed: int) -> int:
        if not self.link.connected:
            return 0   # disconnected by hand while we were waiting
        try:
            alive = self.link.ping(PING_TIMEOUT)
        except SerialError as exc:
            if self.link.connected:
                self._lost(str(exc))
            return 0
        if alive is None or alive:
            return 0   # busy with a transfer, or answered
        missed += 1
        if missed >= MAX_MISSED:
            self._lost("the device stopped answering")
            return 0
        return missed

    def _lost(self, reason: str) -> None:
        self.link.disconnect()
        self.events.put(("lost", reason))

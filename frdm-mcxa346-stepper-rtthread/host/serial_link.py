"""Single-owner serial worker. GUI threads never access the serial port."""
from collections import deque
import queue
import threading
import time
import serial
from protocol import parse_status, parse_config

class SerialLink(threading.Thread):
    def __init__(self, port, events, interval=0.25):
        super().__init__(daemon=True)
        self.port_name, self.events, self.interval = port, events, interval
        self.commands = deque()
        self.lock = threading.Lock()
        self.closing = threading.Event()
        self.port = None
        self.last_sample = 0

    def emit(self, kind, data=None):
        self.events.put((kind, data))

    def send(self, command, token=None):
        with self.lock:
            self.commands.append((command, token))

    def stop_motion(self):
        with self.lock:
            self.commands.clear()
            self.commands.extend([('stepper stop', None), ('stepper en 0', None)])

    def close(self):
        self.closing.set()

    def exchange(self, command, token=None):
        self.port.write((command + '\r\n').encode('ascii'))
        self.emit('tx', command)
        raw = bytearray()
        deadline = time.monotonic() + 0.45
        # Read through the final prompt, including command echo and status.
        while time.monotonic() < deadline:
            chunk = self.port.read(4096)
            if chunk:
                raw.extend(chunk)
                if raw.rstrip().endswith(b'msh >'):
                    break
        text = raw.decode('utf-8', errors='replace').replace('\r', '')
        echo_seen = False
        for line in text.splitlines():
            if not line or line.strip() == 'msh >':
                continue
            if line.strip().removeprefix('msh >').strip() == command:
                echo_seen = True
            self.emit('rx', line)
            cfg = parse_config(line)
            if cfg:
                self.emit('config', cfg)
            sample = parse_status(line, time.monotonic())
            if sample:
                self.last_sample = sample.t
                self.emit('sample', (sample, token if echo_seen else None))
            if 'command rejected' in line or 'stepper: not ready' in line:
                self.emit('rejected', line)
        return text

    def run(self):
        try:
            self.port = serial.Serial(baudrate=115200, timeout=0.025, write_timeout=0.5)
            self.port.dtr = False
            self.port.rts = False
            self.port.port = self.port_name
            self.port.open()
            self.port.reset_input_buffer()
            self.emit('connected', self.port_name)
            self.last_sample = time.monotonic()
            self.exchange('stepper config')
            next_poll = 0
            while not self.closing.is_set():
                with self.lock:
                    item = self.commands.popleft() if self.commands else None
                if item:
                    self.exchange(*item)
                elif time.monotonic() >= next_poll:
                    self.exchange('stepper status')
                    next_poll = time.monotonic() + self.interval
                else:
                    self.closing.wait(0.02)
                if time.monotonic() - self.last_sample > 3:
                    raise TimeoutError('连续 3 秒未收到有效状态，请检查固件和 USB。')
        except Exception as exc:
            self.emit('error', str(exc))
        finally:
            if self.port and self.port.is_open:
                try:
                    self.exchange('stepper stop')
                    self.exchange('stepper en 0')
                except Exception:
                    pass  # A lost cable cannot carry the stop command.
                self.port.close()
            self.emit('disconnected')

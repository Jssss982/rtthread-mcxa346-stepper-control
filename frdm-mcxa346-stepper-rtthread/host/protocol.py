"""Serial protocol and measurement definitions; independent of the GUI."""
from dataclasses import dataclass, asdict
from collections import defaultdict
import math
import re
import statistics

# target occurs twice in the firmware line: integer counts FIRST, degrees LAST.
STATUS = re.compile(
    r'axis1: en=(\d) run=(\d) active=(\d) reached=(\d) fault=(\S+) '
    r'count=(-?\d+) target=(-?\d+) err=(-?\d+) speed=(\d+)Hz remain=(\d+) '
    r'pulses=(\d+) enc_angle=(-?[\d.]+) target=(-?[\d.]+)')
CONFIG = re.compile(r'CPR=(\d+) microstep=(\d+) max=(\d+)Hz')

@dataclass
class Sample:
    t: float
    en: int
    run: int
    active: int
    reached: int
    fault: str
    count: int
    target: int
    err: int
    speed: int
    remain: int
    pulses: int

    @property
    def done(self):
        return bool(self.reached and not self.run and not self.active and self.fault == 'none')

    def degrees(self, cpr):
        return self.count * 360 / cpr, self.target * 360 / cpr, self.err * 360 / cpr

def parse_status(line, t):
    m = STATUS.search(line)
    if not m:
        return None
    v = m.groups()
    s = Sample(t, *map(int, v[:4]), v[4], *map(int, v[5:11]))
    # Truncated or inconsistent snapshots are excluded from measurement.
    if s.target - s.count != s.err:
        return None
    return s

def parse_config(line):
    m = CONFIG.search(line)
    if not m:
        return None
    cpr, microstep, max_hz = map(int, m.groups())
    return (cpr, microstep, max_hz) if cpr > 0 and microstep > 0 else None

@dataclass
class Endpoint:
    command: str
    target_count: int
    count: int
    error_count: int
    error_deg: float
    target_deg: float
    actual_deg: float
    direction: int
    elapsed: float
    cpr: int

class MoveMeasurement:
    """Associate one command with one completion, not repeated reached polls."""
    def __init__(self, command, token, start_count, started, cpr):
        self.command, self.token = command, token
        self.start_count, self.started, self.cpr = start_count, started, cpr
        self.target = None
        self.errors = []
        self.completed = False

    def observe(self, sample, response_token):
        if self.completed:
            return None
        if self.target is None:
            if response_token != self.token:
                return None  # Ignore status replies already queued before our command.
            self.target = sample.target
        if sample.target != self.target or sample.fault != 'none':
            return None
        self.errors.append(sample.err * 360 / self.cpr)
        if not sample.done:
            return None
        self.completed = True
        delta = self.target - self.start_count
        return Endpoint(self.command, sample.target, sample.count, sample.err,
                        sample.err * 360 / self.cpr, sample.target * 360 / self.cpr,
                        sample.count * 360 / self.cpr, ((delta > 0) - (delta < 0)) if abs(delta) > 3 else 0,
                        max(0, sample.t - self.started), self.cpr)

    @property
    def rms(self):
        return math.sqrt(sum(e * e for e in self.errors) / len(self.errors)) if self.errors else None

    @property
    def peak(self):
        return max(map(abs, self.errors)) if self.errors else None

def repeatability(endpoints):
    """Same target, commanded displacement direction and CPR; not a metrology standard."""
    groups = defaultdict(list)
    for e in endpoints:
        if e.direction:
            groups[(e.target_count, e.direction, e.cpr)].append(e)
    rows = []
    for (target, direction, cpr), values in sorted(groups.items()):
        if len(values) < 2:
            continue
        angles = [v.actual_deg for v in values]
        rows.append(dict(target_count=target, target_deg=target * 360 / cpr,
                         direction=direction, cpr=cpr, n=len(values),
                         mean_error_deg=statistics.mean(v.error_deg for v in values),
                         range_deg=max(angles) - min(angles),
                         sample_std_deg=statistics.stdev(angles)))
    return rows

def endpoint_dict(e):
    return asdict(e)

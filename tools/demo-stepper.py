"""Bounded, attended demo. Run only after the operator is ready to record."""
import argparse
import re
import time
from pathlib import Path
import serial

p = argparse.ArgumentParser()
p.add_argument('--port', default='COM5')
p.add_argument('--output', type=Path, default=Path('output/evidence/video-demo-recording.log'))
a = p.parse_args()
a.output.parent.mkdir(parents=True, exist_ok=True)
with serial.Serial(a.port, 115200, timeout=0.1) as port, a.output.open('w', encoding='utf-8') as log:
    def command(cmd, seconds=0.7):
        port.write((cmd + '\r\n').encode())
        data = b''
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            data += port.read(4096)
        s = data.decode('utf-8', errors='replace')
        log.write('HOST ' + cmd + '\n' + s)
        log.flush()
        print(s, end='', flush=True)
        return s

    def move(cmd):
        print('\nDEMO: ' + cmd, flush=True)
        command(cmd)
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            s = command('stepper status')
            states = re.findall(r'axis1:.*', s)
            if not states:
                raise RuntimeError('Missing board status; stopping demo')
            state = states[-1]
            if 'fault=none' not in state:
                raise RuntimeError('Controller fault: ' + state)
            if 'reached=1' in state and 'run=0' in state and 'active=0' in state:
                time.sleep(3)
                return
        raise RuntimeError('Movement timed out')

    try:
        port.reset_input_buffer()
        command('stepper stop')
        command('stepper config')
        command('ps')
        command('stepper en 1', 1.0)
        command('stepper zero')
        for cmd in ('stepper goto 10', 'stepper goto 90', 'stepper move -45', 'stepper goto 0'):
            move(cmd)
    finally:
        command('stepper stop')
        command('stepper en 0')
        print('\nDEMO COMPLETE: stopped and disabled', flush=True)

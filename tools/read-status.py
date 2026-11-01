"""Read-only board status capture; does not start or enable motion."""
import argparse, time
from pathlib import Path
import serial
p=argparse.ArgumentParser()
p.add_argument('--port',default='COM5')
p.add_argument('--seconds',type=int,default=20)
p.add_argument('--output',type=Path,default=Path('output/evidence/status.log'))
a=p.parse_args();a.output.parent.mkdir(parents=True,exist_ok=True)
with serial.Serial(a.port,115200,timeout=0.1) as port, a.output.open('ab') as log:
    deadline=time.monotonic()+a.seconds
    while time.monotonic()<deadline:
        port.write(b'stepper status\r\n')
        end=time.monotonic()+0.6
        data=b''
        while time.monotonic()<end:data+=port.read(4096)
        log.write(data);log.flush()
        print(data.decode('utf-8',errors='replace'),end='',flush=True)

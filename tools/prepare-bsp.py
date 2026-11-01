"""Apply the three verified GCC BSP integration fixes, idempotently."""
from pathlib import Path
import argparse
p=argparse.ArgumentParser()
p.add_argument('bsp',type=Path)
a=p.parse_args()
b=a.bsp.resolve()
f=b/'rtconfig.py'
s=f.read_text(encoding='utf-8')
old="CFLAGS = DEVICE + ' -Wall -D__FPU_PRESENT'"
new="CFLAGS = DEVICE + ' -Wall -D__FPU_PRESENT -D__START=entry -D__STARTUP_CLEAR_BSS -include applications/stepper/inc/stepper_startup.h'"
if old in s:f.write_text(s.replace(old,new),encoding='utf-8')
elif new not in s:raise RuntimeError('Unexpected rtconfig.py; inspect manually')
f=b/'board/linker_scripts/MCXA346_flash.ld'
s=f.read_text(encoding='utf-8')
if '__fsymtab_start' not in s:
    anchor='    KEEP (*(.fini))'
    if anchor not in s:raise RuntimeError('Unexpected linker script')
    tables='\n    /* RT-Thread registration tables must survive --gc-sections. */\n    . = ALIGN(4);\n    __fsymtab_start = .;\n    KEEP(*(FSymTab))\n    __fsymtab_end = .;\n    . = ALIGN(4);\n    __vsymtab_start = .;\n    KEEP(*(VSymTab))\n    __vsymtab_end = .;\n    . = ALIGN(4);\n    __rt_init_start = .;\n    KEEP(*(SORT(.rti_fn.*)))\n    __rt_init_end = .;\n'
    f.write_text(s.replace(anchor,anchor+tables),encoding='utf-8')
f=b/'SConstruct'
s=f.read_text(encoding='utf-8')
line="env.Depends(TARGET, 'board/linker_scripts/MCXA346_flash.ld')"
if line not in s:
    if 'DoBuilding(TARGET, objs)' not in s:raise RuntimeError('Unexpected SConstruct')
    f.write_text(s.replace('DoBuilding(TARGET, objs)',line+'\nDoBuilding(TARGET, objs)'),encoding='utf-8')
print('GCC entry, registration tables, and linker dependency ready')

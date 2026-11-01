"""Package current maintained app plus the verified upstream source snapshot."""
from pathlib import Path
import hashlib, json, shutil, zipfile
root=Path(__file__).resolve().parent.parent
out=root/'output/submission';out.mkdir(parents=True,exist_ok=True)
app=root/'frdm-mcxa346-stepper-rtthread'
bsp=root/'tools/rt-thread/bsp/nxp/mcx/mcxa/frdm-mcxa346'
shutil.copytree(app,bsp/'applications/stepper',dirs_exist_ok=True)
files=[]
for source in [app,root/'tools/rt-thread']:
    for f in source.rglob('*'):
        if not f.is_file():continue
        rel=f.relative_to(root)
        if any(x in rel.parts for x in ['build','__pycache__','.git']):continue
        if f.suffix.lower() in ['.o','.elf','.bin','.map','.log','.pyc']:continue
        if f.name.startswith('.sconsign'):continue
        files.append(f)
files += [root/'tools'/n for n in ['build-stepper.ps1','prepare-bsp.py','test-stepper.ps1','read-status.py','demo-stepper.py','package-stepper.py']]
archive=out/'RTThread_MCXA346_Stepper_Source.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for f in files:z.write(f,f.relative_to(root).as_posix())
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    assert z.read('frdm-mcxa346-stepper-rtthread/src/stepper_shell.c') == (app/'src/stepper_shell.c').read_bytes()
    assert z.read('frdm-mcxa346-stepper-rtthread/inc/stepper_board.h') == (app/'inc/stepper_board.h').read_bytes()
    assert z.read('frdm-mcxa346-stepper-rtthread/README.md') == (app/'README.md').read_bytes()
manifest={'rt_thread_commit':'cbc19778d0ae5ef2533a28c0f62c6a399858010a','archives':{},'firmware':{},'source_zip_sha256':hashlib.sha256(archive.read_bytes()).hexdigest()}
for name in ['nxp-mcx-cmsis.tar.gz','nxp-mcx-series.tar.gz']:
    f=root/'tools/reference-cache'/name
    if f.exists():manifest['archives'][name]=hashlib.sha256(f.read_bytes()).hexdigest()
for f in (root/'output/firmware').glob('rtthread.*'):
    manifest['firmware'][f.name]=hashlib.sha256(f.read_bytes()).hexdigest()
(out/'dependency-lock.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
print('Verified source package:',len(files),'files,',archive.stat().st_size,'bytes')

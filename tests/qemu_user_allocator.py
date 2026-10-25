#!/usr/bin/env python3
"""Native CHOS C allocation/string runtime through actual EL0 memory syscalls."""
import argparse,hashlib,json,pathlib,re,shutil,subprocess,sys,time
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--out',type=pathlib.Path,required=True)
ap.add_argument('--mode',choices=['direct','uefi'],default='direct')
ap.add_argument('--iso',type=pathlib.Path,default=pathlib.Path('build/chos-0.30.0.iso'))
o=ap.parse_args();out=o.out.resolve();out.mkdir(parents=True,exist_ok=False)
disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
app=pathlib.Path('build/userland/runtime_test.elf')
subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/BIN/CRTTEST.ELF',str(app)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=pathlib.Path('build/chos.elf'),uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=o.iso,memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk)
normal='[ OK ] native C malloc/calloc/realloc/alignment, strings and heap reclamation passed'
abandon='[ OK ] native C abandoned private heap exits for process reaper'
def launch(mode=''):
 marker=abandon if mode else normal
 before=g.text().count(marker);g.serial('exec /BIN/CRTTEST.ELF'+(' '+mode if mode else ''))
 wait(lambda:g.text().count(marker)>before or '[FAIL] native C' in g.text(),12,'actual EL0 libc '+(mode or 'runtime'))
 assert '[FAIL] native C' not in g.text() and '[FATAL]' not in g.text(),g.text()
def free_pages():
 end=time.monotonic()+8
 while True:
  header='CHOS process table (foreground and scheduled EL0 processes)';before=g.text().count(header);g.serial('procinfo')
  wait(lambda:g.text().count(header)>before,3,'process cleanup diagnostic')
  if re.search(r'Active: 0;.*pending cleanup: 0',g.text().rsplit(header,1)[-1]):break
  if time.monotonic()>end:raise AssertionError('process runtime did not reap')
  time.sleep(.05)
 before=len(re.findall(r'Free pages: (\d+)',g.text()));g.serial('meminfo')
 wait(lambda:len(re.findall(r'Free pages: (\d+)',g.text()))>before,3,'PMM stats')
 return int(re.findall(r'Free pages: (\d+)',g.text())[-1])
try:
 wait(lambda:'chos> ' in g.text(),45,'scheduled shell')
 if o.mode=='uefi':assert 'UEFI CPU isolation active' in g.text()
 wait(lambda:g.symbol('splash_active')==0,10,'splash')
 launch();baseline=free_pages()
 for _ in range(8):launch();launch('abandon')
 g.serial('runtime');wait(lambda:'CHOS scheduled runtime console.' in g.text(),5,'scheduled runtime')
 for _ in range(4):
  before=g.text().count(normal);g.serial('spawn /BIN/CRTTEST.ELF')
  wait(lambda:g.text().count(normal)>before or '[FAIL] native C' in g.text(),12,'scheduled EL0 libc')
  assert '[FAIL] native C' not in g.text()
 if o.mode=='direct':
  g.serial('runtime-exit');wait(lambda:'runtime shell and scheduled processes finished; returned to boot shell.' in g.text(),8,'runtime cleanup')
 launch();after=free_pages();assert baseline==after,('heap process pages leaked',baseline,after)
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'application_sha256':hashlib.sha256(app.read_bytes()).hexdigest(),'free_pages_before':baseline,'free_pages_after':after,'checks':['native C and freestanding C++ objects call standard allocation and string symbols in isolated EL0','MAP/UNMAP-backed malloc/calloc/realloc, overflow preservation and aligned allocations','all16 mapping regions are exhausted, next allocation fails ENOMEM and live data survives','mixed live allocation patterns, string overlap and complete region release','abandoned mappings are reclaimed by actual process reaper','repeated exec/spawn launches preserve free physical pages']},indent=2))
except Exception:
 (out/'failure.txt').write_text(g.text());raise
finally:g.close()

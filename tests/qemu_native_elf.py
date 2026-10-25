#!/usr/bin/env python3
"""Actual native ELF loading, W^X/gap faults, large BSS and deferred cleanup."""
import argparse,pathlib,shutil,subprocess,sys,json,hashlib,re,time
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',type=pathlib.Path,required=True);ap.add_argument('--iso',type=pathlib.Path,default=pathlib.Path('build/chos-0.30.0.iso'));ap.add_argument('--mode',choices=['direct','uefi'],default='direct');o=ap.parse_args();out=o.out.resolve();out.mkdir(parents=True,exist_ok=False)
disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/BIN/NATIVE.ELF','build/userland/native_test.elf'],check=True)
for name,offset,value in [('BADWX',68,7),('BADABI',64+3*56,0)]:
 bad=out/(name+'.elf');data=bytearray(pathlib.Path('build/userland/native_test.elf').read_bytes());data[offset]=value;bad.write_bytes(data)
 subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/BIN/'+name+'.ELF',str(bad)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=pathlib.Path('build/chos.elf'),uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=o.iso,memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
application_sha256=hashlib.sha256(pathlib.Path('build/userland/native_test.elf').read_bytes()).hexdigest()
g=Guest(a,out,disk)
marker='[ OK ] native AArch64 ELF RX/R/RW, sparse image, large BSS and arguments passed'
def normal():
 before=g.text().count(marker);g.serial('exec /BIN/NATIVE.ELF');wait(lambda:g.text().count(marker)>before,6,'actual native ELF segments/BSS/cleanup')
def free_pages():
 # The default GUI shell already schedules EL0 tasks. Await their actual exit
 # and deferred reaping, rather than sampling immediately after an app's print.
 end=time.monotonic()+6
 while True:
  count=g.text().count('CHOS process table (foreground and scheduled EL0 processes)');g.serial('procinfo')
  wait(lambda:g.text().count('CHOS process table (foreground and scheduled EL0 processes)')>count,3,'process cleanup diagnostic')
  section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
  if re.search(r'Active: 0;.*pending cleanup: 0',section):break
  if time.monotonic()>=end:raise AssertionError('Process cleanup did not quiesce')
  time.sleep(.05)
 before=len(re.findall(r'Free pages: (\d+)',g.text()));g.serial('meminfo')
 wait(lambda:len(re.findall(r'Free pages: (\d+)',g.text()))>before,3,'PMM stats')
 return int(re.findall(r'Free pages: (\d+)',g.text())[-1])
try:
 wait(lambda:'chos> ' in g.text(),45,'scheduled shell')
 if o.mode=='uefi':assert 'UEFI CPU isolation active' in g.text()
 wait(lambda:g.symbol('splash_active')==0,10,'splash')
 normal();normal();baseline=free_pages()
 for name in ['BADWX','BADABI']:
  marker_error='[FAIL] spawn path: invalid CHOS executable format'
  before=g.text().count(marker_error);g.serial('exec /BIN/'+name+'.ELF')
  wait(lambda:g.text().count(marker_error)>before,6,'actual malformed ELF rejected before execution')
 for _ in range(6):normal()
 fault_results=[]
 modes=['readonly','execute','gap']
 kernel_address=g.symbols['__text_start']
 if o.mode=='uefi':kernel_address+=int(re.findall(r'Kernel physical base: (0x[0-9a-f]+)',g.text())[-1],16)-g.symbols['__kernel_start']
 modes.append('kernel '+format(kernel_address,'x'))
 for mode in modes:
  before=g.text().count('[USER FAULT]');g.serial('exec /BIN/NATIVE.ELF '+mode)
  wait(lambda:g.text().count('[USER FAULT]')>before,6,'EL0 '+mode+' protection fault')
  assert '[FATAL]' not in g.text()
  normal();fault_results.append(mode)
 g.serial('runtime');wait(lambda:"CHOS scheduled runtime console." in g.text(),5,'scheduled root/user runtime')
 for _ in range(4):
  before=g.text().count(marker);g.serial('spawn /BIN/NATIVE.ELF');wait(lambda:g.text().count(marker)>before,6,'scheduled EL0 mapping and deferred cleanup');time.sleep(.15)
 if o.mode=='direct':
  g.serial('runtime-exit');wait(lambda:'runtime shell and scheduled processes finished; returned to boot shell.' in g.text(),8,'scheduled cleanup returns to canonical root')
 normal()
 after=free_pages();assert baseline==after,('Native image pages leaked across normal/fault cleanup',baseline,after)
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'application_sha256':application_sha256,'free_pages_before':baseline,'free_pages_after':after,'checks':['native AArch64 ELF executes in EL0 with arguments and separate RX/R/RW segments','sparse image spans more than128 pages with256KiB zeroed writable BSS','W+X and absent CHOS ABI note refused before execution','writes to read-only segment and unmapped gap fault only the process','execute-never data call faults only the process','EL0 read of actual privileged kernel text is denied','normal/fault/scheduled exit and process slot reuse reclaim all owned frames']},indent=2))
except Exception:
 (out/'failure.txt').write_text(g.text());raise
finally:g.close()

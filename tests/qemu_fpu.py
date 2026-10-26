#!/usr/bin/env python3
"""Real FP-enabled EL0 children and full SIMD/control state across IRQ/SVC."""
import argparse,pathlib,shutil,subprocess,sys,json,re,time,hashlib
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--mode',choices=['direct','uefi'],default='direct');o=p.parse_args()
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False);disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
app=pathlib.Path('build/userland/fpu_test.elf');app_sha=hashlib.sha256(app.read_bytes()).hexdigest()
subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/BIN/FPTEST.ELF',str(app)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=pathlib.Path('build/chos.elf'),uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=pathlib.Path('build/chos-0.30.0.iso'),memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk)
marker='[ OK ] native FP/SIMD state isolation and fresh-process zeroing passed'
def free_pages():
 end=time.monotonic()+8
 while True:
  before=g.text().count('CHOS process table (foreground and scheduled EL0 processes)');g.serial('procinfo')
  wait(lambda:g.text().count('CHOS process table (foreground and scheduled EL0 processes)')>before,3,'process diagnostic')
  section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
  if re.search(r'Active: 0;.*pending cleanup: 0',section):break
  if time.monotonic()>end:raise AssertionError('FP process cleanup did not quiesce')
  time.sleep(.05)
 before=len(re.findall(r'Free pages: (\d+)',g.text()));g.serial('meminfo')
 wait(lambda:len(re.findall(r'Free pages: (\d+)',g.text()))>before,3,'free pages')
 return int(re.findall(r'Free pages: (\d+)',g.text())[-1])
try:
 wait(lambda:'chos> ' in g.text(),45,'scheduled runtime')
 if o.mode=='uefi':assert 'UEFI CPU isolation active' in g.text()
 wait(lambda:g.symbol('splash_active')==0,10,'splash')
 cold=free_pages()
 baseline=None
 preemptions_before=g.symbol("preemption_switches")
 for run in range(3):
  n=g.text().count(marker);g.serial('exec /BIN/FPTEST.ELF')
  wait(lambda:g.text().count(marker)>n or '[FAIL]' in g.text(),45,'actual FP/SIMD context switches')
  assert '[FAIL]' not in g.text() and '[USER FAULT]' not in g.text() and '[FATAL]' not in g.text(),g.text()
  observed=free_pages()
  if run==0:baseline=observed # existing kernel heap retains its first file-buffer arena
  else:assert observed==baseline,('FP children leaked pages after warmup',baseline,observed)
 assert g.text().count('[ OK ] native FP worker registers, controls and arithmetic passed')==12
 preemptions_after=g.symbol('preemption_switches')
 assert preemptions_after>preemptions_before,(preemptions_before,preemptions_after)
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'application_sha256':app_sha,'cold_free_pages':cold,'free_pages_before':baseline,'free_pages_after':free_pages(), 'timer_preemptions_before':preemptions_before,'timer_preemptions_after':preemptions_after,'checks':['all 32 full128-bit SIMD registers and FPCR/FPSR zero before C entry','two simultaneous worker processes use distinct patterns/control modes','all64 SIMD lanes and controls preserved across timer IRQ, YIELD and SLEEP','actual volatile double arithmetic compiled with FP ABI','three parent lifecycles; stable physical pages after first heap warmup'],'limits':['base FP/AdvancedSIMD only; SVE/SME intentionally trapped','kernel C must retain -mgeneral-regs-only','QEMU evidence; Fusion and physical hardware NOT RUN']},indent=2))
except Exception:
 (out/'failure.txt').write_text(g.text());raise
finally:g.close()

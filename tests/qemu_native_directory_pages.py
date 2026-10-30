#!/usr/bin/env python3
"""Native complete directory paging qualification."""
import argparse,pathlib,shutil,subprocess,sys,json,re,time,hashlib
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--application',type=pathlib.Path,required=True);p.add_argument('--mode',choices=['direct','uefi'],default='direct');o=p.parse_args()
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False);disk=out/'disposable.img';subprocess.run(['python3','tools/mkchfs.py','create',str(disk),'--size-mib','16'],check=True)
subprocess.run(['python3','tools/mkchfs.py','mkdir',str(disk),'/DOCUMENTS'],check=True)
app=o.application;app_sha=hashlib.sha256(app.read_bytes()).hexdigest()
subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/DIRS.ELF',str(app)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=pathlib.Path('build/chos.elf'),uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=pathlib.Path('build/chos-0.30.0.iso'),memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk)
marker='[ OK ] complete native directory pages,stable restart tokens and guarded outputs passed'
def free_pages():
 end=time.monotonic()+8
 while True:
  before=g.text().count('CHOS process table (foreground and scheduled EL0 processes)');g.serial('procinfo')
  wait(lambda:g.text().count('CHOS process table (foreground and scheduled EL0 processes)')>before,3,'process diagnostic')
  section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
  if re.search(r'Active: 0;.*pending cleanup: 0',section):break
  if time.monotonic()>end:raise AssertionError('thread process cleanup did not quiesce')
  time.sleep(.05)
 before=len(re.findall(r'Free pages: (\d+)',g.text()));g.serial('meminfo')
 wait(lambda:len(re.findall(r'Free pages: (\d+)',g.text()))>before,3,'free pages')
 return int(re.findall(r'Free pages: (\d+)',g.text())[-1])
try:
 assert not subprocess.check_output(['llvm-nm','-u',str(app)],text=True).strip()
 headers=subprocess.check_output(['llvm-readelf','-lW',str(app)],text=True)
 assert not re.search(r'^\s+TLS\s',headers,re.M) and not re.search(r'^\s+LOAD[^\n]*\bRWE\b',headers,re.M)
 wait(lambda:'chos> ' in g.text(),45,'scheduled runtime')
 if o.mode=='uefi':assert 'UEFI CPU isolation active' in g.text()
 wait(lambda:g.symbol('splash_active')==0,10,'splash')
 cold=free_pages();baseline=None;preemptions_before=g.symbol('preemption_switches')
 for run in range(3):
  offset=len(g.text());n=g.text().count(marker);g.serial('exec /DIRS.ELF')
  wait(lambda:g.text().count(marker)>n or '[FAIL]' in g.text(),60,'native directory fixture')
  assert '[FAIL]' not in g.text() and '[USER FAULT]' not in g.text() and '[FATAL]' not in g.text(),g.text()
  assert g.text()[offset:].count('[ OK ] directory page capability refusal')==1,g.text()[offset:]
  observed=free_pages()
  pid=re.search(r'pid=(\d+) path=/DIRS.ELF',g.text()[offset:]).group(1)
  section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
  assert re.search(r'\bpid='+pid+r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b',section),section
  if run==0:baseline=observed;assert baseline==cold,(cold,baseline)
  else:assert observed==baseline,('directory fixture leaked pages',baseline,observed)
 preemptions_after=g.symbol('preemption_switches');assert preemptions_after>preemptions_before
 assert g.symbol('futex_queued_waiters')==0
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'application_sha256':app_sha,'source':'disposable CHFS native application','cold_free_pages':cold,'free_pages_before':baseline,'free_pages_after':free_pages(),'timer_preemptions_before':preemptions_before,'timer_preemptions_after':preemptions_after,  'maximum_observed_page_us':max(map(int,re.findall(r'page call us=(\d+)',g.text()))),'checks':['full175-file directory after175 unrelated files plus10 explicit subdirectories,real ROMFS merge and empty directories','immutable pages within one token,write/rename/state-change refusal with unchanged output and explicit restart','capability refusal,invalid size/version/flags/reserved/path,index overflow and protected crosspage output','three concurrent enumeration threads;three process lifecycles,cold PMM equality and zero user heap/futex waiters'],
 'limits':['read-only restartable native directory pages,not full engine filesystem/browser','global mutations conservatively invalidate token;bounded whole catalogue CPU work and native CHFS path limits','QEMU only;new Fusion/Pi5/laptop qualification NOT RUN']},indent=2)+'\n')
except Exception:
 try:
  g.serial('procinfo');time.sleep(.4);g.serial('threads');time.sleep(.4)
 except Exception:pass
 (out/'failure.txt').write_text(g.text());raise
finally:g.close()

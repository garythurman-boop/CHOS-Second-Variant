#!/usr/bin/env python3
"""Original LLVM RTTI, byte C APIs, and termination in scheduled native CHOS processes."""
import argparse,pathlib,shutil,subprocess,sys,json,re,time,hashlib
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--mode',choices=['direct','uefi'],default='direct');o=p.parse_args()
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False);disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
app=pathlib.Path('build/userland/'+'rtti_test.elf');app_sha=hashlib.sha256(app.read_bytes()).hexdigest()
subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/BIN/RTTI.ELF',str(app)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=pathlib.Path('build/chos.elf'),uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=pathlib.Path('build/chos-0.30.0.iso'),memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk)
marker='[ OK ] native RTTI typeid/cross-casts,byte classification and intmax conversions passed'
refusals=[('bad-cast','failed reference cast refusal begins'),('null-typeid','null polymorphic typeid refusal begins')]
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
 assert not subprocess.check_output(['llvm-nm','-u',str(app)],text=True).strip(),'unresolved C++ import'
 headers=subprocess.check_output(['llvm-readelf','-lW',str(app)],text=True)
 assert not re.search(r'^\s+TLS\s',headers,re.M)
 assert not re.search(r'^\s+LOAD[^\n]*\bRWE\b',headers,re.M),'writable executable segment'
 wait(lambda:'chos> ' in g.text(),45,'scheduled runtime')
 if o.mode=='uefi':assert 'UEFI CPU isolation active' in g.text()
 wait(lambda:g.symbol('splash_active')==0,10,'splash')
 cold=free_pages();baseline=None;cases=[];preemptions_before=g.symbol('preemption_switches')
 for run in range(3):
  offset=len(g.text());n=g.text().count(marker);g.serial('exec /BIN/RTTI.ELF')
  wait(lambda:g.text().count(marker)>n or '[FAIL]' in g.text(),30,'native RTTI')
  assert '[FAIL]' not in g.text() and '[USER FAULT]' not in g.text() and '[FATAL]' not in g.text(),g.text()
  observed=free_pages()
  pid=re.search(r'pid=(\d+) path=/BIN/RTTI.ELF',g.text()[offset:]).group(1)
  section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
  assert re.search(r'\bpid='+pid+r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b',section),section
  cases.append({'argument':'normal','pid':int(pid),'expected_exit':0,'status':'PASS'})
  if run==0:baseline=observed
  else:assert observed==baseline,('RTTI cleanup leaked pages',baseline,observed)
 preemptions_after=g.symbol('preemption_switches');assert preemptions_after>preemptions_before
 for argument,diagnostic in refusals:
  offset=len(g.text());g.serial('exec /BIN/RTTI.ELF '+argument)
  wait(lambda:diagnostic in g.text()[offset:] or '[FAIL]' in g.text()[offset:],10,'bounded '+argument)
  assert '[FAIL]' not in g.text() and '[FATAL]' not in g.text() and free_pages()==baseline
  pid=re.search(r'pid=(\d+) path=/BIN/RTTI.ELF',g.text()[offset:]).group(1)
  section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
  assert re.search(r'\bpid='+pid+r'\b[^\r\n]*exit=134\b',section),section
  cases.append({'argument':argument,'pid':int(pid),'expected_exit':134,'status':'PASS'})
 offset=len(g.text());n=g.text().count(marker);g.serial('exec /BIN/RTTI.ELF')
 wait(lambda:g.text().count(marker)>n or '[FAIL]' in g.text(),30,'C++ relaunch after failures')
 assert '[FAIL]' not in g.text() and free_pages()==baseline
 pid=re.search(r'pid=(\d+) path=/BIN/RTTI.ELF',g.text()[offset:]).group(1)
 section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
 assert re.search(r'\bpid='+pid+r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b',section),section
 cases.append({'argument':'relaunch','pid':int(pid),'expected_exit':0,'status':'PASS'})
 assert g.symbol('futex_queued_waiters')==0
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'cases':cases,'application_sha256':app_sha,'cold_free_pages':cold,'free_pages_before':baseline,'free_pages_after':free_pages(),'timer_preemptions_before':preemptions_before,'timer_preemptions_after':preemptions_after,'checks':['real original LLVM dynamic_cast/type_info algorithm, cross-TU identity and standard exception class RTTI','virtual diamonds, public and private bases, ambiguous and null casts, cross-casts and void-pointer adjustment','three std::thread workers, 64 graph checks each and three lifecycles per application','C-locale bytes EOF through 255, signed/unsigned intmax limits and overflow errno','failed reference cast and null polymorphic typeid terminate only their process with exit 134','six process runs, stable warm physical pages and no queued futexes'],'limits':['bounded native no-exceptions libc++ subset with opt-in RTTI,not full standard library','std::thread/notify-at-exit are separately qualified; public futures and dynamic loading remain open','Only the ICU threading component is qualified; full ICU cleanup registry/common/i18n/data and modern engine remain absent','new Fusion/Pi behavior NOT RUN; Stage3 incomplete']},indent=2)+'\n')
except Exception:
 (out/'failure.txt').write_text(g.text());raise
finally:g.close()

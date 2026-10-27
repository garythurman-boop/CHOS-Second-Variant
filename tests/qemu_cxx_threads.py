#!/usr/bin/env python3
"""Native std::thread lifecycle and failure isolation against CHOS scheduling."""
import argparse,pathlib,shutil,subprocess,sys,json,re,time,hashlib
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--application',type=pathlib.Path);p.add_argument('--standard',choices=['20','23'],default='23');p.add_argument('--mode',choices=['direct','uefi'],default='direct');o=p.parse_args()
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False);disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
app=o.application or pathlib.Path('build/userland/'+('cxx_thread20_test.elf' if o.standard=='20' else 'cxx_thread_test.elf'));app_sha=hashlib.sha256(app.read_bytes()).hexdigest()
subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/BIN/SYNC.ELF',str(app)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=pathlib.Path('build/chos.elf'),uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=pathlib.Path('build/chos-0.30.0.iso'),memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk)
marker='[ OK ] native std::thread move/join,TLS ordering,exit notification and sleep passed'
refusals=[('join-empty','empty join refusal begins'),('joinable-destructor','joinable destructor refusal begins'),('detach','detach refusal begins'),('capacity','fourth C++ worker refusal begins'),('pthread-exit','C++ callable pthread_exit refusal begins'),('move-over-live','move over joinable refusal begins'),('self-join','self join refusal begins'),('handler-return','returning handler refusal begins'),('handler-exit','exiting handler begins'),('handler-recursive','recursive handler refusal begins'),('handler-thread-exit','handler pthread_exit refusal begins')]
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
  offset=len(g.text());n=g.text().count(marker);g.serial('exec /BIN/SYNC.ELF')
  wait(lambda:g.text().count(marker)>n or '[FAIL]' in g.text(),30,'native C++ synchronization')
  assert '[FAIL]' not in g.text() and '[USER FAULT]' not in g.text() and '[FATAL]' not in g.text(),g.text()
  observed=free_pages()
  pid=re.search(r'pid=(\d+) path=/BIN/SYNC.ELF',g.text()[offset:]).group(1)
  section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
  assert re.search(r'\bpid='+pid+r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b',section),section
  cases.append({'argument':'normal','pid':int(pid),'expected_exit':0,'status':'PASS'})
  if run==0:baseline=observed
  else:assert observed==baseline,('C++ synchronization cleanup leaked pages',baseline,observed)
 preemptions_after=g.symbol('preemption_switches');assert preemptions_after>preemptions_before
 for argument,diagnostic in refusals:
  offset=len(g.text());g.serial('exec /BIN/SYNC.ELF '+argument)
  wait(lambda:diagnostic in g.text()[offset:] or '[FAIL]' in g.text()[offset:],10,'bounded '+argument)
  assert '[FAIL]' not in g.text() and '[FATAL]' not in g.text() and free_pages()==baseline
  pid=re.search(r'pid=(\d+) path=/BIN/SYNC.ELF',g.text()[offset:]).group(1)
  section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
  expected=77 if argument=='handler-exit' else 134
  assert re.search(r'\bpid='+pid+r'\b[^\r\n]*exit='+str(expected)+r'\b',section),section
  cases.append({'argument':argument,'pid':int(pid),'expected_exit':expected,'status':'PASS'})
 offset=len(g.text());n=g.text().count(marker);g.serial('exec /BIN/SYNC.ELF')
 wait(lambda:g.text().count(marker)>n or '[FAIL]' in g.text(),30,'C++ relaunch after failures')
 assert '[FAIL]' not in g.text() and free_pages()==baseline
 pid=re.search(r'pid=(\d+) path=/BIN/SYNC.ELF',g.text()[offset:]).group(1)
 section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
 assert re.search(r'\bpid='+pid+r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b',section),section
 cases.append({'argument':'relaunch','pid':int(pid),'expected_exit':0,'status':'PASS'})
 assert g.symbol('futex_queued_waiters')==0
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'cxx_standard':o.standard,'cases':cases,'application_sha256':app_sha,'cold_free_pages':cold,'free_pages_before':baseline,'free_pages_after':free_pages(),'timer_preemptions_before':preemptions_before,'timer_preemptions_after':preemptions_after,'checks':['original LLVM thread.cpp create/move/swap/id/join lifecycle over native workers','three workers and repeated processes free native heap and kernel frames','C++ TLS and post-key TLS destructors precede original notify_all_at_thread_exit unlock/notification','per-thread standard-library exit state survives TLS and then releases its allocations before join','native nanosecond/integer-millisecond/floating-second sleep,steady sleep_until/yield and unknown hardware_concurrency hint','empty/self join,detach,capacity,joinable destruction/move and forbidden callable pthread_exit fail closed','atomic terminate handler get/set,return/reentry refusal and explicit handler process exit','successful relaunch after every refusal set and no queued futexes'],'limits':['static C++20/23 -O2 no-exceptions/no-RTTI profile,three live worker/join records per process','detach and pthread_exit from a std::thread callable are unsupported; no stack unwinding/cancellation','original future.cpp supplies the private readiness callback only; public future/exception_ptr ABI is not supplied','binary128 arithmetic helpers,arbitrary duration/clock types and unoptimized compiler lowering are not qualified','full ICU/engine,memory/isolation,production TLS and real Pi5 wireless remain open; Stage3 incomplete','new Fusion/Pi/laptop qualification NOT RUN']},indent=2)+'\n')
except Exception:
 (out/'failure.txt').write_text(g.text());raise
finally:g.close()

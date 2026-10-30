#!/usr/bin/env python3
"""Native libc++ synchronization against actual CHOS worker scheduling."""
import argparse,pathlib,shutil,subprocess,sys,json,re,time,hashlib
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--component',choices=['cxx','icu'],default='cxx');p.add_argument('--mode',choices=['direct','uefi'],default='direct');o=p.parse_args()
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False);disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
app=pathlib.Path('build/userland/'+('icu_threading_test.elf' if o.component=='icu' else 'cxx_sync_test.elf'));app_sha=hashlib.sha256(app.read_bytes()).hexdigest()
subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/BIN/SYNC.ELF',str(app)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=pathlib.Path('build/chos.elf'),uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=pathlib.Path('build/chos-0.30.0.iso'),memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk)
marker=('[ OK ] original ICU mutex/init-once contention,error publication and cleanup/reinitialize passed' if o.component=='icu' else '[ OK ] native C++ mutex/condition/clock/once contention,timeout and owner-exit passed')
refusals=([('assert','ICU assertion refusal begins')] if o.component=='icu' else [('recursive','recursive once refusal begins'),('quota','once quota refusal begins'),('unlocked-wait','unlocked condition wait refusal begins'),('double-lock','unique_lock double acquisition refusal begins')])
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
  assert re.search(r'\bpid='+pid+r'\b[^\r\n]*exit=134\b',section),section
  if argument=='assert':assert 'Assertion failed: false at userland/examples/icu_threading_test.cpp:' in g.text()[offset:]
  cases.append({'argument':argument,'pid':int(pid),'expected_exit':134,'status':'PASS'})
 offset=len(g.text());n=g.text().count(marker);g.serial('exec /BIN/SYNC.ELF')
 wait(lambda:g.text().count(marker)>n or '[FAIL]' in g.text(),30,'C++ relaunch after failures')
 assert '[FAIL]' not in g.text() and free_pages()==baseline
 pid=re.search(r'pid=(\d+) path=/BIN/SYNC.ELF',g.text()[offset:]).group(1)
 section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
 assert re.search(r'\bpid='+pid+r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b',section),section
 cases.append({'argument':'relaunch','pid':int(pid),'expected_exit':0,'status':'PASS'})
 assert g.symbol('futex_queued_waiters')==0
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'component':o.component,'cases':cases,'application_sha256':app_sha,'cold_free_pages':cold,'free_pages_before':baseline,'free_pages_after':free_pages(),'timer_preemptions_before':preemptions_before,'timer_preemptions_after':preemptions_after,'checks':(['unmodified ICU79.1 umutex.cpp,with original debug assertions enabled','three native workers contend on eight ICU mutexes plus global mutex','ICU UInitOnce exact-once data and failed-status publication through actual condition-variable wait/notify','test recorder captures only the mutex cleanup callback; invokes real ICU cleanup and placement reset across four complete lifecycles','native C assertion failure aborts only its process; clean relaunch,stable warm frames and no queued futexes'] if o.component=='icu' else ['upstream mutex/recursive/timed/recursive-timed algorithms with native pthread bindings','three contending workers,condition predicates,notify_one/all,unique_lock/scoped_lock and memory publication','original shared_ptr/weak_ptr concurrent ownership and exact destruction; condition_variable_any with recursive_mutex','real system/steady clocks,monotonic wait_for/until and timeout followed by lock reacquisition','once_flag full-width completion,argument forwarding,16 nested initializers and retry after pthread_exit including key destructor','recursive/depth17 once,unowned wait and double unique_lock abort134 without kernel failure','three process runs and relaunch after refusals,stable warm physical pages and no queued futexes']),'limits':['bounded native no-exceptions/no-RTTI libc++ subset,not full standard library','std::thread/notify-at-exit are separately qualified; public futures and dynamic loading remain open','Only the ICU threading component is qualified; full ICU cleanup registry/common/i18n/data and modern engine remain absent','new Fusion/Pi behavior NOT RUN; Stage3 incomplete']},indent=2)+'\n')
except Exception:
 # Preserve live scheduler/futex/thread state without extending acceptance
 # deadlines. Do not hide a stopped guest behind a later passing run.
 for command in ['procinfo','taskinfo']:
  try:g.serial(command);time.sleep(.3)
  except Exception:pass
 try:
  g.qmp('stop')
  (out/'failure-registers.txt').write_text(g.hmp('info registers'))
  kernel=a.kernel if o.mode=='direct' else a.uefi_kernel
  symbols=subprocess.check_output(['llvm-nm','-S',str(kernel)],text=True)
  wanted={'task_states','task_wake_ticks','task_reap_pending','tasks','threads','owners','waits','current_slot','futex_queued_waiters'}
  for line in symbols.splitlines():
   fields=line.split()
   if len(fields)==4 and fields[-1] in wanted:
    name=fields[-1];size=int(fields[1],16)
    if 0<size<=65536:(out/('failure-'+name+'.bin')).write_bytes(g.block(name,size=size))
 except Exception as diagnostic_error:(out/'failure-diagnostic-error.txt').write_text(str(diagnostic_error))
 (out/'failure.txt').write_text(g.text());raise
finally:g.close()

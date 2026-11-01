#!/usr/bin/env python3
"""Actual managed detached-thread ownership,TLS cleanup and process isolation."""
import argparse,pathlib,shutil,subprocess,sys,json,re,time,hashlib
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--application',type=pathlib.Path,required=True);p.add_argument('--mode',choices=['direct','uefi'],default='direct');o=p.parse_args()
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False);disk=out/'disposable.img';subprocess.run(['python3','tools/mkchfs.py','create',str(disk),'--size-mib','16'],check=True)
subprocess.run(['python3','tools/mkchfs.py','mkdir',str(disk),'/DOCUMENTS'],check=True)
app=o.application;app_sha=hashlib.sha256(app.read_bytes()).hexdigest()
subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/DETACH.ELF',str(app)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=pathlib.Path('build/chos.elf'),uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=pathlib.Path('build/chos-0.30.0.iso'),memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk)
marker='[ OK ] native managed detach,TLS ordering,stack ownership and repeated cleanup passed'
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
  offset=len(g.text());n=g.text().count(marker);g.serial('exec /DETACH.ELF')
  wait(lambda:g.text().count(marker)>n or '[FAIL]' in g.text() or '[FATAL]' in g.text(),60,'managed detach fixture')
  assert '[FAIL]' not in g.text() and '[FATAL]' not in g.text(),g.text()
  assert g.text().count('[USER FAULT]')==run+1,g.text()
  observed=free_pages()
  pid=re.search(r'pid=(\d+) path=/DETACH.ELF',g.text()[offset:]).group(1)
  section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
  assert re.search(r'\bpid='+pid+r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b',section),section
  if run==0:baseline=observed;assert baseline==cold,(cold,baseline)
  else:assert observed==baseline,('managed detach fixture leaked pages',baseline,observed)
 for diagnostic in ['managed detach capability refusals preserved ownership','detached main exit preserved worker and TLS cleanup','foreign thread ownership refused','exiting with three live detached workers','entering fault with three live detached workers']:assert g.text().count(diagnostic)==3,diagnostic
 preemptions_after=g.symbol('preemption_switches');assert preemptions_after>preemptions_before
 assert g.symbol('futex_queued_waiters')==0
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'application_sha256':app_sha,'source':'disposable CHFS native application','cold_free_pages':cold,'free_pages_before':baseline,'free_pages_after':free_pages(),'timer_preemptions_before':preemptions_before,'timer_preemptions_after':preemptions_after,  'checks':['native pthread attr/live/self/after-exit detach and actual std::thread.detach;112 managed library workers in each parent plus eight in its children and26 raw ownership cases','C++ TLS,twice-reset ordinary keys and post-key TLS finish before real thread-exit notifications;FP inheritance and errno isolation','kernel-managed exact stack disposal and zero allocator/owned-frame growth;fast creation acknowledgement and stale TID/address reuse safety','legacy raw V1 exit/unpin/collect behavior preserved;managed stack pin and collect/double-detach refusal','REAP58 denial blocks both detach79 and initially-detached creation;foreign process cannot inspect,collect or detach parent worker','detached main exits with live worker;normal process exit and actual EL0 fault cancel three live detached workers','three process lifecycles,cold physical page equality,no remaining futex waiters;exactly three expected isolated child data aborts'],
 'limits':['bounded single-CPU native pthread/managed stack component,not complete POSIX,foreign GC or engine/browser qualification','forced process termination does not promise user TLS destructors;all owned kernel resources must still be reclaimed','QEMU only;new Fusion/Pi5/laptop qualification NOT RUN']},indent=2)+'\n')
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

#!/usr/bin/env python3
"""Real aligned engine reservations, bounded commits and cancellation cleanup."""
import argparse,pathlib,shutil,subprocess,time,json,re,hashlib
from qemu_platform import Guest,wait
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True)
p.add_argument('--application',type=pathlib.Path,required=True);p.add_argument('--legacy-application',type=pathlib.Path,required=True)
p.add_argument('--mode',choices=['direct','uefi'],default='direct');o=p.parse_args()
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False);disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
for name,app in [('/RESERVE.ELF',o.application),('/RESLEG.ELF',o.legacy_application)]:subprocess.run(['python3','tools/mkchfs.py','put',str(disk),name,str(app)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=pathlib.Path('build/chos.elf'),uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=pathlib.Path('build/chos-0.30.0.iso'),memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk)
normal='[ OK ] native aligned32MiB reservation,sparse zero commit,decommit,split release and budgets passed'
live='[ OK ] native reservation worker remains live for process termination'
legacy='[ OK ] legacy executable refuses engine virtual reservation'
def free_pages():
 end=time.monotonic()+15
 while True:
  header='CHOS process table (foreground and scheduled EL0 processes)';before=g.text().count(header);g.serial('procinfo')
  wait(lambda:g.text().count(header)>before,4,'process diagnostic')
  if re.search(r'Active: 0;.*pending cleanup: 0',g.text().rsplit(header,1)[-1]):break
  assert time.monotonic()<end,'Reservation cleanup did not quiesce'
 before=len(re.findall(r'Free pages: (\d+)',g.text()));g.serial('meminfo')
 wait(lambda:len(re.findall(r'Free pages: (\d+)',g.text()))>before,4,'free pages')
 assert g.symbol('committed_frame_limits')==0 and g.symbol('engine_owner_slot')==0xffffffff
 return int(re.findall(r'Free pages: (\d+)',g.text())[-1])
try:
 wait(lambda:'chos> ' in g.text(),45,'scheduled runtime')
 if o.mode=='uefi':assert 'UEFI CPU isolation active' in g.text()
 wait(lambda:g.symbol('splash_active')==0,10,'splash');cold=free_pages();runs=[];faults=[]
 before_preemptions=int.from_bytes(g.block('preemption_switches',size=8),'little')
 for argument,marker in [('',normal),('live-worker',live)]:
  for run in range(3):
   if g.symbol('view')==4:g.hmp('sendkey esc');wait(lambda:g.symbol('view')!=4,3,'reset foreground view')
   offset=len(g.text());g.serial('exec /RESERVE.ELF'+(' '+argument if argument else ''));response=None
   if argument:
    wait(lambda:'[INFO] native reservation worker loop active' in g.text()[offset:] or '[FAIL]' in g.text()[offset:],30,'live commit/decommit worker')
    started=time.monotonic();g.hmp('sendkey f1');wait(lambda:g.symbol('view')==4,1,'desktop during live bounded memory worker')
    response=time.monotonic()-started
    assert live not in g.text()[offset:],'Worker must still be active when actual keyboard response is observed'
   wait(lambda:marker in g.text()[offset:] or any(v in g.text()[offset:] for v in ['[FAIL]','[USER FAULT]','[FATAL]']),60,'reservation '+(argument or 'resource fixture'))
   assert marker in g.text()[offset:] and '[FAIL]' not in g.text()[offset:] and '[USER FAULT]' not in g.text()[offset:]
   pid=re.search(r'pid=(\d+) path=/RESERVE.ELF',g.text()[offset:]).group(1);assert free_pages()==cold
   section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
   assert re.search(r'\bpid='+pid+r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b',section),section
   runs.append({'argument':argument,'pid':int(pid),'desktop_response_seconds_during_live_worker':response})
 for mode in ['uncommitted','guard','readonly','decommitted','execute']:
  offset=len(g.text());before=g.text().count('[USER FAULT]');g.serial('exec /RESERVE.ELF '+mode)
  wait(lambda:g.text().count('[USER FAULT]')>before or '[FAIL]' in g.text()[offset:],30,'contained reservation '+mode+' fault')
  assert g.text().count('[USER FAULT]')==before+1 and '[FAIL]' not in g.text()[offset:] and free_pages()==cold
  faults.append(mode)
 for run in range(3):
  offset=len(g.text());g.serial('exec /RESLEG.ELF')
  wait(lambda:legacy in g.text()[offset:] or '[FAIL]' in g.text()[offset:],15,'legacy reservation refusal')
  assert legacy in g.text()[offset:] and free_pages()==cold
 assert '[FATAL]' not in g.text() and g.symbol('futex_queued_waiters')==0
 after_preemptions=int.from_bytes(g.block('preemption_switches',size=8),'little');assert after_preemptions>before_preemptions
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'applications_sha256':{n:hashlib.sha256(app.read_bytes()).hexdigest() for n,app in [('engine',o.application),('legacy',o.legacy_application)]},'cold_free_pages':cold,'final_free_pages':free_pages(),'normal_and_live_worker_runs':runs,'contained_faults':faults,'legacy_refusals':3,'preemption_switches_before':before_preemptions,'preemption_switches_after':after_preemptions,'checks':['real32MiB aligned logical reservation adds no resident or mapped pages','32MiB bounded zero-filled commits,16MiB real decommit and zero recommit;exact per-process frame charges','guard,NONE,RO,decommitted and XN access faults contained','512-record logical metadata cap without physical backing;split release preserves reservation ownership','process termination with live commit/decommit worker;actual HID desktop response while worker remains active','every process returns to exact cold pages,commit,pool and futex baseline;legacy profile refuses new reservation'],'limits':['QEMU resource service only;not actual VM/JavaScript/Intl/GC/Wasm or modern browser qualification','Single CPU;no SMP or maximum IRQ latency claim;physical operation limit1MiB remains','New Fusion/Pi5/laptop qualification NOT RUN;Stage3 incomplete']},indent=2)+'\n')
except Exception:
 try:g.serial('procinfo');g.serial('taskinfo');time.sleep(.3);(out/'failure-registers.txt').write_text(g.hmp('info registers'))
 except Exception:pass
 (out/'failure.txt').write_text(g.text());raise
finally:g.close()

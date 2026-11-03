#!/usr/bin/env python3
"""Explicit large native image,heap/stack isolation and terminal cleanup."""
import argparse,hashlib,json,pathlib,re,shutil,subprocess,time
from qemu_platform import Guest,wait
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--out',type=pathlib.Path,required=True)
p.add_argument('--application',type=pathlib.Path,required=True)
p.add_argument('--variants',type=pathlib.Path,required=True)
p.add_argument('--mode',choices=['direct','uefi'],default='direct')
p.add_argument('--pressure',action='store_true')
o=p.parse_args();out=o.out.resolve();out.mkdir(parents=True,exist_ok=False)
disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
app=o.application.resolve();variants={'/UNKNOWN.ELF':'unknown-profile.elf','/AMBIG.ELF':'ambiguous-profile.elf','/LEGACY.ELF':'legacy-large.elf'}
for path,source in [('/ENGINE.ELF',app)]+[(n,o.variants/f) for n,f in variants.items()]:
 subprocess.run(['python3','tools/mkchfs.py','put',str(disk),path,str(source)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=pathlib.Path('build/chos.elf'),uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=pathlib.Path('build/chos-0.30.0.iso'),memory='256M' if o.pressure else ('2G' if o.mode=='uefi' else '512M'),qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk);marker='[ OK ] native engine48MiB image,1MiB main and128KiB worker stacks,expanded heap and isolated mappings passed'

def free_pages():
 end=time.monotonic()+12
 while True:
  before=g.text().count('CHOS process table (foreground and scheduled EL0 processes)');g.serial('procinfo')
  wait(lambda:g.text().count('CHOS process table (foreground and scheduled EL0 processes)')>before,4,'process diagnostic')
  section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
  if re.search(r'Active: 0;.*pending cleanup: 0',section):break
  if time.monotonic()>end:raise AssertionError('large process cleanup did not quiesce')
  time.sleep(.05)
 before=len(re.findall(r'Free pages: (\d+)',g.text()));g.serial('meminfo')
 wait(lambda:len(re.findall(r'Free pages: (\d+)',g.text()))>before,4,'free pages')
 assert g.symbol('committed_frame_limits')==0 and g.symbol('engine_owner_slot')==0xffffffff
 return int(re.findall(r'Free pages: (\d+)',g.text())[-1])

try:
 wait(lambda:'chos> ' in g.text(),45,'scheduled runtime')
 if o.mode=='uefi':assert 'UEFI CPU isolation active' in g.text()
 wait(lambda:g.symbol('splash_active')==0,10,'splash')
 cold=free_pages();runs=[];faults=[]
 before_preemptions=int.from_bytes(g.block('preemption_switches',size=8),'little')
 if o.pressure:
  for run in range(3):
   offset=len(g.text());g.serial('exec /ENGINE.ELF')
   wait(lambda:'[FAIL] spawn path:' in g.text()[offset:],10,'engine reservation pressure refusal')
   assert 'could not create process address space' in g.text()[offset:] and marker not in g.text()[offset:]
   assert free_pages()==cold
 else:
  for run in range(3):
   offset=len(g.text());g.serial('exec /ENGINE.ELF')
   wait(lambda:marker in g.text()[offset:] or '[FAIL]' in g.text()[offset:] or '[USER FAULT]' in g.text()[offset:],60,'large native engine-profile application')
   assert marker in g.text()[offset:] and '[FAIL]' not in g.text()[offset:] and '[USER FAULT]' not in g.text()[offset:],g.text()[offset:]
   pid=re.search(r'pid=(\d+) path=/ENGINE.ELF',g.text()[offset:]).group(1)
   assert free_pages()==cold
   section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
   assert re.search(r'\bpid='+pid+r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b',section),section
   runs.append({'pid':int(pid),'engine_teardown_timer_ticks':int.from_bytes(g.block('last_engine_teardown_ticks',size=8),'little')})
  for mode in ['image-guard','stack-guard','profile-write','execute-heap']:
   offset=len(g.text());before=g.text().count('[USER FAULT]');g.serial('exec /ENGINE.ELF '+mode)
   wait(lambda:g.text().count('[USER FAULT]')>before or '[FAIL]' in g.text()[offset:],45,'contained engine '+mode+' fault')
   assert g.text().count('[USER FAULT]')==before+1 and '[FAIL]' not in g.text()[offset:]
   assert free_pages()==cold
   faults.append(mode)
 for path in variants:
  offset=len(g.text());g.serial('exec '+path)
  wait(lambda:'[FAIL] spawn path:' in g.text()[offset:],10,'invalid executable profile refusal')
  assert 'invalid CHOS executable format' in g.text()[offset:] and marker not in g.text()[offset:]
  assert free_pages()==cold
 assert '[FATAL]' not in g.text() and g.symbol('futex_queued_waiters')==0
 after_preemptions=int.from_bytes(g.block('preemption_switches',size=8),'little')
 if not o.pressure:assert after_preemptions>before_preemptions
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'pressure_refusal':o.pressure,'application_sha256':hashlib.sha256(app.read_bytes()).hexdigest(),'file_bytes':app.stat().st_size,'cold_free_pages':cold,'final_free_pages':free_pages(),'normal_runs':runs,'contained_faults':faults,'invalid_profiles':list(variants),'preemption_switches_before':before_preemptions,'preemption_switches_after':after_preemptions,'checks':(['three engine executable reservations refused without page/commit/pool leak'] if o.pressure else ['actual48MiB BSS image and unique page markers','actual64KiB stack scratch and context bounds on1MiB main/128KiB defaultworker stacks','malloc registry beyond16regions and8MiB anonymous data with protection/discard/split release','contained image/stack guard,readonlyCRTprofile and heap-XN faults','abandoned mappings and large image return to exact cold physical baseline'])+['unknown/ambiguous ABI profile and legacy-large image refused independently of CRT hint'],'limits':['native executable resource fixture only;no actual VM/JavaScript/GC/WebAssembly/WebCore/browser qualification','QEMU evidence does not establish Fusion or physical hardware qualification']},indent=2)+'\n')
except Exception:
 try:g.serial('procinfo');time.sleep(.3);g.serial('taskinfo');time.sleep(.3)
 except Exception:pass
 (out/'failure.txt').write_text(g.text());raise
finally:g.close()

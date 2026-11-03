#!/usr/bin/env python3
"""Actual embedded native JSC VM execution; disposable guests, no host engine."""
import argparse,pathlib,shutil,subprocess,json,hashlib,re,struct,time
from qemu_platform import Guest,wait
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--mode',choices=['direct','uefi'],default='direct')
p.add_argument('--runs',type=int,choices=[1,3],default=3)
p.add_argument('--kernel',type=pathlib.Path,default=pathlib.Path('build/engine-test/chos.elf'))
p.add_argument('--uefi-kernel',type=pathlib.Path,default=pathlib.Path('build/engine-test/uefi-kernel.elf'))
p.add_argument('--iso',type=pathlib.Path,default=pathlib.Path('build/engine-test/chos.iso'))
p.add_argument('--application',type=pathlib.Path,default=pathlib.Path('build/engine-test/native-engine.elf'))
o=p.parse_args();out=o.out.resolve();out.mkdir(parents=True,exist_ok=False)
disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
a=argparse.Namespace(mode=o.mode,kernel=o.kernel,uefi_kernel=o.uefi_kernel,iso=o.iso,memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk);marker='[ OK ] actual JavaScriptCore VM,JavaScript and full ICU Intl executed'
def free_pages():
 end=time.monotonic()+15
 while True:
  header='CHOS process table (foreground and scheduled EL0 processes)';before=g.text().count(header);g.serial('procinfo')
  wait(lambda:g.text().count(header)>before,4,'process diagnostic')
  if re.search(r'Active: 0;.*pending cleanup: 0',g.text().rsplit(header,1)[-1]):break
  assert time.monotonic()<end,'VM cleanup did not quiesce'
 before=len(re.findall(r'Free pages: (\d+)',g.text()));g.serial('meminfo')
 wait(lambda:len(re.findall(r'Free pages: (\d+)',g.text()))>before,4,'free page count')
 assert g.symbol('committed_frame_limits')==0 and g.symbol('engine_owner_slot')==0xffffffff
 return int(re.findall(r'Free pages: (\d+)',g.text())[-1])
try:
 # Verify the immutable bytes actually linked into the snapshotted kernel.
 raw=(out/'kernel.elf').read_bytes();start=g.symbols['chos_romfs_engine_start'];end=g.symbols['chos_romfs_engine_end']
 ph=struct.unpack_from('<Q',raw,32)[0];count=struct.unpack_from('<H',raw,56)[0];embedded=None
 for i in range(count):
  kind,flags,f,va,pa,n,memory,align=struct.unpack_from('<IIQQQQQQ',raw,ph+i*56)
  if kind==1 and va<=start<end<=va+n:embedded=raw[f+start-va:f+end-va]
 assert embedded is not None and embedded==o.application.read_bytes(),'Kernel fixture differs from staged executable'
 app_sha=hashlib.sha256(embedded).hexdigest()
 wait(lambda:'chos> ' in g.text(),60,'diagnostic scheduled runtime')
 if o.mode=='uefi':assert 'UEFI CPU isolation active' in g.text()
 wait(lambda:g.symbol('splash_active')==0,10,'splash')
 cold=free_pages();runs=[]
 for run in range(o.runs):
  if g.symbol('view')==4:
   g.hmp('sendkey esc');wait(lambda:g.symbol('view')!=4,3,'reset desktop view')
  offset=len(g.text());started=time.monotonic();g.serial('exec /BIN/JSCVM.ELF');next_info=started+5
  while marker not in g.text()[offset:]:
   text=g.text()[offset:]
   # A failed evaluation must still get its real GC/context-release/exit path.
   # Success continues to require the exact positive marker and PID exit0.
   assert not any(v in text for v in ['[FATAL]','[USER FAULT]','ASSERTION FAILED']),text
   assert time.monotonic()-started<120,'Actual VM runtime timed out'
   if time.monotonic()>=next_info:
    g.serial('procinfo');next_info=time.monotonic()+5
    assert not re.search(r'pid=\d+[^\r\n]*state=(?:exited|failed)[^\r\n]*exit=(?:[1-9]\d*|-\d+)',text),text
   time.sleep(.05)
  pid=re.search(r'pid=(\d+) path=/BIN/JSCVM.ELF',g.text()[offset:]).group(1)
  response_start=time.monotonic();g.hmp('sendkey f1')
  wait(lambda:g.symbol('view')==4,2,'actual HID desktop response during VM exit')
  response=time.monotonic()-response_start
  assert free_pages()==cold
  section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
  assert re.search(r'\bpid='+pid+r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b',section),section
  runs.append({'pid':int(pid),'seconds':time.monotonic()-started,'free_pages_after':cold,'desktop_response_seconds_during_VM_exit':response,'engine_teardown_ticks':int.from_bytes(g.block('last_engine_teardown_ticks',size=8),'little')})
 assert g.symbol('futex_queued_waiters')==0
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'application_sha256':app_sha,'cold_free_pages':cold,'final_free_pages':free_pages(),'runs':runs,'checks':['Actual native original JSC CAPI VM creation','Actual JavaScript iteration and JSON round trip','Original full ICU Intl UTC date formatting','JSGarbageCollect and context release;exact physical/commit/pool/futex baseline'],'limits':['Initial VM execution fixture only;not ECMAScript/Wasm/full GC conformance or modern WebCore/browser qualification','Diagnostic ISO contains no installer payload;normal distribution media unchanged','QEMU only;new Fusion/Pi5/laptop execution NOT RUN;Stage3 incomplete']},indent=2)+'\n')
except Exception as error:
 try:
  g.serial('procinfo');g.serial('taskinfo');time.sleep(.3)
  (out/'failure-registers.txt').write_text(g.hmp('info registers'));g.hmp('screendump '+str(out/'failure.ppm'))
 except Exception:pass
 (out/'failure.txt').write_text(g.text());(out/'result.json').write_text(json.dumps({'status':'FAIL','error':str(error),'VM_qualified':False},indent=2)+'\n');raise
finally:g.close()

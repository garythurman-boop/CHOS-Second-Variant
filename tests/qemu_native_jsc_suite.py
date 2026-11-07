#!/usr/bin/env python3
"""Actual native Test262,checked Wasm and watchdog probes in disposable guests."""
import argparse,pathlib,shutil,json,hashlib,struct,re,time
from qemu_platform import Guest,wait
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True)
p.add_argument('--mode',choices=['direct','uefi'],default='direct')
p.add_argument('--suite',choices=['test262','wasm','watchdog','async-wasm','mixed'],required=True)
p.add_argument('--runs',type=int,choices=[1,3],default=1)
p.add_argument('--image-root',type=pathlib.Path,default=pathlib.Path('build/engine-test'),help='Frozen diagnostic kernel/ISO directory')
p.add_argument('--application',type=pathlib.Path,default=pathlib.Path('build/engine-test/native-engine.elf'))
o=p.parse_args();out=o.out.resolve();out.mkdir(parents=True,exist_ok=False)
disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
a=argparse.Namespace(mode=o.mode,kernel=o.image_root/'chos.elf',uefi_kernel=o.image_root/'uefi-kernel.elf',iso=o.image_root/'chos.iso',memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk)
markers={'test262':'[ OK ] actual pinned Test262 selected native suite executed','wasm':'[ OK ] actual checked native Wasm arithmetic,memory growth,tables and bounds traps executed','watchdog':'[ OK ] actual native JSC CPU watchdog terminates long scripts and respects callback continuation','async-wasm':'[ OK ] actual native RunLoop,async Wasm compile/instantiate and Promise completion executed','mixed':'[ OK ] actual native Wasm,async main loop and CPU watchdog coexist'}
def free_pages():
 end=time.monotonic()+15
 while True:
  header='CHOS process table (foreground and scheduled EL0 processes)';before=g.text().count(header);g.serial('procinfo')
  wait(lambda:g.text().count(header)>before,4,'process diagnostic')
  if re.search(r'Active: 0;.*pending cleanup: 0',g.text().rsplit(header,1)[-1]):break
  assert time.monotonic()<end,'Native JSC suite cleanup did not quiesce'
 before=len(re.findall(r'Free pages: (\d+)',g.text()));g.serial('meminfo')
 wait(lambda:len(re.findall(r'Free pages: (\d+)',g.text()))>before,4,'free page count')
 assert g.symbol('committed_frame_limits')==0 and g.symbol('engine_owner_slot')==0xffffffff
 return int(re.findall(r'Free pages: (\d+)',g.text())[-1])
try:
 raw=(out/'kernel.elf').read_bytes();start=g.symbols['chos_romfs_engine_start'];end=g.symbols['chos_romfs_engine_end'];ph=struct.unpack_from('<Q',raw,32)[0];embedded=None
 for i in range(struct.unpack_from('<H',raw,56)[0]):
  kind,flags,f,va,pa,n,memory,alignment=struct.unpack_from('<IIQQQQQQ',raw,ph+i*56)
  if kind==1 and va<=start<end<=va+n:embedded=raw[f+start-va:f+end-va]
 assert embedded==o.application.read_bytes(),'Actual embedded executable differs from staged input'
 wait(lambda:'chos> ' in g.text(),60,'diagnostic scheduled runtime')
 if o.mode=='uefi':assert 'UEFI CPU isolation active' in g.text()
 wait(lambda:g.symbol('splash_active')==0,10,'splash');cold=free_pages();runs=[]
 for run in range(o.runs):
  if g.symbol('view')==4:g.hmp('sendkey esc');wait(lambda:g.symbol('view')!=4,3,'reset desktop')
  offset=len(g.text());started=time.monotonic();g.serial('exec /BIN/JSCVM.ELF '+o.suite);next_info=started+5;response=None
  if o.suite in ['watchdog','mixed']:
   wait(lambda:'[INFO] actual native JSC long script begins' in g.text()[offset:] or '[FAIL]' in g.text()[offset:] or '[USER FAULT]' in g.text()[offset:],30,'native long script entry')
   assert '[INFO] actual native JSC long script begins' in g.text()[offset:]
   before=time.monotonic();g.hmp('sendkey f1');wait(lambda:g.symbol('view')==4,1,'actual HID desktop during native long script')
   response=time.monotonic()-before
   assert markers[o.suite] not in g.text()[offset:],'Long-script work must still be active during the input observation'
  while markers[o.suite] not in g.text()[offset:]:
   text=g.text()[offset:]
   assert not any(v in text for v in ['[FATAL]','[USER FAULT]','ASSERTION FAILED']),text
   assert time.monotonic()-started<(600 if o.suite=='test262' else 120),'Native '+o.suite+' timed out'
   if time.monotonic()>=next_info:
    g.serial('procinfo');next_info=time.monotonic()+5
    assert not re.search(r'pid=\d+[^\r\n]*state=(?:exited|failed)[^\r\n]*exit=(?:[1-9]\d*|-\d+)',text),text
   time.sleep(.05)
  text=g.text()[offset:];assert '[FAIL]' not in text and '[TEST262 FAIL]' not in text,text
  pid=int(re.search(r'pid=(\d+) path=/BIN/JSCVM.ELF',text).group(1));assert free_pages()==cold
  section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
  assert re.search(r'\bpid='+str(pid)+r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b',section),section
  if o.suite=='test262':
   assert '[TEST262 TOTAL] passed=172 failed=0' in text
   assert len(re.findall(r'\[TEST262 PASS\]',text))==172
  if o.suite in ['watchdog','mixed']:
   assert '[WATCHDOG] threshold=1 calls=1 terminated=1' in text and '[WATCHDOG] threshold=2 calls=2 terminated=1' in text
   for observation in ['default calls=0','reset calls=2','clear calls=1']:assert '[WATCHDOG] '+observation+' passed=1' in text
  runs.append({'pid':pid,'seconds':time.monotonic()-started,'desktop_response_seconds_during_long_script':response})
 assert g.symbol('futex_queued_waiters')==0
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'suite':o.suite,'application_sha256':hashlib.sha256(embedded).hexdigest(),'cold_free_pages':cold,'final_free_pages':free_pages(),'runs':runs,'checks':['Actual native original JSC execution,not host JavaScript','Exact positive suite marker,normal process exit and cold physical/commit/engine-pool/futex baseline'],'limits':['Selected Test262 cases/checked Wasm/watchdog probes only;not complete standards or security conformance','Diagnostic ISO has no installer;modern WebCore/browser,native WiFi and production browser policy remain open','QEMU only;new Fusion/Pi5/laptop acceptance NOT RUN;Stage3 incomplete']},indent=2)+'\n')
except Exception as error:
 try:g.serial('procinfo');g.serial('taskinfo');time.sleep(.3);(out/'failure-registers.txt').write_text(g.hmp('info registers'))
 except Exception:pass
 (out/'failure.txt').write_text(g.text());(out/'result.json').write_text(json.dumps({'status':'FAIL','suite':o.suite,'error':str(error),'qualified':False},indent=2)+'\n');raise
finally:g.close()

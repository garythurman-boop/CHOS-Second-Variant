#!/usr/bin/env python3
"""Exercise the actual native WTF allocator and contained protection faults."""
import argparse,pathlib,shutil,subprocess,time,json,re,hashlib
from qemu_platform import Guest,wait
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--out',type=pathlib.Path,required=True)
p.add_argument('--application',type=pathlib.Path,required=True)
p.add_argument('--mode',choices=['direct','uefi'],default='direct');o=p.parse_args()
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False)
disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/WTFALLOC.ELF',str(o.application)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=pathlib.Path('build/chos.elf'),uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=pathlib.Path('build/chos-0.30.0.iso'),memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk)
marker='[ OK ] actual WTF aligned reservation,guarded commit,decommit,zero fill and release passed'
def free_pages():
 end=time.monotonic()+15
 while True:
  header='CHOS process table (foreground and scheduled EL0 processes)';before=g.text().count(header);g.serial('procinfo')
  wait(lambda:g.text().count(header)>before,4,'process diagnostic')
  if re.search(r'Active: 0;.*pending cleanup: 0',g.text().rsplit(header,1)[-1]):break
  assert time.monotonic()<end,'WTF allocator cleanup did not quiesce'
 before=len(re.findall(r'Free pages: (\d+)',g.text()));g.serial('meminfo')
 wait(lambda:len(re.findall(r'Free pages: (\d+)',g.text()))>before,4,'free page count')
 assert g.symbol('committed_frame_limits')==0 and g.symbol('engine_owner_slot')==0xffffffff
 return int(re.findall(r'Free pages: (\d+)',g.text())[-1])
try:
 wait(lambda:'chos> ' in g.text(),45,'scheduled runtime')
 if o.mode=='uefi':assert 'UEFI CPU isolation active' in g.text()
 wait(lambda:g.symbol('splash_active')==0,10,'splash');cold=free_pages();runs=[];faults=[]
 for run in range(3):
  offset=len(g.text());g.serial('exec /WTFALLOC.ELF')
  wait(lambda:marker in g.text()[offset:] or any(v in g.text()[offset:] for v in ['[FAIL]','[USER FAULT]','[FATAL]']),45,'actual WTF allocator')
  assert marker in g.text()[offset:] and '[FAIL]' not in g.text()[offset:] and '[USER FAULT]' not in g.text()[offset:],g.text()[offset:]
  pid=re.search(r'pid=(\d+) path=/WTFALLOC.ELF',g.text()[offset:]).group(1);assert free_pages()==cold
  section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
  assert re.search(r'\bpid='+pid+r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b',section),section
  runs.append(int(pid))
 for mode in ['leftguard','rightguard','readonly','decommitted','execute']:
  offset=len(g.text());before=g.text().count('[USER FAULT]');g.serial('exec /WTFALLOC.ELF '+mode)
  wait(lambda:g.text().count('[USER FAULT]')>before or '[FAIL]' in g.text()[offset:],30,'contained WTF allocator '+mode+' fault')
  assert g.text().count('[USER FAULT]')==before+1 and '[FAIL]' not in g.text()[offset:] and free_pages()==cold
  faults.append(mode)
 assert '[FATAL]' not in g.text() and g.symbol('futex_queued_waiters')==0
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'application_sha256':hashlib.sha256(o.application.read_bytes()).hexdigest(),'cold_free_pages':cold,'final_free_pages':free_pages(),'normal_run_pids':runs,'contained_faults':faults,'checks':['Actual pinned WTF OSAllocator public methods;32MiB aligned reservation without physical backing','Guarded64KiB data alignment,virtual slack trimming and2MiB bounded commit/release','Real zero-filled commit,permission changes,preserved data,decommit,recommit,zeroFill and hints','Executable,address-hint,invalid-alignment and over-budget refusals','Exact cold physical,commit,engine-pool and futex baseline after every lifecycle'],'limits':['Selected actual allocator APIs,not full WTF/JSC/Wasm/WebCore/browser conformance','QEMU only;new Fusion/Pi5/laptop qualification NOT RUN;Stage3 incomplete']},indent=2)+'\n')
except Exception:
 try:g.serial('procinfo');g.serial('taskinfo');time.sleep(.3);(out/'failure-registers.txt').write_text(g.hmp('info registers'))
 except Exception:pass
 (out/'failure.txt').write_text(g.text());raise
finally:g.close()

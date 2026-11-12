#!/usr/bin/env python3
"""Real engine-only large malloc, guard faults and unchanged legacy admission."""
import argparse,hashlib,json,pathlib,re,subprocess,time
from qemu_platform import Guest,wait
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--out',type=pathlib.Path,required=True)
p.add_argument('--application',type=pathlib.Path,required=True)
p.add_argument('--legacy',type=pathlib.Path,required=True)
p.add_argument('--image-root',type=pathlib.Path,required=True)
p.add_argument('--mode',choices=['direct','uefi'],default='direct')
o=p.parse_args();out=o.out.resolve();out.mkdir(parents=True,exist_ok=False)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
disk=out/'disposable.img';subprocess.run(['python3','tools/mkchfs.py','create',str(disk),'--size-mib','16'],check=True)
for name,app in [('/LARGE.ELF',o.application),('/LEGACY.ELF',o.legacy)]:
 assert not subprocess.check_output(['llvm-nm','-u',str(app)],text=True).strip()
 subprocess.run(['python3','tools/mkchfs.py','put',str(disk),name,str(app)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=o.image_root/'chos.elf',uefi_kernel=o.image_root/'uefi-kernel.elf',iso=o.image_root/'chos.iso',memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk)
def cold():
 header='CHOS process table (foreground and scheduled EL0 processes)';end=time.monotonic()+15
 while True:
  n=g.text().count(header);g.serial('procinfo');wait(lambda:g.text().count(header)>n,4,'process status')
  section=g.text().rsplit(header,1)[-1]
  if re.search(r'Active: 0;.*pending cleanup: 0',section):break
  assert time.monotonic()<end,'cleanup failed';time.sleep(.03)
 n=len(re.findall(r'Free pages: (\d+)',g.text()));g.serial('meminfo')
 wait(lambda:len(re.findall(r'Free pages: (\d+)',g.text()))>n,4,'page report')
 assert g.symbol('committed_frame_limits')==0 and g.symbol('engine_owner_slot')==0xffffffff and g.symbol('futex_queued_waiters')==0
 return int(re.findall(r'Free pages: (\d+)',g.text())[-1]),section
runs=[]
try:
 wait(lambda:'chos> ' in g.text(),60,'scheduled runtime')
 if o.mode=='uefi':assert 'UEFI CPU isolation active' in g.text()
 wait(lambda:g.symbol('splash_active')==0,10,'splash');baseline,_=cold()
 for cycle in range(3):
  for path,marker in [('/LARGE.ELF','[ OK ] actual native large heap workers and cleanup passed'),('/LEGACY.ELF','[ OK ] legacy heap keeps1MiB mapping limit')]:
   offset=len(g.text());g.serial('exec '+path)
   wait(lambda:marker in g.text()[offset:] or '[FAIL]' in g.text()[offset:] or '[USER FAULT]' in g.text()[offset:],90,'native large allocation fixture')
   text=g.text()[offset:];assert marker in text and '[FAIL]' not in text and '[USER FAULT]' not in text,text
   pages,section=cold();assert pages==baseline,(baseline,pages)
   pid=int(re.search(r'pid=(\d+) path='+re.escape(path),g.text()[offset:])[1])
   assert re.search(r'\bpid='+str(pid)+r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b',section),section
   runs.append({'cycle':cycle,'path':path,'pid':pid,'cold_pages_restored':pages})
  for guard in ['low','high']:
   offset=len(g.text());g.serial('exec /LARGE.ELF '+guard)
   wait(lambda:'[USER FAULT]' in g.text()[offset:] or '[FAIL]' in g.text()[offset:],20,'actual allocation guard fault')
   text=g.text()[offset:];assert '[ OK ] entering actual large allocation guard fault' in text and text.count('[USER FAULT]')==1 and '[FAIL]' not in text,text
   pages,section=cold();assert pages==baseline,(baseline,pages)
   pid=int(re.search(r'pid=(\d+) path=/LARGE.ELF',g.text()[offset:])[1])
   assert re.search(r'\bpid='+str(pid)+r'\b[^\r\n]*state=failed\b',section),section
   runs.append({'cycle':cycle,'guard':guard,'pid':pid,'isolated_user_fault':True,'cold_pages_restored':pages})
 assert '[FATAL]' not in g.text() and '[FAIL]' not in g.text()
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'application_sha256':sha(o.application),'legacy_sha256':sha(o.legacy),'cold_free_pages':baseline,'runs':runs,'checks':['Actual engine malloc contiguous8/16MiB and32MiB backing boundary,complete byte patterns/zeroing,alignment and realloc preservation','Three concurrent native workers per engine process,zero native heap before return','Unchanged legacy1MiB mapping admission','Actual low/high unmapped guard writes fault only owning process','Three lifecycles,normal exits and exact cold physical-page/committed-frame/engine/futex cleanup'],'limits':['Engine-only allocator prerequisite;not full browser/WebCore qualification','Kernel physical work remains<=1MiB per syscall;user allocator composes transactions','QEMU only;newFusion/Pi5/laptop execution NOT RUN']},indent=2)+'\n')
except Exception as error:
 try:g.serial('procinfo');g.serial('meminfo');time.sleep(.2);g.screen('failure')
 except Exception:pass
 (out/'result.json').write_text(json.dumps({'status':'FAIL','error':str(error)},indent=2)+'\n');raise
finally:g.close()

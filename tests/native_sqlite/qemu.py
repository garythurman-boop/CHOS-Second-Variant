#!/usr/bin/env python3
"""Execute native SQLite memory SQL and real threaded cleanup on disposable CHOS."""
import pathlib,sys,argparse,json,hashlib,re,subprocess,time,shutil
root=pathlib.Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'tests'))
from qemu_platform import Guest,wait
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--application',type=pathlib.Path,required=True);p.add_argument('--image-root',type=pathlib.Path,required=True);p.add_argument('--mode',choices=['direct','uefi'],default='direct');p.add_argument('--missing-entropy',action='store_true');o=p.parse_args()
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False);app=out/'application.elf';shutil.copyfile(o.application,app)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
disk=out/'disposable.img';subprocess.run(['python3',str(root/'tools/mkchfs.py'),'create',str(disk),'--size-mib','16'],check=True);subprocess.run(['python3',str(root/'tools/mkchfs.py'),'put',str(disk),'/SQLITE.ELF',str(app)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=o.image_root/'chos.elf',uefi_kernel=o.image_root/'uefi-kernel.elf',iso=o.image_root/'chos.iso',memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=o.missing_entropy,hub_depth=0,empty_usb=False,tablet=True)
g=None
def cold():
 header='CHOS process table (foreground and scheduled EL0 processes)';deadline=time.monotonic()+15
 while True:
  n=g.text().count(header);g.serial('procinfo');wait(lambda:g.text().count(header)>n,4,'process status')
  section=g.text().rsplit(header,1)[-1]
  if re.search(r'Active: 0;.*pending cleanup: 0',section):break
  assert time.monotonic()<deadline,'Process cleanup not quiescent';time.sleep(.03)
 n=len(re.findall(r'Free pages: (\d+)',g.text()));g.serial('meminfo');wait(lambda:len(re.findall(r'Free pages: (\d+)',g.text()))>n,4,'physical page report')
 assert g.symbol('committed_frame_limits')==0 and g.symbol('engine_owner_slot')==0xffffffff and g.symbol('futex_queued_waiters')==0
 return int(re.findall(r'Free pages: (\d+)',g.text())[-1]),section
runs=[]
try:
 g=Guest(a,out,disk)
 wait(lambda:'chos> ' in g.text(),60,'scheduled runtime');wait(lambda:g.symbol('splash_active')==0,10,'desktop splash')
 if o.mode=='uefi':assert 'UEFI CPU isolation active' in g.text()
 baseline,_=cold()
 marker='[ OK ] SQLite refuses initialization without entropy' if o.missing_entropy else '[ OK ] native SQLite in-memory resources cleaned up'
 for cycle in range(3):
  offset=len(g.text());g.serial('exec /SQLITE.ELF'+(' entropy-failure' if o.missing_entropy else ''))
  wait(lambda:marker in g.text()[offset:] or any(x in g.text()[offset:] for x in ['[FAIL]','[FATAL]','[USER FAULT]']),90,'actual SQLite fixture')
  text=g.text()[offset:];assert marker in text and not any(x in text for x in ['[FAIL]','[FATAL]','[USER FAULT]']),text
  if not o.missing_entropy:
   assert '[ OK ] actual SQLite statements,transactions,bindings,errors and allocation bounds passed' in text
   assert '[ OK ] actual SQLite pthread serialized and independent connections passed' in text
  pages,section=cold();assert pages==baseline,(baseline,pages)
  pid=int(re.search(r'pid=(\d+) path=/SQLITE.ELF',g.text()[offset:])[1]);assert re.search(r'\bpid='+str(pid)+r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b',section),section
  runs.append({'cycle':cycle,'pid':pid,'cold_pages_restored':pages})
 checks=(['Missing OS entropy makes sqlite3_initialize return SQLITE_IOERR',
          'Failed initialization shuts down with zero SQLite and native heap allocations']
         if o.missing_entropy else
         ['Actual SQLite 3.54.1 memory SQL, transactions, UTF-8/blob/real bindings, metadata, JSON and date/error tests',
          'Real recursive mutex contention, three pthread workers, 900 serialized shared connection updates and independent databases',
          'Actual hard heap quota rejection/recovery, disk file/extension refusal and complete SQLite/native heap cleanup'])
 checks.append('Three normal exits with exact physical-page, engine, commit and futex baseline restoration')
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'missing_entropy':o.missing_entropy,'application_sha256':sha(app),'cold_free_pages':baseline,'runs':runs,'checks':checks,'limits':['Selected native memory SQL prerequisite; persistent disk VFS, WAL/browser storage and full SQLite conformance remain unqualified','Guest wall clock used; production trusted time policy remains separate','QEMU only; physical Fusion/Pi 5/laptop not run; Stage 3 incomplete']},indent=2)+'\n')
except Exception as error:
 try:g.serial('procinfo');g.serial('meminfo');time.sleep(.2);g.screen('failure')
 except Exception:pass
 (out/'result.json').write_text(json.dumps({'status':'FAIL','error':str(error)},indent=2)+'\n');raise
finally:
 if g is not None:g.close()

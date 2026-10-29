#!/usr/bin/env python3
"""Actual upstream FilePrintStream and native standard output lifecycle checks."""
import argparse,pathlib,shutil,subprocess,sys,json,re,time,hashlib
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--application',type=pathlib.Path,required=True);p.add_argument('--mode',choices=['direct','uefi'],default='direct');o=p.parse_args()
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False);disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
app=o.application;app_sha=hashlib.sha256(app.read_bytes()).hexdigest()
subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/BIN/WTFOUT.ELF',str(app)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=pathlib.Path('build/chos.elf'),uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=pathlib.Path('build/chos-0.30.0.iso'),memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk)
marker='[ OK ] actual WTF output and native standard streams passed'
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
  offset=len(g.text());n=g.text().count(marker);g.serial('exec /BIN/WTFOUT.ELF')
  wait(lambda:g.text().count(marker)>n or '[FAIL]' in g.text(),60,'native WTF fixture')
  assert '[FAIL]' not in g.text() and '[USER FAULT]' not in g.text() and '[FATAL]' not in g.text(),g.text()
  assert g.text()[offset:].count('[EXPECT] native output child refusal')==2,g.text()[offset:]
  # The recovery shell emits its prompt independently of the process's FILE lock.
  # Remove only its exact prompt and structured launch acknowledgment. Public FILE
  # serialization covers this process's writers, not independent kernel diagnostics.
  section=g.text()[offset:].replace('chos> ','')
  section=re.sub(r'\[SPAWNED\] pid=\d+ path=/BIN/WTFOUT\.ELF \(running asynchronously\)\r?\n','',section)
  for expected in ['[STREAM] native 002a 1.25 -123','[STREAM] native stderr','[STREAM] binary128 1.2344 count=512 utf8=café','[STREAM] actual upstream FilePrintStream 42 1.25','[STREAM] borrowed stream remains open','[STREAM] stderr survives stdout closure']:
   assert expected in section,(expected,section)
  assert 'FORBIDDEN' not in section
  lines=re.findall(r'^\[STREAM\] worker=(\d+) item=(\d+) complete\r?$',section,re.M)
  assert len(lines)==54,len(lines)
  for worker in range(3):
   for item in range(6):assert lines.count((str(worker),str(item)))==3,(worker,item,lines)
  observed=free_pages()
  pid=re.search(r'pid=(\d+) path=/BIN/WTFOUT.ELF',g.text()[offset:]).group(1)
  section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
  assert re.search(r'\bpid='+pid+r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b',section),section
  if run==0:baseline=observed;assert baseline==cold,(cold,baseline)
  else:assert observed==baseline,('native WTF leaked pages',baseline,observed)
 preemptions_after=g.symbol('preemption_switches');assert preemptions_after>preemptions_before
 assert g.symbol('futex_queued_waiters')==0
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'application_sha256':app_sha,'source':'disposable CHFS native application','cold_free_pages':cold,'free_pages_before':baseline,'free_pages_after':free_pages(),'timer_preemptions_before':preemptions_before,'timer_preemptions_after':preemptions_after, 'checks':['actual upstream FilePrintStream/PrintStream native console output',
 'mixed integer/double/binary128/UTF8 printf ABI and exact output lengths',
 '54 complete output lines across three concurrent workers and cycles per process',
 'read snapshots stay read-only;denied writer and logical stdout close refuse writes',
 'stderr and borrowed output remain open;zero user heap,three process lifecycles,strict cold PMM and no queued futexes'],
 'limits':['native standard-stream component,not complete WTF/engine/browser or writable ordinary files',
 'QEMU evidence,new Fusion/Pi/laptop qualification NOT RUN']},indent=2)+'\n')
except Exception:
 try:
  g.serial('procinfo');time.sleep(.4);g.serial('threads');time.sleep(.4)
 except Exception:pass
 (out/'failure.txt').write_text(g.text());raise
finally:g.close()

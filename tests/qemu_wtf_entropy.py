#!/usr/bin/env python3
"""Actual upstream WTF OS entropy, PID and page adapters with fail-closed recovery."""
import argparse,pathlib,shutil,subprocess,sys,json,re,time,hashlib
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--application',type=pathlib.Path,required=True);p.add_argument('--mode',choices=['direct','uefi'],default='direct');p.add_argument('--no-rng',action='store_true');o=p.parse_args();assert not o.no_rng or o.mode=='direct','missing entropy uses direct boot without firmware RNG'
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False);disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
app=o.application;app_sha=hashlib.sha256(app.read_bytes()).hexdigest()
subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/BIN/WTFENT.ELF',str(app)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=pathlib.Path('build/chos.elf'),uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=pathlib.Path('build/chos-0.30.0.iso'),memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=o.no_rng,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk)
marker='[ OK ] native WTF OS entropy, process and page services passed'
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
  offset=len(g.text());n=g.text().count(marker);g.serial('exec /BIN/WTFENT.ELF'+(' missing-entropy' if o.no_rng else ''))
  wait(lambda:('[EXPECT] native WTF entropy error fails closed' in g.text()[offset:] if o.no_rng else g.text().count(marker)>n) or '[FAIL]' in g.text(),30,'native WTF fixture')
  assert '[FAIL]' not in g.text() and '[USER FAULT]' not in g.text() and '[FATAL]' not in g.text(),g.text()
  assert g.text()[offset:].count('[EXPECT] native WTF ')==(1 if o.no_rng else 3),g.text()[offset:]
  observed=free_pages()
  pid=re.search(r'pid=(\d+) path=/BIN/WTFENT.ELF',g.text()[offset:]).group(1)
  section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
  assert re.search(r'\bpid='+pid+r'\b[^\r\n]*state=exited\b[^\r\n]*exit='+('134' if o.no_rng else '0')+r'\b',section),section
  if run==0:baseline=observed;assert baseline==cold,(cold,baseline)
  else:assert observed==baseline,('native WTF leaked pages',baseline,observed)
  if o.no_rng:
   offset=len(g.text());g.serial('exec /BIN/WTFENT.ELF empty')
   wait(lambda:'[ OK ] native WTF empty request works without entropy' in g.text()[offset:],8,'post-failure empty entropy request')
   assert free_pages()==cold
   section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
   pid=re.search(r'pid=(\d+) path=/BIN/WTFENT.ELF',g.text()[offset:]).group(1)
   assert re.search(r'\bpid='+pid+r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b',section),section
 preemptions_after=g.symbol('preemption_switches');assert preemptions_after>preemptions_before
 assert g.symbol('futex_queued_waiters')==0
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'application_sha256':app_sha,'source':'disposable CHFS native application','cold_free_pages':cold,'free_pages_before':baseline,'free_pages_after':free_pages(),'timer_preemptions_before':preemptions_before,'timer_preemptions_after':preemptions_after, 'entropy_available':not o.no_rng,'checks':['actual original WTF RandomDevice/PageBlock and ProcessID native adapters',
 'bounded256-byte requests,600-byte canaries and empty requests',
 'three workers sharing stateless source;three worker and process lifecycles',
 'denied RNG/PID and read-only output abort134 without user fault' if not o.no_rng else 'missing entropy abort134 within bounded retry budget;empty request and new process still run',
 'zero user heap,strict cold PMM equality,preemption and zero queued futexes'],
 'limits':['native OS-service component,not entropy quality certification,full WTF,engine or browser',
 'QEMU evidence,new Fusion/Pi/laptop qualification NOT RUN']},indent=2)+'\n')
except Exception:
 try:
  g.serial('procinfo');time.sleep(.4);g.serial('threads');time.sleep(.4)
 except Exception:pass
 (out/'failure.txt').write_text(g.text());raise
finally:g.close()

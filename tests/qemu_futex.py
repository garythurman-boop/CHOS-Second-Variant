#!/usr/bin/env python3
"""Real private futex compare/sleep/wake, mutex contention and cancellation."""
import argparse,pathlib,shutil,subprocess,sys,json,re,time,hashlib
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--mode',choices=['direct','uefi'],default='direct');o=p.parse_args()
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False);disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
app=pathlib.Path('build/userland/futex_test.elf');app_sha=hashlib.sha256(app.read_bytes()).hexdigest()
subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/BIN/FUTEX.ELF',str(app)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=pathlib.Path('build/chos.elf'),uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=pathlib.Path('build/chos-0.30.0.iso'),memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk)
marker='[ OK ] private futex mutex, bounded wake, timeout and repeated lifecycle passed'
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
 wait(lambda:'chos> ' in g.text(),45,'scheduled runtime')
 if o.mode=='uefi':assert 'UEFI CPU isolation active' in g.text()
 wait(lambda:g.symbol('splash_active')==0,10,'splash')
 cold=free_pages()
 baseline=None
 preemptions_before=g.symbol("preemption_switches")
 for run in range(3):
  n=g.text().count(marker);g.serial('exec /BIN/FUTEX.ELF')
  wait(lambda:g.text().count(marker)>n or '[FAIL]' in g.text(),45,'actual native shared-AS thread lifecycle')
  assert '[FAIL]' not in g.text() and '[USER FAULT]' not in g.text() and '[FATAL]' not in g.text(),g.text()
  assert g.symbol('futex_queued_waiters')==0,'normal futex record retained'
  observed=free_pages()
  if run==0:baseline=observed # existing kernel heap retains its first file-buffer arena
  else:assert observed==baseline,('threads leaked pages after warmup',baseline,observed)
 assert g.text().count('[ OK ] private futex foreign process cannot wake matching virtual address')==9
 preemptions_after=g.symbol('preemption_switches')
 assert preemptions_after>preemptions_before,(preemptions_before,preemptions_after)
 # Main EXIT must cancel workers sleeping inside FUTEX_WAIT.
 before=g.text().count('[ OK ] private futex process exit cancels sleeping waiters')
 g.serial('exec /BIN/FUTEX.ELF exit')
 wait(lambda:g.text().count('[ OK ] private futex process exit cancels sleeping waiters')>before,10,'active cancellation workers')
 assert free_pages()==baseline,'process exit with live threads leaked pages'
 assert g.symbol('futex_queued_waiters')==0,'cancelled futex records remain queued'
 n=g.text().count(marker);g.serial('exec /BIN/FUTEX.ELF')
 wait(lambda:g.text().count(marker)>n or '[FAIL]' in g.text(),45,'post-cancellation thread and owner reuse')
 assert '[FAIL]' not in g.text() and '[FATAL]' not in g.text(),g.text()
 assert free_pages()==baseline,'post-cancellation thread groups leaked pages'
 counters={name:int.from_bytes(g.block(name,size=8),'little') for name in ['futex_wait_calls','futex_wake_events','futex_timeouts','futex_cancellations']}
 assert counters['futex_wait_calls']>0 and counters['futex_wake_events']>0 and counters['futex_timeouts']>0 and counters['futex_cancellations']>=2,counters
 assert g.symbol('futex_queued_waiters')==0,'completed guest retains futex records'
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'application_sha256':app_sha,'kernel_futex_counters':counters,'cold_free_pages':cold,'free_pages_before':baseline,'free_pages_after':free_pages(), 'timer_preemptions_before':preemptions_before,'timer_preemptions_after':preemptions_after,'checks':['three actual EL0 workers contend, yield while holding mutex and preserve600 exact updates per lifecycle','same virtual address in foreign process cannot wake private-key waiters','wake counts2 and1 are bounded and condition must be rechecked after wake','mismatched value returns EAGAIN without sleeping; timeout and invalid word/width/capacity rejected','three worker lifecycles per process, repeated process launches and stable physical pages','process exit cancels futex sleepers and leaves zero queued wait records','post-cancellation normal process succeeds'],'limits':['native private bounded futex; not shared-process futex, POSIX pthreads or compiler ELF TLS','new Fusion/physical behavior NOT RUN','modern engine and Stage3 remain incomplete']},indent=2))
except Exception:
 (out/'failure.txt').write_text(g.text());raise
finally:g.close()

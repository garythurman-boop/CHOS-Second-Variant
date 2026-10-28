#!/usr/bin/env python3
"""Large native image,readonly/XN boundaries and frame-budget/cleanup qualification."""
import argparse,pathlib,shutil,subprocess,sys,json,re,time,hashlib
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--application',type=pathlib.Path);p.add_argument('--mode',choices=['direct','uefi'],default='direct');o=p.parse_args()
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False);disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
app=o.application or pathlib.Path('build/userland/large_image_test.elf');app_sha=hashlib.sha256(app.read_bytes()).hexdigest()
subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/BIN/LARGE.ELF',str(app)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=pathlib.Path('build/chos.elf'),uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=pathlib.Path('build/chos-0.30.0.iso'),memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk)
marker='[ OK ] native3MiB read-only image and1MiB private mapping passed'
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
  offset=len(g.text());n=g.text().count(marker);g.serial('exec /BIN/LARGE.ELF')
  wait(lambda:g.text().count(marker)>n or '[FAIL]' in g.text(),60,'native large image fixture')
  assert '[FAIL]' not in g.text() and '[USER FAULT]' not in g.text() and '[FATAL]' not in g.text(),g.text()
  observed=free_pages()
  pid=re.search(r'pid=(\d+) path=/BIN/LARGE.ELF',g.text()[offset:]).group(1)
  section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
  assert re.search(r'\bpid='+pid+r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b',section),section
  if run==0:baseline=observed
  else:assert observed==baseline,('base C leaked pages',baseline,observed)
 faults=[]
 symbols={line.split()[-1]:int(line.split()[0],16) for line in subprocess.check_output(['llvm-nm','--defined-only',str(app)],text=True).splitlines() if len(line.split())==3}
 expected_far=symbols['chos_large_read_only']+2*1024*1024
 for mode,ec in [('write-ro',0x24),('execute-ro',0x20)]:
  offset=len(g.text());g.serial('exec /BIN/LARGE.ELF '+mode)
  wait(lambda:re.search(r'\[USER FAULT\][^\r\n]*ESR=0x[0-9A-Fa-f]{16}[^\r\n]*FAR=0x[0-9A-Fa-f]{16}[\r\n]',g.text()[offset:]),15,'complete large image '+mode+' fault record')
  matches=re.findall(r'\[USER FAULT\][^\r\n]*ESR=0x([0-9A-Fa-f]+)[^\r\n]*FAR=0x([0-9A-Fa-f]+)',g.text())
  esr,far=map(lambda v:int(v,16),matches[-1]);assert esr>>26==ec and far==expected_far,(mode,hex(esr),hex(far))
  assert '[FATAL]' not in g.text();free_pages();faults.append({'mode':mode,'esr':esr,'far':far})
 n=g.text().count(marker);g.serial('exec /BIN/LARGE.ELF');wait(lambda:g.text().count(marker)>n,60,'large image recovery after faults')
 assert free_pages()==baseline
 preemptions_after=g.symbol('preemption_switches');assert preemptions_after>preemptions_before
 assert g.symbol('futex_queued_waiters')==0
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'application_sha256':app_sha,'source':'disposable CHFS native application','cold_free_pages':cold,'free_pages_before':baseline,'free_pages_after':free_pages(),'timer_preemptions_before':preemptions_before,'timer_preemptions_after':preemptions_after, 'checks':['actual3MiB readonly native segment crosses previous2MiB image bound and multiple L3 tables',
 'actual1MiB private mapping plus image/stack exceeds old4MiB owned-frame budget',
 'every readonly payload byte and zeroed private byte checked;initial writable data/BSS preserved',
 'write-to-RO and execute-from-RO above2MiB fault at exact expected address and correct architectural class',
 'normal/abandoned/faulted application cleanup,recovery,stable warm frames and zero private futex waiters'],
 'contained_faults':faults,
 'limits':['4MiB image/8MiB owned-frame profile,not the larger footprint required by the full engine/data',
 'QEMU evidence,new Fusion/Pi/laptop qualification NOT RUN']},indent=2)+'\n')
except Exception:
 try:
  g.serial('procinfo');time.sleep(.4);g.serial('threads');time.sleep(.4)
 except Exception:pass
 (out/'failure.txt').write_text(g.text());raise
finally:g.close()

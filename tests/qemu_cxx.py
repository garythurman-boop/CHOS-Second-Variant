#!/usr/bin/env python3
"""Native ARM64 libc++ containers,allocation and controlled failure isolation."""
import argparse,pathlib,shutil,subprocess,sys,json,re,time,hashlib
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--mode',choices=['direct','uefi'],default='direct');o=p.parse_args()
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False);disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
app=pathlib.Path('build/userland/cxx_test.elf');app_sha=hashlib.sha256(app.read_bytes()).hexdigest()
subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/BIN/CXX.ELF',str(app)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=pathlib.Path('build/chos.elf'),uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=pathlib.Path('build/chos-0.30.0.iso'),memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk)
marker='[ OK ] native libc++ containers,RAII,allocation,atomics,threads and filenames passed'
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
 assert not subprocess.check_output(['llvm-nm','-u',str(app)],text=True).strip(),'unresolved C++ import'
 sections=subprocess.check_output(['llvm-readelf','--sections','--wide',str(app)],text=True)
 assert not re.search(r'\.(init_array|fini_array|ctors|dtors)\b',sections),'fixture requires unimplemented global CRT'
 headers=subprocess.check_output(['llvm-readelf','-lW',str(app)],text=True)
 assert not re.search(r'^\s+TLS\s',headers,re.M)
 wait(lambda:'chos> ' in g.text(),45,'scheduled runtime')
 if o.mode=='uefi':assert 'UEFI CPU isolation active' in g.text()
 wait(lambda:g.symbol('splash_active')==0,10,'splash')
 cold=free_pages();baseline=None;preemptions_before=g.symbol('preemption_switches')
 for run in range(3):
  n=g.text().count(marker);g.serial('exec /BIN/CXX.ELF')
  wait(lambda:g.text().count(marker)>n or '[FAIL]' in g.text(),30,'native C++ runtime')
  assert '[FAIL]' not in g.text() and '[USER FAULT]' not in g.text() and '[FATAL]' not in g.text(),g.text()
  observed=free_pages()
  if run==0:baseline=observed
  else:assert observed==baseline,('native C++ leaked pages',baseline,observed)
 preemptions_after=g.symbol('preemption_switches');assert preemptions_after>preemptions_before
 for argument,diagnostic in [('oom','libc++ termination: bad_alloc was thrown in -fno-exceptions mode'),('bounds','[USER FAULT]')]:
  offset=len(g.text());g.serial('exec /BIN/CXX.ELF '+argument)
  wait(lambda:diagnostic in g.text()[offset:] or '[FAIL]' in g.text()[offset:],10,'controlled '+argument)
  assert '[FAIL]' not in g.text() and '[FATAL]' not in g.text() and free_pages()==baseline
  pid=re.search(r'pid=(\d+) path=/BIN/CXX.ELF',g.text()[offset:]).group(1)
  section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
  assert re.search(r'\bpid='+pid+r'\b[^\r\n]*exit='+('134' if argument=='oom' else '-188')+r'\b',section),section
  if argument=='bounds':assert 'state=failed' in section and 'ESR=0x00000000F2000001' in section
 n=g.text().count(marker);g.serial('exec /BIN/CXX.ELF')
 wait(lambda:g.text().count(marker)>n or '[FAIL]' in g.text(),30,'C++ relaunch after failures')
 assert '[FAIL]' not in g.text() and free_pages()==baseline
 assert g.symbol('futex_queued_waiters')==0
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'application_sha256':app_sha,'cold_free_pages':cold,'free_pages_before':baseline,'free_pages_after':free_pages(),'timer_preemptions_before':preemptions_before,'timer_preemptions_after':preemptions_after,'checks':['native array/vector/string,default integer sort/search,unique_ptr and RAII','regular/array/aligned/nothrow new/delete,handler retry and overflow refusal','three raw pthread workers use native containers and lock-free atomics across scheduling','file rename/read/remove,empty/nonempty directory,missing/protected paths and no-replace destination','three worker lifecycles and repeated processes return heap and PMM frames','OOM abort and hardening bounds fault are process-local with successful relaunch','no unresolved imports,global initarray or PT_TLS dependency; no retained futex waiters'],'limits':['static C++20 no-exceptions/no-RTTI container/allocation profile,not full libc++/libc++abi','std::thread/mutex runtime,global constructors/static guards and process atexit remain absent','binary128 default sort requires compiler-rt __lttf2 and is not linked by this profile','no stdio streams/formatting/locale/full POSIX or JavaScriptCore execution','new Fusion/physical behavior NOT RUN; modern browser and Stage3 incomplete']},indent=2)+'\n')
except Exception:
 (out/'failure.txt').write_text(g.text());raise
finally:g.close()

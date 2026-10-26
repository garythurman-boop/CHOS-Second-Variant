#!/usr/bin/env python3
"""Compiler-emulated C/C++ TLS, destructor ordering and bounded failure cleanup."""
import argparse,pathlib,shutil,subprocess,sys,json,re,time,hashlib
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--mode',choices=['direct','uefi'],default='direct');o=p.parse_args()
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False);disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
app=pathlib.Path('build/userland/emutls_test.elf');app_sha=hashlib.sha256(app.read_bytes()).hexdigest()
subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/BIN/EMUTLS.ELF',str(app)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=pathlib.Path('build/chos.elf'),uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=pathlib.Path('build/chos-0.30.0.iso'),memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk)
marker='[ OK ] compiler C/C++ TLS isolation, alignment, constructors and ordered cleanup passed'
main_marker='[ OK ] main C++ thread_local destructors retained compiler TLS'
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
 headers=subprocess.check_output(['llvm-readelf','--program-headers','--wide',str(app)],text=True)
 assert not re.search(r'^\s+TLS\s',headers,re.M),'fixture accidentally relies on unsupported ELF PT_TLS'
 symbols=subprocess.check_output(['llvm-nm',str(app)],text=True)
 tls_controls=len(re.findall(r'__emutls_v[.]',symbols));assert tls_controls>=48,tls_controls
 assert not subprocess.check_output(['llvm-nm','-u',str(app)],text=True).strip(),'unresolved native runtime import'
 wait(lambda:'chos> ' in g.text(),45,'scheduled runtime')
 if o.mode=='uefi':assert 'UEFI CPU isolation active' in g.text()
 wait(lambda:g.symbol('splash_active')==0,10,'splash')
 cold=free_pages();baseline=None;preemptions_before=g.symbol('preemption_switches')
 for run in range(3):
  n=g.text().count(marker);m=g.text().count(main_marker);g.serial('exec /BIN/EMUTLS.ELF')
  wait(lambda:(g.text().count(marker)>n and g.text().count(main_marker)>m) or '[FAIL]' in g.text(),45,'native compiler TLS and normal-main C++ finalizers')
  assert '[FAIL]' not in g.text() and '[USER FAULT]' not in g.text() and '[FATAL]' not in g.text(),g.text()
  observed=free_pages()
  if run==0:baseline=observed
  else:assert observed==baseline,('compiler TLS leaked pages',baseline,observed)
 preemptions_after=g.symbol('preemption_switches');assert preemptions_after>preemptions_before
 marker_exit='[ OK ] compiler TLS main pthread_exit preserves destructor order and live worker'
 n=g.text().count(marker_exit);g.serial('exec /BIN/EMUTLS.ELF main-exit')
 wait(lambda:g.text().count(marker_exit)>n or '[FAIL]' in g.text(),20,'main TLS cleanup while worker survives')
 assert '[FAIL]' not in g.text() and free_pages()==baseline
 for mode,begin in [('key-quota','key quota refusal'),('dtor-quota','destructor quota refusal'),('dtor-loop','destructor rearm bound')]:
  offset=len(g.text());g.serial('exec /BIN/EMUTLS.ELF '+mode)
  wait(lambda:begin+' begins' in g.text()[offset:] or '[FAIL]' in g.text(),10,'bounded '+mode)
  assert '[FAIL]' not in g.text() and free_pages()==baseline
  pid=re.search(r'pid=(\d+) path=/BIN/EMUTLS.ELF',g.text()[offset:]).group(1)
  section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
  assert re.search(r'\bpid='+pid+r'\b[^\r\n]*exit=134\b',section),section
 assert '[USER FAULT]' not in g.text() and '[FATAL]' not in g.text()
 assert g.symbol('futex_queued_waiters')==0,'compiler TLS retained wait records'
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'application_sha256':app_sha,'compiler_tls_controls':tls_controls,'elf_pt_tls':False,'cold_free_pages':cold,'free_pages_before':baseline,'free_pages_after':free_pages(),'timer_preemptions_before':preemptions_before,'timer_preemptions_after':preemptions_after,'checks':['real clang -femulated-tls C11 and C++17 variables, initialization and4096-byte alignment','at least48 compiler TLS controls trigger upstream per-thread address-array growth','three workers preserve isolated TLS values across SVC/yield/preemption and repeated launches','C++ thread_local constructors once per thread, destructors in reverse order before C key callbacks','ordinary key callbacks access retained compiler TLS through all four destructor passes','compiler storage and C++ cleanup nodes reclaimed after join; user heap returns to exact main baseline','normal main return and explicit main pthread_exit run C++ TLS destructors with valid values','main pthread_exit preserves worker and join result','key/dtor quotas and recursive destructor registration fail closed with bounded exit134 and stable pages','no queued futex records, no unresolved runtime imports and no PT_TLS dependency'],'limits':['explicit -femulated-tls static-link profile, not native ELF TLS relocations/dynamic loader','no full C++ standard library, exceptions, global constructors/atexit or DSO unload support','C++ thread-local callback registration64 pending per thread, cleanup bound256 invocations','native heap, thread and key quotas remain in force','new Fusion/physical behavior NOT RUN; modern engine and Stage3 incomplete']},indent=2)+'\n')
except Exception:
 (out/'failure.txt').write_text(g.text());raise
finally:g.close()

#!/usr/bin/env python3
"""Native constructor/atexit/TLS/last-pthread-exit lifecycle on disposable guests."""
import argparse,pathlib,shutil,subprocess,sys,json,re,time,hashlib,struct
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--mode',choices=['direct','uefi'],default='direct');p.add_argument('--application',type=pathlib.Path);o=p.parse_args()
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False);disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
apps={}
for file,path in [('static_startup_test.elf','/BIN/START.ELF'),('static_startup_early_test.elf','/BIN/EARLY.ELF')]:
 app=o.application if path=='/BIN/START.ELF' and o.application else pathlib.Path('build/userland')/file;data=app.read_bytes();apps[path]=hashlib.sha256(data).hexdigest()
 assert not subprocess.check_output(['llvm-nm','-u',str(app)],text=True).strip(),'unresolved native import'
 phoff=struct.unpack_from('<Q',data,32)[0];phsize,phcount=struct.unpack_from('<HH',data,54)
 ph=[struct.unpack_from('<IIQQQQQQ',data,phoff+i*phsize) for i in range(phcount)]
 assert not any(h[0]==7 for h in ph),'unsupported ELF PT_TLS'
 assert not any(h[0]==1 and h[1]&3==3 for h in ph),'writable executable segment'
 symbols={line.split()[2]:int(line.split()[0],16) for line in subprocess.check_output(['llvm-nm',str(app)],text=True).splitlines() if len(line.split())==3}
 for kind in ['preinit','init','fini']:
  lo,hi=(symbols['__'+kind+'_array_'+end] for end in ['start','end'])
  assert hi>lo and not (lo&7) and (hi-lo)%8==0
  assert any(h[0]==1 and h[1]==4 and h[3]<=lo<hi<=h[3]+h[5] for h in ph),(kind,'array must be file-backed read-only')
 subprocess.run(['python3','tools/mkchfs.py','put',str(disk),path,str(app)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=pathlib.Path('build/chos.elf'),uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=pathlib.Path('build/chos-0.30.0.iso'),memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk);cases=[];baseline=None
normal='[ OK ] native static lifecycle globals,TLS,atexit and fini arrays passed'
early='[ OK ] native early constructor cleanup passed'
def free_pages():
 end=time.monotonic()+10
 while True:
  before=g.text().count('CHOS process table (foreground and scheduled EL0 processes)');g.serial('procinfo')
  wait(lambda:g.text().count('CHOS process table (foreground and scheduled EL0 processes)')>before,3,'process diagnostic')
  section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
  if re.search(r'Active: 0;.*pending cleanup: 0',section):break
  if time.monotonic()>end:raise AssertionError('native startup cleanup did not quiesce')
  time.sleep(.05)
 before=len(re.findall(r'Free pages: (\d+)',g.text()));g.serial('meminfo')
 wait(lambda:len(re.findall(r'Free pages: (\d+)',g.text()))>before,3,'free pages')
 return int(re.findall(r'Free pages: (\d+)',g.text())[-1])
def run(mode,status,marker=None,path='/BIN/START.ELF',fault=False):
 global baseline
 offset=len(g.text());before=g.text().count(normal);begin=time.monotonic()
 g.serial('exec '+path+' '+mode+' alpha')
 if marker:wait(lambda:marker in g.text()[offset:] or '[FAIL]' in g.text()[offset:],30,'lifecycle '+mode)
 observed=free_pages();section=g.text()[offset:]
 assert '[FAIL]' not in section and '[FATAL]' not in section,section
 if not fault:assert '[USER FAULT]' not in section,section
 pid=re.search(r'pid=(\d+) path='+re.escape(path),section).group(1)
 history=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
 if fault:assert re.search(r'\bpid='+pid+r'\b[^\r\n]*state=failed\b[^\r\n]*exit=-\d+',history),history
 else:assert re.search(r'\bpid='+pid+r'\b[^\r\n]*state=exited\b[^\r\n]*exit='+str(status)+r'\b',history),history
 if marker!=normal:assert g.text().count(normal)==before,'unexpected process cleanup on refusal/immediate exit'
 if baseline is None:baseline=observed
 else:assert observed==baseline,('native lifecycle leaked frames',baseline,observed,mode)
 assert g.symbol('futex_queued_waiters')==0
 cases.append({'mode':mode,'path':path,'expected_exit':status,'fault_expected':fault,'free_pages':observed,'seconds':time.monotonic()-begin,'status':'PASS'})
try:
 wait(lambda:'chos> ' in g.text(),45,'scheduled runtime')
 if o.mode=='uefi':assert 'UEFI CPU isolation active' in g.text()
 wait(lambda:g.symbol('splash_active')==0,10,'splash');cold=free_pages()
 for mode,status in [('return',17),('exit',23),('last-thread',0),('worker-exit',37),('c-quota',18)]:run(mode,status,normal)
 run('immediate',77,'[ OK ] native immediate exit skips cleanup')
 run('return',9,early,path='/BIN/EARLY.ELF')
 for mode,marker in [('cxx-quota','C++ process callback quota refusal begins'),('loop','process callback rearm refusal begins'),('recursion','process callback recursion refusal begins'),('thread-in-exit','pthread_exit during process cleanup refusal begins'),('thread-in-finalize','pthread_exit during manual finalization refusal begins'),('recursive-exit','recursive process exit refusal begins'),('pure-virtual','pure virtual refusal begins')]:run(mode,134,marker)
 run('array-ro',None,'[USER FAULT]',fault=True)
 for i in range(3):run('return',17,normal)
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'applications_sha256':apps,'cold_free_pages':cold,'free_pages_before':baseline,'free_pages_after':free_pages(),'cases':cases,'checks':['real preinit/init priorities before main,including native worker creation/join during a constructor','argc/argv preserved through startup and native C++ vector/global/local-static constructors execute','typed C/C++ LIFO,DSO-selective/repeated/concurrent finalization and exactly-once destruction','C++ thread-local destructors precede process statics; objects created in key destructors also finalize before compiler TLS release; current compiler TLS survives global teardown','normal main return,exit,last pthread_exit,worker-initiated exit and early-constructor exit','_Exit bypasses all callbacks and global fini arrays','callback quotas,rearm/recursion and forbidden pthread_exit/recursive exit fail closed','constructor arrays are file-backed RO; actual attempted store faults only that process','fini priorities run after process callbacks and can register additional atexit cleanup,clean relaunch and stable warm frames/no queued futexes'],'limits':['bounded static native ELF profile,128 process callbacks,512 callback budget,16 recursive finalizations','no exceptions/unwinding,DSO loading,full libc++/POSIX or modern engine','new Fusion/Pi/laptop behavior NOT RUN; Stage3 incomplete']},indent=2)+'\n')
except Exception:
 (out/'failure.txt').write_text(g.text());raise
finally:g.close()

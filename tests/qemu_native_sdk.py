#!/usr/bin/env python3
"""Run ordinary CMake-linked C/C++ main programs against native CHOS."""
import argparse,pathlib,shutil,subprocess,sys,json,re,time,hashlib
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--applications',type=pathlib.Path,required=True);p.add_argument('--mode',choices=['direct','uefi'],default='direct');o=p.parse_args()
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False);disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
apps={}
for file,path in [('sdk_c','/BIN/SDKC.ELF'),('sdk_cpp','/BIN/SDKCPP.ELF')]:
 app=o.applications/file;apps[path]=hashlib.sha256(app.read_bytes()).hexdigest()
 assert not subprocess.check_output(['llvm-nm','-u',str(app)],text=True).strip()
 headers=subprocess.check_output(['llvm-readelf','-lW',str(app)],text=True)
 assert not re.search(r'^\s+TLS\s',headers,re.M) and not re.search(r'^\s+LOAD[^\n]*\bRWE\b',headers,re.M)
 subprocess.run(['python3','tools/mkchfs.py','put',str(disk),path,str(app)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=pathlib.Path('build/chos.elf'),uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=pathlib.Path('build/chos-0.30.0.iso'),memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk)
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
cases=[]
def run(path,marker,status):
 offset=len(g.text());g.serial('exec '+path+' alpha beta')
 wait(lambda:marker in g.text()[offset:] or '[FAIL]' in g.text()[offset:],30,'SDK application '+path)
 pages=free_pages();section=g.text()[offset:]
 assert not any(m in section for m in ['[FAIL]','[FATAL]','[USER FAULT]']),section
 pid=re.search(r'pid=(\d+) path='+re.escape(path),section).group(1)
 history=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
 assert re.search(r'\bpid='+pid+r'\b[^\r\n]*state=exited\b[^\r\n]*exit='+str(status)+r'\b',history),history
 cases.append({'path':path,'expected_exit':status,'free_pages':pages,'status':'PASS'});return pages
try:
 wait(lambda:'chos> ' in g.text(),45,'scheduled runtime')
 if o.mode=='uefi':assert 'UEFI CPU isolation active' in g.text() or 'UEFI CPU isolation' in g.text()
 wait(lambda:g.symbol('splash_active')==0,10,'splash')
 cold=free_pages();baseline=None
 for cycle in range(4):
  for path,marker,status in [('/BIN/SDKC.ELF','[ OK ] native SDK C main arguments and allocation passed',19),('/BIN/SDKCPP.ELF','[ OK ] native SDK C++ main,constructors and threads passed',23)]:
   pages=run(path,marker,status)
   if baseline is not None:assert pages==baseline,('SDK leaked frames',baseline,pages)
  if cycle==0:baseline=pages
 assert g.symbol('futex_queued_waiters')==0
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'applications_sha256':apps,'cases':cases,'cold_free_pages':cold,'warm_free_pages':baseline,'checks':['actual CMake executable links against staged native CRT/static runtime','ordinary hosted-language main without extern C workaround,argc/argv and C allocation','global C++ vector constructors and three real std::thread workers,normal return/cleanup','alternating C/C++ processes after warmup retain stable PMM pages/no queued futexes'],'limits':['bounded static SDK,no full hosted C/C++/POSIX or modern engine','new Fusion/Pi/laptop behavior NOT RUN; Stage3 incomplete']},indent=2)+'\n')
except Exception:
 (out/'failure.txt').write_text(g.text());raise
finally:g.close()

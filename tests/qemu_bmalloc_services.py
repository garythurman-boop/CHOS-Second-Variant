#!/usr/bin/env python3
"""Actual bmalloc library native allocation, entropy, diagnostics and mutexes."""
import argparse,pathlib,shutil,subprocess,sys,json,re,time,hashlib
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--application',type=pathlib.Path,required=True);p.add_argument('--mode',choices=['direct','uefi'],default='direct');p.add_argument('--missing-entropy',action='store_true');o=p.parse_args()
assert not o.missing_entropy or o.mode=='direct','absence test uses direct boot without EFI seed'
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False);disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
app=o.application;app_sha=hashlib.sha256(app.read_bytes()).hexdigest();subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/BIN/BMSVC.ELF',str(app)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=pathlib.Path('build/chos.elf'),uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=pathlib.Path('build/chos-0.30.0.iso'),memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=o.missing_entropy,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk);marker='[ OK ] upstream bmalloc allocation, native entropy, diagnostics and mutex services passed';fault_pattern=r'\[USER FAULT\][^\r\n]*ESR=0x([0-9A-Fa-f]{16})[^\r\n]*FAR=0x([0-9A-Fa-f]{16})[\r\n]'
def free_pages():
 end=time.monotonic()+8
 while True:
  header='CHOS process table (foreground and scheduled EL0 processes)';before=g.text().count(header);g.serial('procinfo')
  wait(lambda:g.text().count(header)>before,3,'process diagnostic');section=g.text().rsplit(header,1)[-1]
  if re.search(r'Active: 0;.*pending cleanup: 0',section):break
  if time.monotonic()>end:raise AssertionError('process cleanup did not quiesce')
  time.sleep(.05)
 before=len(re.findall(r'Free pages: (\d+)',g.text()));g.serial('meminfo');wait(lambda:len(re.findall(r'Free pages: (\d+)',g.text()))>before,3,'free pages')
 return int(re.findall(r'Free pages: (\d+)',g.text())[-1])
def launch():
 offset=len(g.text());n=g.text().count(marker);g.serial('exec /BIN/BMSVC.ELF')
 wait(lambda:g.text().count(marker)>n or '[FAIL]' in g.text()[offset:],60,'native bmalloc services')
 section=g.text()[offset:];assert '[FAIL]' not in section and '[FATAL]' not in section,section
 assert section.count('[EXPECT] native entropy must fail closed')==1 and len(re.findall(fault_pattern,section))==1,section
 assert '[native-bmalloc] value=7 text=ready' in section and 'fixture.cpp(23) : native_services' in section
 assert '[truncated]' in section and '[diagnostic format failed]' in section
 observed=free_pages();pid=re.search(r'pid=(\d+) path=/BIN/BMSVC.ELF',section).group(1)
 status=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
 assert re.search(r'\bpid='+pid+r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b',status),status
 return observed
try:
 assert not subprocess.check_output(['llvm-nm','-u',str(app)],text=True).strip()
 headers=subprocess.check_output(['llvm-readelf','-lW',str(app)],text=True)
 assert not re.search(r'^\s+TLS\s',headers,re.M) and not re.search(r'^\s+LOAD[^\n]*\bRWE\b',headers,re.M)
 wait(lambda:'chos> ' in g.text(),45,'scheduled runtime')
 if o.mode=='uefi':assert 'UEFI CPU isolation active' in g.text()
 wait(lambda:g.symbol('splash_active')==0,10,'splash');cold=free_pages();preemptions_before=g.symbol('preemption_switches');faults=[]
 if o.missing_entropy:
  offset=len(g.text());started=time.monotonic();g.serial('exec /BIN/BMSVC.ELF missing-entropy')
  wait(lambda:re.search(fault_pattern,g.text()[offset:]),8,'bounded entropy absence failure')
  section=g.text()[offset:];assert '[EXPECT] native entropy must fail closed' in section and marker not in section and '[FATAL]' not in section
  faults.append({'mode':'missing-entropy','observed_seconds':time.monotonic()-started,'record':re.findall(fault_pattern,section)[-1]});baseline=free_pages()
 else:
  baseline=launch()
  for run in range(2):assert launch()==baseline
  offset=len(g.text());g.serial('exec /BIN/BMSVC.ELF readonly-entropy')
  wait(lambda:re.search(fault_pattern,g.text()[offset:]),8,'read-only entropy output denial')
  section=g.text()[offset:];assert '[EXPECT] native entropy rejects a read-only output buffer' in section and marker not in section and '[FATAL]' not in section
  faults.append({'mode':'read-only-entropy-buffer','record':re.findall(fault_pattern,section)[-1]});assert free_pages()==baseline
 offset=len(g.text());g.serial('exec /BIN/BMSVC.ELF control-only')
 wait(lambda:'[ OK ] upstream native allocation and logging recover after entropy failure' in g.text()[offset:],15,'allocation/logging after denied entropy')
 assert free_pages()==baseline==cold and g.symbol('futex_queued_waiters')==0
 preemptions_after=g.symbol('preemption_switches');assert preemptions_after>preemptions_before
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'missing_entropy':o.missing_entropy,'application_sha256':app_sha,'cold_free_pages':cold,'free_pages_before':baseline,'free_pages_after':free_pages(),'timer_preemptions_before':preemptions_before,'timer_preemptions_after':preemptions_after,'contained_failures':faults,
 'checks':(['no hardware or EFI seed: bounded fail-closed entropy path;no weak fallback'] if o.missing_entropy else ['actual native bmalloc library out-of-line allocation/reallocation/zero/alignment/free and larger virtual helpers','OS-broker entropy spans multiple256byte chunks;canaries and independent-sample transport sanity','native Console formatting,truncation and invalid-wide-character error;denied output returns without spinning','three concurrent native mutex workers held behind real scheduled yields;three lifecycles','denied entropy permission and read-only output kill only the process;process-lifetime small cache reaped']),
 'limits':['transport sanity is not cryptographic-strength certification;production reseed/trust policy remains open','configured system-malloc library;libpas/mimalloc not enabled;not WTF/JavaScriptCore/browser','one upstream allocation-map bucket cache may live until process exit;strict post-reap PMM equality holds','new Fusion/Pi/laptop NOT RUN;Stage3 remains incomplete']},indent=2)+'\n')
except Exception:
 try:g.serial('procinfo');time.sleep(.3);g.serial('heapinfo');time.sleep(.3)
 except Exception:pass
 (out/'failure.txt').write_text(g.text());raise
finally:g.close()

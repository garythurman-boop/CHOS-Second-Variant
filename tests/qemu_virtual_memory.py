#!/usr/bin/env python3
"""Actual native anonymous mmap/protection/discard/split unmap and isolation."""
import argparse,pathlib,shutil,subprocess,sys,json,re,time,hashlib
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--application',type=pathlib.Path);p.add_argument('--mode',choices=['direct','uefi'],default='direct');o=p.parse_args()
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False);disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
app=o.application or pathlib.Path('build/userland/vm_test.elf');app_sha=hashlib.sha256(app.read_bytes()).hexdigest()
subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/BIN/VM.ELF',str(app)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=pathlib.Path('build/chos.elf'),uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=pathlib.Path('build/chos-0.30.0.iso'),memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk)
marker='[ OK ] native anonymous mmap, protection, discard, split unmap and cleanup passed'
def free_pages():
 end=time.monotonic()+8
 while True:
  header='CHOS process table (foreground and scheduled EL0 processes)';before=g.text().count(header);g.serial('procinfo')
  wait(lambda:g.text().count(header)>before,3,'process diagnostic')
  section=g.text().rsplit(header,1)[-1]
  if re.search(r'Active: 0;.*pending cleanup: 0',section):break
  if time.monotonic()>end:raise AssertionError('process cleanup did not quiesce')
  time.sleep(.05)
 before=len(re.findall(r'Free pages: (\d+)',g.text()));g.serial('meminfo')
 wait(lambda:len(re.findall(r'Free pages: (\d+)',g.text()))>before,3,'free pages')
 return int(re.findall(r'Free pages: (\d+)',g.text())[-1])
def launch():
 offset=len(g.text());n=g.text().count(marker);g.serial('exec /BIN/VM.ELF')
 wait(lambda:g.text().count(marker)>n or '[FAIL] native virtual memory' in g.text()[offset:] or '[USER FAULT]' in g.text()[offset:],60,'native virtual memory')
 section=g.text()[offset:];assert '[FAIL]' not in section and '[USER FAULT]' not in section and '[FATAL]' not in section,section
 assert 'reduced permissions deny control and unmap' in section and 'rejects foreign address-space ownership' in section,section
 observed=free_pages();pid=re.search(r'pid=(\d+) path=/BIN/VM.ELF',section).group(1)
 status=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
 assert re.search(r'\bpid='+pid+r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b',status),status
 return observed
try:
 assert not subprocess.check_output(['llvm-nm','-u',str(app)],text=True).strip()
 headers=subprocess.check_output(['llvm-readelf','-lW',str(app)],text=True)
 assert not re.search(r'^\s+TLS\s',headers,re.M) and not re.search(r'^\s+LOAD[^\n]*\bRWE\b',headers,re.M)
 wait(lambda:'chos> ' in g.text(),45,'scheduled runtime')
 if o.mode=='uefi':assert 'UEFI CPU isolation active' in g.text()
 wait(lambda:g.symbol('splash_active')==0,10,'splash')
 cold=free_pages();preemptions_before=g.symbol('preemption_switches');baseline=launch()
 for run in range(2):assert launch()==baseline
 faults=[]
 for mode,ec,dfsc in [('read-none',0x24,0xf),('write-ro',0x24,0xf),('execute-ro',0x20,0xf),('unmapped-middle',0x24,7)]:
  offset=len(g.text());before=g.text().count('[USER FAULT]');g.serial('exec /BIN/VM.ELF '+mode)
  wait(lambda:re.search(r'\[USER FAULT\][^\r\n]*ESR=0x[0-9A-Fa-f]{16}[^\r\n]*FAR=0x[0-9A-Fa-f]{16}[\r\n]',g.text()[offset:]) or '[FAIL] native virtual memory' in g.text()[offset:],15,'complete native '+mode+' fault record')
  section=g.text()[offset:];assert '[FAIL]' not in section and '[FATAL]' not in section,section
  expected=int(re.search(r'\[VM FAULT EXPECT\] 0x([0-9a-fA-F]+)',section).group(1),16)
  matches=re.findall(r'\[USER FAULT\][^\r\n]*ESR=0x([0-9A-Fa-f]+)[^\r\n]*FAR=0x([0-9A-Fa-f]+)',section)
  esr,far=(int(v,16) for v in matches[-1]);assert esr>>26==ec and esr&63==dfsc and far==expected,(mode,hex(esr),hex(far),hex(expected))
  faults.append({'mode':mode,'esr':esr,'far':far});assert free_pages()==baseline
 assert launch()==baseline
 preemptions_after=g.symbol('preemption_switches');assert preemptions_after>preemptions_before and g.symbol('futex_queued_waiters')==0
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'application_sha256':app_sha,'cold_free_pages':cold,'free_pages_before':baseline,'free_pages_after':free_pages(),'timer_preemptions_before':preemptions_before,'timer_preemptions_after':preemptions_after,'contained_faults':faults,'operation_timings_ns':[{ 'operation':name,'nanoseconds':int(value,16)} for name,value in re.findall(r'\[VM TIMING NS\] ([a-z-]+) 0x([0-9a-f]+)',g.text())],
 'checks':['real owned anonymous mmap at NONE/R/RW;zero-on-discard with protection retained;RW restoration',
 'partial middle unmap/prefix/suffix trim;split-quota failure changes no live data',
 'three concurrent native worker lifecycles;all cleanup returns heap and warm physical pages',
 'actual foreign address-space and reduced syscall-permission attempts denied;code permissions cannot be promoted',
 'architectural read-NONE/write-RO/execute-XN/unmapped-hole faults at exact announced addresses;subsequent execution works',
 'real sysconf/getpagesize/getpid and bounded process-owned read/lseek/close;write invalid fd fails'],
 'limits':['bounded private anonymous subset;no fixed/file/shared/executable mappings;no sparse demand paging or swap',
 'munmap/mprotect/madvise must fit one live owned region;unsupported holes/multiple regions fail',
 '1MiB mapping,2MiB aperture,16 regions,4MiB image,8MiB owned frames;not engine-sized',
 'not full POSIX,JavaScriptCore/browser or Stage3 completion;new Fusion/Pi/laptop NOT RUN']},indent=2)+'\n')
except Exception:
 try:g.serial('procinfo');time.sleep(.3);g.serial('threads');time.sleep(.3)
 except Exception:pass
 (out/'failure.txt').write_text(g.text());raise
finally:g.close()

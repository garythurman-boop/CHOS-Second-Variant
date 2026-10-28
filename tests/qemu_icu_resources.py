#!/usr/bin/env python3
"""Original generated ICU locale/converter/break resource execution and cleanup."""
import argparse,pathlib,shutil,subprocess,sys,json,re,time,hashlib
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=pathlib.Path,required=True);p.add_argument('--application',type=pathlib.Path);p.add_argument('--mode',choices=['direct','uefi'],default='direct');o=p.parse_args()
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False);disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
app=o.application or pathlib.Path('build/userland/icu_resource_test.app.elf');app_sha=hashlib.sha256(app.read_bytes()).hexdigest()
subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/BIN/ICURES.ELF',str(app)],check=True)
resources=['root.res','pool.res','en.res','en_US.res','en_US_POSIX.res','de.res','fr.res','cnvalias.icu','ibm-5348_P100-1997.cnv','ibm-943_P130-1999.cnv','ibm-943_P15A-2003.cnv',
 'zoneinfo64.res','windowsZones.res','units.res','timezoneTypes.res','supplementalData.res','plurals.res','pluralRanges.res','numberingSystems.res','metadata.res','metaZones.res','langInfo.res','keyTypeData.res','icuver.res','icustd.res','grammaticalFeatures.res','genderList.res','dayPeriods.res','currencyNumericCodes.res',
 'curr/root.res','curr/pool.res','curr/en.res','curr/de.res','curr/fr.res','brkitr/root.res','brkitr/en.res','brkitr/char.brk','brkitr/word.brk']
extract=out/'generated-resources'
command=['python3','tools/verify_icu_full_data.py','--build-dir','build/icu-data','--extract-dir',str(extract)]
for name in resources:command+=['--resource',name]
subprocess.run(command,check=True)
for name in resources:subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/ICU/icudt79l/'+name,str(extract/name)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=pathlib.Path('build/chos.elf'),uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=pathlib.Path('build/chos-0.30.0.iso'),memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk)
marker='[ OK ] native ICU generated locale,converter,break and cleanup passed'
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
  offset=len(g.text());n=g.text().count(marker);g.serial('exec /BIN/ICURES.ELF')
  wait(lambda:g.text().count(marker)>n or '[FAIL]' in g.text(),60,'native generated ICU resource fixture')
  assert '[FAIL]' not in g.text() and '[USER FAULT]' not in g.text() and '[FATAL]' not in g.text(),g.text()
  observed=free_pages()
  pid=re.search(r'pid=(\d+) path=/BIN/ICURES.ELF',g.text()[offset:]).group(1)
  section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
  assert re.search(r'\bpid='+pid+r'\b[^\r\n]*state=exited\b[^\r\n]*exit=0\b',section),section
  if run==0:baseline=observed
  else:assert observed==baseline,('base C leaked pages',baseline,observed)
 preemptions_after=g.symbol('preemption_switches');assert preemptions_after>preemptions_before
 assert g.symbol('futex_queued_waiters')==0
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'application_sha256':app_sha,'source':'disposable CHFS native application','cold_free_pages':cold,'free_pages_before':baseline,'free_pages_after':free_pages(),'timer_preemptions_before':preemptions_before,'timer_preemptions_after':preemptions_after, 'checks':['selected files extracted with individual hashes from actual upstream default4321-resource package',
 'native u_init/data version,en/de/fr decimal format and parse,Windows1252/ShiftJIS conversion and strict errors',
 'actual word and extended-grapheme break data;missing data and unsupported converter fail',
 '143KiB locale/193KiB currency pools and235KiB supplemental resources use actual larger bounded allocation path',
 'three native workers and three lifecycles,u_cleanup,heap0,stable warm frames and zero private futex waiters'],
 'limits':['selected deployed resources,not execution of the entire embedded32MiB library or full ICU/Intl conformance',
 'current4MiB image,2MiB anonymous aperture,1MiB mapping and8MiB frame limits;not a browser-ready runtime',
 'no JavaScriptCore/WebKit/browser or live WiFi success inferred;new Fusion/Pi/laptop NOT RUN']},indent=2)+'\n')
except Exception:
 try:
  g.serial('procinfo');time.sleep(.4);g.serial('threads');time.sleep(.4)
 except Exception:pass
 (out/'failure.txt').write_text(g.text());raise
finally:g.close()

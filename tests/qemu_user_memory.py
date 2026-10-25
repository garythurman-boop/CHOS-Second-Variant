#!/usr/bin/env python3
"""Actual EL0 heap mappings, guard/XN faults and repeated deferred cleanup."""
import argparse,pathlib,shutil,subprocess,sys,json,hashlib,re,time
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',type=pathlib.Path,required=True);o=ap.parse_args();out=o.out.resolve();out.mkdir(parents=True,exist_ok=False)
disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/BIN/MEMTEST.CHX','build/userland/memory_test.chcx'],check=True)
a=argparse.Namespace(mode='direct',kernel=pathlib.Path('build/chos.elf'),memory='512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,no_rng=False)
application_sha256=hashlib.sha256(pathlib.Path('build/userland/memory_test.chcx').read_bytes()).hexdigest()
g=Guest(a,out,disk)
marker='[ OK ] EL0 anonymous RW/XN mappings and cleanup contract passed'
def normal():
 before=g.text().count(marker);g.serial('exec /BIN/MEMTEST.CHX');wait(lambda:g.text().count(marker)>before,6,'actual EL0 mapping/zeroing/cleanup')
def free_pages():
 # The default GUI shell already schedules EL0 tasks. Await their actual exit
 # and deferred reaping, rather than sampling immediately after an app's print.
 end=time.monotonic()+6
 while True:
  count=g.text().count('CHOS process table (foreground and scheduled EL0 processes)');g.serial('procinfo')
  wait(lambda:g.text().count('CHOS process table (foreground and scheduled EL0 processes)')>count,3,'process cleanup diagnostic')
  section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
  if re.search(r'Active: 0;.*pending cleanup: 0',section):break
  if time.monotonic()>=end:raise AssertionError('Process cleanup did not quiesce')
  time.sleep(.05)
 before=len(re.findall(r'Free pages: (\d+)',g.text()));g.serial('meminfo')
 wait(lambda:len(re.findall(r'Free pages: (\d+)',g.text()))>before,3,'PMM stats')
 return int(re.findall(r'Free pages: (\d+)',g.text())[-1])
try:
 wait(lambda:'chos> ' in g.text(),15,'direct shell')
 wait(lambda:g.symbol('splash_active')==0,10,'splash')
 normal();normal();baseline=free_pages()
 for _ in range(6):normal()
 fault_results=[]
 for mode in ['guard','execute']:
  before=g.text().count('[USER FAULT]');g.serial('exec /BIN/MEMTEST.CHX '+mode)
  wait(lambda:g.text().count('[USER FAULT]')>before,6,'EL0 '+mode+' protection fault')
  assert '[FATAL]' not in g.text()
  normal();fault_results.append(mode)
 g.serial('runtime');wait(lambda:"CHOS scheduled runtime console." in g.text(),5,'scheduled root/user runtime')
 for _ in range(4):
  before=g.text().count(marker);g.serial('spawn /BIN/MEMTEST.CHX');wait(lambda:g.text().count(marker)>before,6,'scheduled EL0 mapping and deferred cleanup');time.sleep(.15)
 g.serial('runtime-exit');wait(lambda:'runtime shell and scheduled processes finished; returned to boot shell.' in g.text(),8,'scheduled cleanup returns to canonical root')
 normal()
 after=free_pages();assert baseline==after,('Process memory leaked across normal/fault cleanup',baseline,after)
 (out/'result.json').write_text(json.dumps({'status':'PASS','application_sha256':application_sha256,'free_pages_before':baseline,'free_pages_after':after,'checks':['real EL0 anonymous zeroed RW/XN mappings','invalid/overflow sizes and nonzero high flags refused','exact unmap and double-unmap rejection','guard-page data abort contained to process','execute-never instruction abort contained to process','normal/fault/scheduled exit cleans abandoned mappings; no physical-page leak']},indent=2))
except Exception:
 (out/'failure.txt').write_text(g.text());raise
finally:g.close()

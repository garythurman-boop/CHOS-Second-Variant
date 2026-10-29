import sys,argparse,pathlib,shutil,time,json,hashlib
sys.path.insert(0,'tests')
from qemu_platform import Guest,wait
parser=argparse.ArgumentParser(description='UEFI browser, read-only NVMe and scaled GOP acceptance; disposable VM only')
parser.add_argument('--out',type=pathlib.Path,required=True)
parser.add_argument('--write-fault',action='store_true')
parser.add_argument('--keyboard',action='store_true')
parser.add_argument('--supported-mode',action='store_true')
options=parser.parse_args()
out=options.out.resolve();out.mkdir(parents=True,exist_ok=False)
disk=out/'disposable.img';
with disk.open('xb') as image:image.truncate(256*1024*1024)
before=hashlib.sha256(disk.read_bytes()).hexdigest()
a=argparse.Namespace(mode='uefi',uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=pathlib.Path('build/chos-0.30.0.iso'),memory='2G',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,storage_error=options.write_fault,nvme=True,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk)
def capture_failure():
 try:
  # Preserve the terminal job/error before teardown. A rendered progress frame
  # can lag the actual I/O failure; a screenshot alone loses its diagnosis.
  import subprocess,tempfile
  with tempfile.TemporaryDirectory() as temporary:
   source=pathlib.Path(temporary)/'layout.c';binary=pathlib.Path(temporary)/'layout'
   source.write_text('#include <stddef.h>\n#include <stdio.h>\n#include "chos/installer_io.h"\nint main(void){printf("%zu %zu %zu %zu",sizeof(chos_install_job_t),offsetof(chos_install_job_t,state),offsetof(chos_install_job_t,error),offsetof(chos_install_job_t,next_sector));}\n')
   subprocess.run(['cc','-Iinclude',str(source),'-o',str(binary)],check=True)
   size,state_offset,error_offset,sector_offset=map(int,subprocess.check_output([str(binary)],text=True).split())
   raw=g.block('installation',size=size);(out/'failed-job.bin').write_bytes(raw)
   pointer=int.from_bytes(raw[error_offset:error_offset+8],'little')
   diagnostic={'state':int.from_bytes(raw[state_offset:state_offset+4],'little'),'next_sector':int.from_bytes(raw[sector_offset:sector_offset+8],'little')}
   if pointer:
    target=out/'failed-error.bin';result=g.hmp(f'pmemsave {pointer:#x} 256 "{target}"')
    if not result:diagnostic['error']=target.read_bytes().split(b'\0',1)[0].decode('utf-8',errors='replace')
   # Several backends use a local last_error name. Preserve every such symbol
   # instead of assuming the symbol dictionary selected a particular driver.
   import re
   relocation=int(re.findall(r'Kernel physical base: (0x[0-9a-f]+)',g.text())[-1],16)-g.symbols['__kernel_start']
   driver_errors=[]
   for line in subprocess.check_output(['llvm-nm','-a',str(out/'kernel.elf')],text=True).splitlines():
    fields=line.split()
    if len(fields)!=3 or fields[2]!='last_error':continue
    address=int(fields[0],16)+relocation;target=out/'failed-driver-probe.bin'
    if g.hmp(f'pmemsave {address:#x} 8 "{target}"'):continue
    pointer=int.from_bytes(target.read_bytes(),'little')
    if not pointer or g.hmp(f'pmemsave {pointer:#x} 256 "{target}"'):continue
    driver_errors.append({'symbol':fields[0],'error':target.read_bytes().split(b'\0',1)[0].decode('utf-8',errors='replace')})
   diagnostic['driver_errors']=driver_errors
   (out/'failed-job.json').write_text(json.dumps(diagnostic,indent=2)+'\n')
 except Exception as diagnostic_error:
  (out/'failure-diagnostics-error.txt').write_text(repr(diagnostic_error)+'\n')
def key(k):g.hmp('sendkey '+k);time.sleep(.15)
def click(x,y):
 window=g.window();scale=max(1,window['width']//595)
 x=window['x']+(x-180)*scale;y=window['y']+(y-70)*scale
 width=g.symbol('active_surface',8);height=g.symbol('active_surface',16)
 assert 320<=width<=4096 and 200<=height<=4096,(width,height)
 g.qmp('input-send-event',events=[{'type':'abs','data':{'axis':'x','value':round(x*32767/(width-1))}},{'type':'abs','data':{'axis':'y','value':round(y*32767/(height-1))}}]);time.sleep(.12)
 for down in [True,False]:g.qmp('input-send-event',events=[{'type':'btn','data':{'button':'left','down':down}}]);time.sleep(.12)
def main_action():
 if options.keyboard:key('ret')
 else:click(560,469)
try:
 wait(lambda:'Initial CHOS GUI frame rendered' in g.text(),45,'desktop')
 wait(lambda:g.symbol('splash_active')==0,10,'splash')
 key('i');time.sleep(.1);assert g.symbol('view')!=8,'plain I must not launch Setup'
 key('f1');wait(lambda:g.symbol('view')==4,3,'Diagnostics')
 click(300,475);wait(lambda:g.symbol('view')==8,3,'explicit Setup button')
 g.screen('welcome')
 main_action() # Next: no target yet.
 assert hashlib.sha256(disk.read_bytes()).hexdigest()==before
 main_action() # Explicitly select NVMe.
 g.screen('target')
 main_action() # Partition plan.
 g.screen('plan')
 main_action() # Confirmation.
 g.screen('confirmation')
 assert hashlib.sha256(disk.read_bytes()).hexdigest()==before,'Disk changed before confirmation'
 main_action() # Explicit acknowledgement.
 assert hashlib.sha256(disk.read_bytes()).hexdigest()==before,'Acknowledgement alone wrote disk'
 started=time.monotonic()
 main_action() # Install.
 focus=g.symbol('installer_focus');input_start=time.monotonic();key('tab')
 wait(lambda:g.symbol('installer_focus')!=focus,2,'keyboard focus during installation')
 key('tab');key('tab')
 responsiveness_ms=round((time.monotonic()-input_start)*1000,2)
 g.screen('interactive-progress')
 wait(lambda:g.symbol('installer_page')==5,90,'actual install and verification')
 g.screen('finished')
 # Stable state field follows the planner; read its position from a tiny host layout probe.
 import subprocess,struct,tempfile
 with tempfile.TemporaryDirectory() as temporary:
  source=pathlib.Path(temporary)/'offset.c';binary=pathlib.Path(temporary)/'offset'
  source.write_text('#include <stddef.h>\n#include <stdio.h>\n#include "chos/installer_io.h"\nint main(void){printf("%zu",offsetof(chos_install_job_t,state));}\n')
  subprocess.run(['cc','-Iinclude',str(source),'-o',str(binary)],check=True)
  offset=int(subprocess.check_output([str(binary)],text=True))
 state=int.from_bytes(g.block('installation',offset=offset,size=4),'little')
 if options.write_fault:
  capture_failure()
  diagnostic=json.loads((out/"failed-job.json").read_text())
  assert diagnostic.get("error")=="Write, flush or readback failed; installation is incomplete.",diagnostic
  assert any(row["error"]=="NVMe I/O command failed" for row in diagnostic.get("driver_errors",[])),diagnostic
  assert state==6,('Write failure must produce FAILED, not success',state)
  assert hashlib.sha256(disk.read_bytes()).hexdigest()==before,'Injected failed write changed disk'
  ticks=g.symbol('tick_count');key('tab');time.sleep(.2);assert g.symbol('tick_count')>ticks
  g.screen('write-failure-responsive')
  (out/'result.json').write_text(json.dumps({'status':'PASS','checks':['real guest NVMe write error','no false success','no disk mutation on first failed write','GUI and timer remain responsive']},indent=2))
  raise SystemExit(0)
 assert state==5,('Installer did not report DONE',state)
 elapsed=time.monotonic()-started
 max_step=int.from_bytes(g.block('installer_max_step_counter_ticks',size=8),'little')
 frequency=int.from_bytes(g.block('counter_frequency',size=8),'little')
 max_step_ms=round(1000*max_step/frequency,3) if frequency else None
 a.uefi_kernel=out/'kernel.elf' # Pin exact symbols across later build/reboot.
 g.close();g=None
 # Validate the actual guest-written GPT/ESP/CHFS with the independent host validator.
 assert (out/'kernel.elf').read_bytes() in disk.read_bytes(), 'Installed ESP must contain the exact running kernel'
 boot=out/'disk-only';boot.mkdir()
 a.mode='installed';a.allow_reboot=True
 g=Guest(a,boot,disk)
 wait(lambda:'Initial CHOS GUI frame rendered' in g.text(),45,'disk-only boot without ISO')
 wait(lambda:g.symbol('splash_active')==0,10,'installed desktop')
 assert 'Installed NVMe boot volume mounted writable.' in g.text(),'Installed boot volume should be discovered without manual probing'
 g.screen('installed-desktop')
 key('esc');key('f1');click(600,475);wait(lambda:g.symbol('fs_ready')==1,5,'installed CHFS mount')
 assert g.symbol('native_storage_writable')==1,'Installed storage writes must be automatic'
 g.serial('fswrite /stage2.txt installed-persistence')
 wait(lambda:'file written (directory committed' in g.text(),5,'installed-system write through scheduled shell')
 g.screen('persistent-write')
 key('esc');key('s');wait(lambda:g.symbol('view')==5,3,'installed display settings')
 click(350,330 if options.supported_mode else 300);wait(lambda:g.symbol('display_boot_request_saved')==1,5,'ESP resolution preference write')
 g.screen('boot-resolution-saved')
 g.close();g=None
 reboot=out/'reboot';reboot.mkdir();g=Guest(a,reboot,disk)
 wait(lambda:'Initial CHOS GUI frame rendered' in g.text(),45,'installed reboot')
 wait(lambda:g.symbol('splash_active')==0,10,'reboot splash')
 assert '[DISPLAY] Requested width:' in g.text(),'UEFI loader must read the installed ESP request'
 assert '0x0000000000000400' in g.text(),'Saved request must be 1024 wide'
 if options.supported_mode:
  assert '[DISPLAY] Active width: 0x0000000000000400' in g.text(),'Supported GOP mode must actually change scanout width'
  assert '[DISPLAY] Active height: 0x0000000000000300' in g.text(),'Supported GOP mode must actually change scanout height'
 else:
  assert '[DISPLAY] Active width: 0x0000000000000320' in g.text(),'Unsupported mode must retain the safe framebuffer'
 # 1024x640 is absent from RAMFB; 1024x768 exercises an enumerated mode.
 key('esc');key('f1');click(600,475);wait(lambda:g.symbol('fs_ready')==1,5,'reboot CHFS mount')
 g.serial('fsread /stage2.txt');wait(lambda:'installed-persistence' in g.text(),5,'persistent data after reboot')
 g.screen('persistence-after-reboot')
 protected_hash=hashlib.sha256(disk.read_bytes()).hexdigest()
 key('esc');key('i');time.sleep(.3)
 assert g.symbol('view')!=8,'Typing I must not launch Setup on the installed system'
 assert hashlib.sha256(disk.read_bytes()).hexdigest()==protected_hash,'Hidden installer modified the disk'
 g.screen('setup-hidden')
 # Actual guest reset, not restarting the emulator. Select Start -> Restart.
 def physical_click(x,y):
  width=g.symbol('active_surface',8);height=g.symbol('active_surface',16)
  g.qmp('input-send-event',events=[{'type':'abs','data':{'axis':'x','value':round(x*32767/(width-1))}},{'type':'abs','data':{'axis':'y','value':round(y*32767/(height-1))}}]);time.sleep(.15)
  for down in [True,False]:g.qmp('input-send-event',events=[{'type':'btn','data':{'button':'left','down':down}}]);time.sleep(.15)
 width=g.symbol('active_surface',8);height=g.symbol('active_surface',16)
 boots=g.text().count('Initial CHOS GUI frame rendered')
 physical_click(40,height-15);physical_click(180,height-60)
 wait(lambda:g.symbol('power_confirmation_action')==2,3,'restart confirmation')
 key('ret')
 wait(lambda:g.text().count('Initial CHOS GUI frame rendered')>boots,55,'actual PSCI restart through firmware')
 wait(lambda:g.symbol('splash_active')==0,10,'restarted desktop')
 assert g.symbol('native_storage_writable')==1
 g.serial('fsread /stage2.txt');wait(lambda:g.text().count('installed-persistence')>=2,5,'data after real reset')
 g.screen('actual-restart-persistence')
 width=g.symbol('active_surface',8);height=g.symbol('active_surface',16)
 physical_click(40,height-15);physical_click(95,height-60)
 wait(lambda:g.symbol('power_confirmation_action')==1,3,'shutdown confirmation')
 started_shutdown=time.monotonic();key('ret')
 time.sleep(1);assert g.process.poll() is None,'Shutdown must hold status for at least five seconds'
 g.screen('shutting-down')
 wait(lambda:g.process.poll() is not None,15,'actual guest power off')
 shutdown_seconds=time.monotonic()-started_shutdown
 assert shutdown_seconds>=5 and g.process.returncode==0
 # QMP may contain a shutdown event before closing; serial and process independently show guest path.
 assert '[POWER] PSCI system off.' in g.text()

 (out/'result.json').write_text(json.dumps({'status':'PASS','install_seconds':round(elapsed,3),'three_key_focus_ms':responsiveness_ms,'max_install_step_ms':max_step_ms,'keyboard_wizard':options.keyboard,'physical_mode_change':options.supported_mode,'shutdown_seconds':round(shutdown_seconds,3),'checks':['no writes before target selection and final confirmation','guest GPT/FAT32/EFI/CHFS creation and verification','ISO detached: disk-only desktop and native input','scheduled shell file write survives a fresh reboot; native editor checked separately','ESP display request consumed before EBS; physical mode changes or retains safe unsupported-mode fallback','keyboard focus remains responsive during installation','installed Setup hidden; actual firmware reset preserves automatically writable storage; shutdown status held >=5 seconds then guest exits']},indent=2))
except Exception:
 if g and g.process.poll() is None:
  g.screen('failure')
  capture_failure()
 raise
finally:
 if g:g.close()

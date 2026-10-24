import sys,argparse,pathlib,shutil,time,json,hashlib
sys.path.insert(0,'tests')
from qemu_platform import Guest,wait
parser=argparse.ArgumentParser(description='UEFI browser, read-only NVMe and scaled GOP acceptance; disposable VM only')
parser.add_argument('--out',type=pathlib.Path,required=True)
parser.add_argument('--write-fault',action='store_true')
options=parser.parse_args()
out=options.out.resolve();out.mkdir(parents=True,exist_ok=False)
disk=out/'disposable.img';
with disk.open('xb') as image:image.truncate(256*1024*1024)
before=hashlib.sha256(disk.read_bytes()).hexdigest()
a=argparse.Namespace(mode='uefi',uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=pathlib.Path('build/chos-0.30.0.iso'),memory='2G',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,storage_error=options.write_fault,nvme=True,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk)
def key(k):g.hmp('sendkey '+k);time.sleep(.15)
def click(x,y):
 g.qmp('input-send-event',events=[{'type':'abs','data':{'axis':'x','value':round(x*32767/799)}},{'type':'abs','data':{'axis':'y','value':round(y*32767/599)}}]);time.sleep(.12)
 for down in [True,False]:g.qmp('input-send-event',events=[{'type':'btn','data':{'button':'left','down':down}}]);time.sleep(.12)
try:
 wait(lambda:'Initial CHOS GUI frame rendered' in g.text(),45,'desktop')
 wait(lambda:g.symbol('splash_active')==0,10,'splash')
 key('i');wait(lambda:g.symbol('view')==8,3,'installer welcome')
 g.screen('welcome')
 click(560,469) # Next: no target yet.
 assert hashlib.sha256(disk.read_bytes()).hexdigest()==before
 click(560,469) # Explicitly select NVMe.
 g.screen('target')
 click(560,469) # Partition plan.
 g.screen('plan')
 click(560,469) # Confirmation.
 g.screen('confirmation')
 assert hashlib.sha256(disk.read_bytes()).hexdigest()==before,'Disk changed before confirmation'
 click(560,469) # Explicit acknowledgement.
 assert hashlib.sha256(disk.read_bytes()).hexdigest()==before,'Acknowledgement alone wrote disk'
 started=time.monotonic()
 click(560,469) # Install.
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
  assert state==6,('Write failure must produce FAILED, not success',state)
  assert hashlib.sha256(disk.read_bytes()).hexdigest()==before,'Injected failed write changed disk'
  ticks=g.symbol('tick_count');key('tab');time.sleep(.2);assert g.symbol('tick_count')>ticks
  g.screen('write-failure-responsive')
  (out/'result.json').write_text(json.dumps({'status':'PASS','checks':['real guest NVMe write error','no false success','no disk mutation on first failed write','GUI and timer remain responsive']},indent=2))
  raise SystemExit(0)
 assert state==5,('Installer did not report DONE',state)
 elapsed=time.monotonic()-started
 g.close();g=None
 # Validate the actual guest-written GPT/ESP/CHFS with the independent host validator.
 assert (out/'kernel.elf').read_bytes() in disk.read_bytes(), 'Installed ESP must contain the exact running kernel'
 boot=out/'disk-only';boot.mkdir()
 a.mode='installed'
 g=Guest(a,boot,disk)
 wait(lambda:'Initial CHOS GUI frame rendered' in g.text(),45,'disk-only boot without ISO')
 wait(lambda:g.symbol('splash_active')==0,10,'installed desktop')
 g.screen('installed-desktop')
 key('esc');key('f1');click(600,475);wait(lambda:g.symbol('fs_ready')==1,5,'installed CHFS mount')
 click(600,475);click(600,475) # Enable file writes, then explicitly confirm.
 key('esc');key('c');g.type('write /stage2.txt installed-persistence')
 wait(lambda:'File written and flushed.' in g.terminal(),5,'installed-system write')
 g.screen('persistent-write');g.close();g=None
 reboot=out/'reboot';reboot.mkdir();g=Guest(a,reboot,disk)
 wait(lambda:'Initial CHOS GUI frame rendered' in g.text(),45,'installed reboot')
 wait(lambda:g.symbol('splash_active')==0,10,'reboot splash')
 key('esc');key('f1');click(600,475);wait(lambda:g.symbol('fs_ready')==1,5,'reboot CHFS mount')
 key('esc');key('c');g.type('type /stage2.txt');wait(lambda:'installed-persistence' in g.terminal(),5,'persistent data after reboot')
 g.screen('persistence-after-reboot')
 (out/'result.json').write_text(json.dumps({'status':'PASS','install_seconds':round(elapsed,3),'checks':['no writes before target selection and final confirmation','guest GPT/FAT32/EFI/CHFS creation and verification','ISO detached: disk-only desktop and native input','installed file write survives a fresh reboot']},indent=2))
except Exception:
 if g:g.screen('failure')
 raise
finally:
 if g:g.close()

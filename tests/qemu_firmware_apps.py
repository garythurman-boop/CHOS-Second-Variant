import sys,argparse,pathlib,shutil,time,json,hashlib
sys.path.insert(0,'tests')
from qemu_platform import Guest,wait
parser=argparse.ArgumentParser(description='UEFI browser, read-only NVMe and scaled GOP acceptance; disposable VM only')
parser.add_argument('--out',type=pathlib.Path,required=True)
options=parser.parse_args()
out=options.out.resolve();out.mkdir(parents=True,exist_ok=False)
disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
before=hashlib.sha256(disk.read_bytes()).hexdigest()
a=argparse.Namespace(mode='uefi',uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=pathlib.Path('build/chos-0.30.0.iso'),memory='2G',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,storage_error=False,nvme=True,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk)
def key(k):g.hmp('sendkey '+k);time.sleep(.15)
def click(x,y):
 g.qmp('input-send-event',events=[{'type':'abs','data':{'axis':'x','value':round(x*32767/799)}},{'type':'abs','data':{'axis':'y','value':round(y*32767/599)}}]);time.sleep(.12)
 for down in [True,False]:g.qmp('input-send-event',events=[{'type':'btn','data':{'button':'left','down':down}}]);time.sleep(.12)
try:
 wait(lambda:'Initial CHOS GUI frame rendered' in g.text(),45,'desktop')
 wait(lambda:g.symbol('splash_active')==0,10,'splash')
 key('c');g.type('run /bin/browser.chx')
 wait(lambda:g.symbol('kernel_browser_active')==1,5,'UEFI browser launch')
 g.screen('browser');key('esc');wait(lambda:g.symbol('kernel_browser_active')==0,3,'browser close')
 key('esc');key('f1');wait(lambda:g.symbol('view')==4,3,'diagnostics')
 click(600,475);wait(lambda:g.symbol('disk_ready')==1,5,'explicit NVMe read-only attach')
 assert g.symbol('fs_ready')==1,'Existing CHFS must mount read only'
 g.screen('nvme-mounted')
 key('esc');key('s');wait(lambda:g.symbol('view')==5,3,'settings')
 click(350,300);wait(lambda:g.symbol('active_mode_index')==1,4,'GOP desktop sizing')
 g.screen('scaled-desktop')
 assert hashlib.sha256(disk.read_bytes()).hexdigest()==before,'Read-only NVMe discovery changed disk bytes'
 (out/'result.json').write_text(json.dumps({'status':'PASS','checks':['UEFI browser launch and close','explicit NVMe detection and CHFS mount without disk writes','GOP logical desktop sizing']},indent=2))
except Exception:
 g.screen('failure');raise
finally:g.close()

#!/usr/bin/env python3
"""Real installed guest: Network Manager, static profile, link recovery, rollback."""
import argparse,pathlib,shutil,sys,time,json
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',type=pathlib.Path,required=True);ap.add_argument('--net-model',choices=['e1000e','vmxnet3','virtio'],default='e1000e');o=ap.parse_args()
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False);disk=out/'disposable.img';shutil.copyfile('build/chos-0.30.0-installed.img',disk)
a=argparse.Namespace(mode='installed',uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=pathlib.Path('build/chos-0.30.0.iso'),memory='2G',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model=o.net_model,storage_error=False,nvme=True,no_rng=True,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk)
def key(k):g.hmp('sendkey '+k+' 10');time.sleep(.04)
def click(x,y):
 w=g.window();x=w['x']+(x-180);y=w['y']+(y-70)
 for event in [[{'type':'abs','data':{'axis':'x','value':round(x*32767/799)}},{'type':'abs','data':{'axis':'y','value':round(y*32767/599)}}],[{'type':'btn','data':{'button':'left','down':True}}],[{'type':'btn','data':{'button':'left','down':False}}]]:
  g.qmp('input-send-event',events=event);time.sleep(.1)
def field(n,value):
 click(470,237+n*29)
 for _ in range(15):key('backspace')
 for c in value:key('dot' if c=='.' else c)
def ready():
 wait(lambda:'Initial CHOS GUI frame rendered' in g.text(),45,'installed desktop')
 wait(lambda:g.symbol('splash_active')==0,10,'splash')
def ipv4():return g.symbol('net_state',12)
try:
 ready();wait(lambda:g.symbol('net_state')==3,15,'automatic e1000e DHCP')
 assert g.symbol('native_storage_writable')==1
 key('n');wait(lambda:g.symbol('view')==9,3,'Network Manager shortcut')
 g.screen('network-manager')
 click(400,195)
 for n,value in enumerate(['10.0.2.25','255.255.255.0','10.0.2.2','10.0.2.3']):field(n,value)
 click(245,443);wait(lambda:g.symbol('net_state')==6 and ipv4()==0x0a000219,4,'static IPv4 apply')
 g.screen('configuration-trial')
 click(435,443);wait(lambda:g.symbol('trial')==0,4,'keep and save profile')
 g.screen('saved-profile')
 g.qmp('set_link',name='nic',up=False)
 wait(lambda:g.symbol('net_state')==0 and ipv4()==0,5,'link loss clears address')
 g.screen('link-down')
 g.qmp('set_link',name='nic',up=True)
 wait(lambda:g.symbol('net_state')==6 and ipv4()==0x0a000219,8,'static recovery on link return')
 g.screen('link-restored')
 # Invalid mask never changes the active address/configuration.
 field(1,'255.0.255.0');click(245,443);time.sleep(.2)
 assert g.symbol('trial')==0 and ipv4()==0x0a000219
 g.screen('invalid-mask-refused')
 # Trial DHCP followed by automatic rollback to static, without user confirmation.
 click(270,195);click(245,443);wait(lambda:g.symbol('trial')==1,3,'DHCP trial')
 wait(lambda:g.symbol('net_state')==3,15,'trial DHCP configuration')
 wait(lambda:g.symbol('trial')==0 and g.symbol('net_state')==6,35,'30s automatic rollback')
 assert ipv4()==0x0a000219;g.screen('automatic-rollback')
 key('esc');key('c');g.type('ping 10.0.2.2')
 def ping_replied():
  g.type('netstatus');return 'Ping: reply received' in g.terminal()
 wait(ping_replied,10,'gateway ping after link recovery')
 g.type('netstatus');g.screen('gateway-reachable')
 g.close();g=None
 reboot=out/'reboot';reboot.mkdir();g=Guest(a,reboot,disk);ready()
 wait(lambda:g.symbol('net_state')==6 and ipv4()==0x0a000219,5,'persistent static profile after reboot')
 key('n');g.screen('saved-profile-after-reboot')
 # DHCP reconnect is also exercised independently of the static profile.
 click(270,195);click(245,443);wait(lambda:g.symbol('net_state')==3,15,'DHCP after saved static')
 click(625,443);click(435,443) # keep only this session, preserve saved static profile
 wait(lambda:g.symbol('trial')==0,3,'session-only keep')
 g.qmp('set_link',name='nic',up=False);wait(lambda:g.symbol('net_state')==0 and ipv4()==0,5,'DHCP cable loss')
 g.qmp('set_link',name='nic',up=True);wait(lambda:g.symbol('net_state')==3 and ipv4()!=0,15,'automatic DHCP after reconnect')
 g.screen('dhcp-reconnected')
 click(555,195);click(245,443);wait(lambda:g.symbol('net_state')==0 and ipv4()==0,3,'administrative disconnect')
 click(530,443);wait(lambda:g.symbol('net_state')==3,15,'Revert restores automatic configuration')
 g.screen('disconnect-revert')

 (out/'result.json').write_text(json.dumps({'status':'PASS','backend':o.net_model,'checks':['automatic DHCP without Console','functional keyboard/pointer Network Manager','static address/mask/gateway/DNS apply and explicit save','invalid mask refused','link loss clears configuration and sockets; reconnect restores static profile','unconfirmed trial rolls back after 30 seconds','actual gateway ARP/ICMP after reconnect','MAC-bound profile survives installed reboot','automatic DHCP reconnect and administrative disconnect/revert']},indent=2))
except Exception:
 if g and g.process.poll() is None:g.screen('failure')
 raise
finally:
 if g:g.close()

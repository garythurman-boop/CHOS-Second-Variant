#!/usr/bin/env python3
"""Actual dual-stack Ethernet: IPv6 DAD/SLAAC/ND/echo and retained IPv4 DHCP/ping."""
import argparse,json,pathlib,shutil,struct,sys,time
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--out',type=pathlib.Path,required=True)
ap.add_argument('--net-model',choices=['e1000e','vmxnet3','virtio'],default='e1000e')
ap.add_argument('--mode',choices=['direct','uefi'],default='uefi')
o=ap.parse_args();out=o.out.resolve();out.mkdir(parents=True,exist_ok=False)
disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
a=argparse.Namespace(mode=o.mode,kernel=pathlib.Path('build/chos.elf'),uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=pathlib.Path('build/chos-0.30.0.iso'),memory='2G',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model=o.net_model,net_capture=True,storage_error=False,nvme=o.net_model!='virtio',no_rng=o.net_model!='virtio',hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk)
def key(k):g.hmp('sendkey '+k+' 10');time.sleep(.045)
def command(text):
 for c in text:key({' ': 'spc',':':'shift-semicolon','.':'dot','/':'slash','-':'minus'}.get(c,c))
 key('ret');time.sleep(.1)
def contains(command_text,needle):
 command(command_text);return needle in g.terminal()
def packets():
 data=(out/'network.pcap').read_bytes();pos=24;frames=[]
 while pos+16<=len(data):
  _,_,n,_=struct.unpack_from('<IIII',data,pos);pos+=16
  if pos+n>len(data):break
  frames.append(data[pos:pos+n]);pos+=n
 return frames
try:
 wait(lambda:'Initial CHOS GUI frame rendered' in g.text() if o.mode=='uefi' else 'chos> ' in g.text(),45,'desktop')
 wait(lambda:g.symbol('splash_active')==0,10,'splash')
 wait(lambda:g.symbol('net_state')==3,15,'existing IPv4 DHCP')
 key('c')
 def global6():
  command('net6status');t=g.terminal();return 'FEC0:' in t and 'state=2' in t
 wait(global6,15,'actual router advertisement and preferred SLAAC address')
 g.screen('slaac')
 command('ping6 fec0::2')
 wait(lambda:contains('net6status','Ping6: reply received'),10,'actual IPv6 gateway echo reply')
 g.screen('ipv6-echo')
 command('ping 10.0.2.2')
 wait(lambda:contains('netstatus','Ping: reply received'),10,'IPv4 gateway echo remains working')
 g.screen('dual-stack')
 if o.mode=='uefi':
  g.qmp('set_link',name='nic',up=False)
  wait(lambda:contains('net6status','link down'),8,'IPv6 link loss')
  command('clear');command('net6status');assert 'state=2' not in g.terminal(),'lost link must invalidate all IPv6 addresses'
  g.screen('link-down')
  g.qmp('set_link',name='nic',up=True)
  wait(global6,18,'fresh SLAAC/DAD after reconnect')
  wait(lambda:g.symbol('net_state')==3,15,'IPv4 DHCP after same reconnect')
  command('ping6 fec0::2');wait(lambda:contains('net6status','Ping6: reply received'),10,'IPv6 echo after reconnect')
  g.screen('reconnected')
 frames=packets()
 types={f[54] for f in frames if len(f)>=55 and f[12:14]==b'\x86\xdd' and f[20]==58}
 assert {128,129,133,134,135,136}.issubset(types),types
 assert any(len(f)>14 and f[12:14]==b'\x08\x00' for f in frames)
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'backend':o.net_model if o.mode=='uefi' else 'virtio-mmio','icmp6_wire_types':sorted(types),'checks':['real DAD, router solicitation/advertisement and preferred SLAAC address','ICMPv6 echo to the emulated router over selected real Ethernet queues','IPv4 DHCP and ICMP remain working','link loss invalidates IPv6 state; reconnect performs SLAAC/DAD again' if o.mode=='uefi' else 'direct VirtIO backend preserves shared RX ownership'],'limits':['IPv6 control plane only; TCP6/DNS6/DHCPv6 unavailable','QEMU router fixture, not a public Internet or Fusion IPv6 certification']},indent=2))
except Exception:
 if g.process.poll() is None:g.screen('failure');(out/'terminal.txt').write_text(g.terminal())
 raise
finally:g.close()

#!/usr/bin/env python3
"""Disposable UEFI guest Ethernet -> DHCP/DNS -> real browser TCP/HTTP fixture."""
import argparse,pathlib,shutil,sys,time,json,http.server,threading,socket,struct
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--out',required=True,type=pathlib.Path)
ap.add_argument('--net-model',choices=['virtio','vmxnet3'],default='vmxnet3')
a=ap.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
dns_queries=[]
dns_socket=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
dns_socket.bind(('127.0.0.1',53));dns_socket.settimeout(.5)
dns_running=True
def dns_fixture():
 while dns_running:
  try:query,peer=dns_socket.recvfrom(512)
  except socket.timeout:continue
  except OSError:break
  if len(query)<17:continue
  end=12
  while end<len(query) and query[end]:end+=query[end]+1
  end+=5
  if end>len(query):continue
  dns_queries.append(query[12:end].hex())
  reply=query[:2]+b'\x81\x80\x00\x01\x00\x01\x00\x00\x00\x00'+query[12:end]+b'\xc0\x0c\x00\x01\x00\x01'+struct.pack('!IH',60,4)+b'\x0a\x00\x02\x02'
  dns_socket.sendto(reply,peer)
threading.Thread(target=dns_fixture,daemon=True).start()
requests=[]
class Handler(http.server.BaseHTTPRequestHandler):
 def do_GET(self):
  requests.append(self.path)
  body=b'<html><body>chos-network-fixture-ok <a href="/next">Next</a></body></html>'
  if self.path=='/redirect':self.send_response(302);self.send_header('Location','/page');body=b''
  else:self.send_response(200)
  self.send_header('Content-Type','text/html');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
 def log_message(self,*args):pass
server=http.server.HTTPServer(('0.0.0.0',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start()
disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
args=argparse.Namespace(mode='uefi',uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=pathlib.Path('build/chos-0.30.0.iso'),memory='2G',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model=a.net_model,net_capture=True,storage_error=False,nvme=(a.net_model=='vmxnet3'),no_rng=(a.net_model=='vmxnet3'),hub_depth=0,empty_usb=False,tablet=True)
g=Guest(args,out,disk)
def key(k):g.hmp('sendkey '+k);time.sleep(.07)
def type_text(t):
 for c in t:key({':':'shift-semicolon','.':'dot','/':'slash','-':'minus',' ':'spc'}.get(c,c))
 key('ret')
try:
 wait(lambda:'Initial CHOS GUI frame rendered' in g.text(),45,'desktop')
 wait(lambda:g.symbol('splash_active')==0,10,'splash')
 wait(lambda:g.symbol('net_state')==3,20,'DHCP lease')
 if a.net_model=='vmxnet3':assert 'No usable VirtIO transport' in g.text(),'VMXNET3 must operate independently of VirtIO'
 key('c');type_text('run /bin/browser.chx');wait(lambda:g.symbol('kernel_browser_active')==1,5,'browser')
 type_text(f'http://10.0.2.2:{server.server_port}/redirect')
 wait(lambda:b'chos-network-fixture-ok' in g.block('page',size=4096),12,'browser over selected real Ethernet driver')
 assert requests==['/redirect','/page'],requests
 g.screen('http-loaded')
 # Ctrl+L keeps existing address; clear it through real key reports.
 key('ctrl-l')
 old=g.block('address',size=512).split(b'\0')[0]
 for _ in old:key('backspace')
 type_text('https://example.com/')
 wait(lambda:b'HTTPS' in g.block('page',size=4096),5,'HTTPS remains fail closed')
 assert requests==['/redirect','/page']
 g.screen('https-blocked')
 key('esc');key('esc');key('c');type_text('netstatus');g.screen('network-status')
 # Explicit session resolver points at a local deterministic UDP DNS fixture.
 type_text('dnsserver 10.0.2.2')
 wait(lambda:'Session DNS resolver set.' in g.terminal(),4,'session resolver configuration')
 type_text('run /bin/browser.chx');key('ctrl-l')
 old=g.block('address',size=512).split(b'\0')[0]
 for _ in old:key('backspace')
 type_text(f'http://chos.test:{server.server_port}/page')
 wait(lambda:g.symbol('dns_state')==2,12,'real guest DNS lookup')
 wait(lambda:b'chos-network-fixture-ok' in g.block('page',size=4096),12,'HTTP after DNS resolution')
 assert dns_queries
 g.screen('dns-and-http-loaded')
 (out/'result.json').write_text(json.dumps({'status':'PASS','backend':a.net_model,'requests':requests,'dns_queries':dns_queries,'checks':['native Ethernet queues and DHCP','browser TCP HTTP body and relative redirect','HTTPS blocked without TLS','DNS A query and reply over actual adapter']},indent=2))
except Exception:
 if g.process.poll() is None:
  g.screen('failure')
  (out/'page.txt').write_bytes(g.block('page',size=4096))
  if a.net_model=='vmxnet3':
   (out/'vmx-info.bin').write_bytes(g.block('vmx_info',size=128))
 raise
finally:
 g.close();server.shutdown();server.server_close();dns_running=False;dns_socket.close()

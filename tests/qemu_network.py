#!/usr/bin/env python3
"""Disposable UEFI guest Ethernet -> DHCP/DNS -> real browser TCP/HTTP fixture."""
import argparse,pathlib,shutil,sys,time,json,http.server,threading,socket,struct,hashlib
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
from guest_user_probe import GuestUserProbe
from guest_iso_artifacts import verify_iso_kernel
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--out',required=True,type=pathlib.Path)
ap.add_argument('--net-model',choices=['virtio','vmxnet3','e1000e'],default='vmxnet3')
ap.add_argument('--iso',type=pathlib.Path,default=pathlib.Path('build/chos-0.30.0.iso'))
ap.add_argument('--uefi-kernel',type=pathlib.Path,default=pathlib.Path('build/uefi-kernel/chos.elf'))
ap.add_argument('--browser-elf',type=pathlib.Path,default=pathlib.Path('build/userland/browser.elf'))
ap.add_argument('--network-loss',action='store_true',help='also interrupt an actual in-flight HTTP response and reconnect')
a=ap.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
# Freeze all symbols and boot bytes together. An independently rebuilt ELF can
# otherwise make read-only probes inspect zeros at stale offsets and falsely
# diagnose a working driver. Check the actual kernel embedded in this ISO.
inputs=out/'inputs';inputs.mkdir()
for source,name in [(a.iso,'boot.iso'),(a.uefi_kernel,'kernel.elf'),(a.browser_elf,'browser.elf')]:
 shutil.copyfile(source,inputs/name)
verify_iso_kernel(inputs/'boot.iso',inputs/'kernel.elf')
a.iso=inputs/'boot.iso';a.uefi_kernel=inputs/'kernel.elf';a.browser_elf=inputs/'browser.elf'
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
slow_started=threading.Event();slow_release=threading.Event()
class Handler(http.server.BaseHTTPRequestHandler):
 def do_GET(self):
  requests.append(self.path)
  body=b'<html><body>chos-network-fixture-ok <a href="/next">Next</a></body></html>'
  if self.path=='/redirect':self.send_response(302);self.send_header('Location','/page');body=b''
  else:self.send_response(200)
  self.send_header('Content-Type','text/html');self.send_header('Content-Length',str(len(body)));self.end_headers()
  if self.path=='/slow':
   slow_started.set()
   slow_release.wait(20) # Bounded server fixture, released after actual guest cancellation.
  try:self.wfile.write(body)
  except (BrokenPipeError,ConnectionResetError):pass
 def log_message(self,*args):pass
server=http.server.HTTPServer(('0.0.0.0',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start()
disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
args=argparse.Namespace(mode='uefi',uefi_kernel=a.uefi_kernel,iso=a.iso,memory='2G',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model=a.net_model,net_capture=True,storage_error=False,nvme=(a.net_model!='virtio'),no_rng=(a.net_model!='virtio'),hub_depth=0,empty_usb=False,tablet=True)
g=Guest(args,out,disk)
probe=GuestUserProbe(g)
browser_app=out/'browser-input.elf';shutil.copyfile(a.browser_elf,browser_app)
browser_sha256=hashlib.sha256(browser_app.read_bytes()).hexdigest()
browser_pid=None
def browser_owner():
 client=probe.foreground()
 return client if client and client['title'].startswith('CHOS Web Browser') and client['pid'] else None
def browser_open():
 global browser_pid
 wait(browser_owner,8,'isolated EL0 browser window')
 client=browser_owner();browser_pid=client['pid']
 assert probe.process(browser_pid)['state'] in (2,3)
 assert probe.value(browser_pid,browser_app,'window_id')==client['window_id']
 assert g.symbol('kernel_browser_active')==0,'UEFI must use the isolated app rather than the recovery kernel browser'
def browser_block(name,size):return probe.block(browser_pid,browser_app,name,size)
def browser_text(name,size):return probe.text(browser_pid,browser_app,name,size)
def key(k):g.hmp('sendkey '+k);time.sleep(.07)
def type_text(t):
 for c in t:key({':':'shift-semicolon','.':'dot','/':'slash','-':'minus',' ':'spc'}.get(c,c))
 key('ret')
try:
 wait(lambda:'Initial CHOS GUI frame rendered' in g.text(),45,'desktop')
 assert 'UEFI CPU isolation active' in g.text(),'network regression requires the isolated UEFI runtime'
 wait(lambda:g.symbol('splash_active')==0,10,'splash')
 wait(lambda:g.symbol('net_state')==3,20,'DHCP lease')
 if a.net_model!='virtio':assert 'No usable VirtIO transport' in g.text(),'VMXNET3 must operate independently of VirtIO'
 key('c');type_text('run /bin/browser.chx');browser_open()
 type_text(f'http://10.0.2.2:{server.server_port}/redirect')
 wait(lambda:b'chos-network-fixture-ok' in browser_block('page',4096),12,'isolated browser over selected real Ethernet driver')
 assert requests==['/redirect','/page'],requests
 assert browser_text('current_url',256)==f'http://10.0.2.2:{server.server_port}/page'.encode()
 g.screen('http-loaded')
 # Ctrl+L keeps existing address; clear it through real key reports.
 key('ctrl-l')
 old=browser_text('address',256)
 for _ in old:key('backspace')
 type_text('https://example.com/')
 wait(lambda:b'Secure browsing is not available' in browser_block('page',4096),5,'HTTPS remains fail closed')
 assert browser_text('address',256)==b'https://example.com/'
 assert requests==['/redirect','/page']
 g.screen('https-blocked')
 # Configuration belongs to the privileged OS service, while the browser and
 # ordinary EL0 Console retain read-only networking access. Use the trusted
 # recovery serial command for this deterministic fixture's session resolver.
 g.serial('netstatus');wait(lambda:'DHCP: bound' in g.text(),4,'OS network diagnostics')
 g.screen('network-status')
 g.serial('dnsserver 10.0.2.2')
 wait(lambda:'Session DNS resolver set.' in g.text(),4,'trusted session resolver configuration')
 key('ctrl-l')
 old=browser_text('address',256)
 for _ in old:key('backspace')
 type_text(f'http://chos.test:{server.server_port}/page')
 wait(lambda:g.symbol('dns_state')==2,12,'real guest DNS lookup')
 wait(lambda:b'chos-network-fixture-ok' in browser_block('page',4096),12,'HTTP after DNS resolution in isolated EL0 browser')
 assert b'\x04chos\x04test\x00\x00\x01\x00\x01'.hex() in dns_queries,dns_queries
 assert requests==['/redirect','/page','/page'],requests
 g.screen('dns-and-http-loaded')
 checks=['native Ethernet queues and DHCP','isolated EL0 browser window bound to its actual process owner','browser TCP HTTP body and relative redirect','HTTPS blocked without TLS and no plaintext downgrade','DNS A query and reply over actual adapter']
 if a.network_loss:
  key('ctrl-l');old=browser_text('address',256)
  for _ in old:key('backspace')
  type_text(f'http://10.0.2.2:{server.server_port}/slow')
  wait(slow_started.is_set,8,'actual HTTP server receives in-flight request')
  wait(lambda:probe.value(browser_pid,browser_app,'phase')==4,5,'browser receiving incomplete HTTP response')
  assert int.from_bytes(browser_block('socket_handle',4),'little',signed=True)>0
  g.qmp('set_link',name='nic',up=False)
  wait(lambda:g.symbol('net_state')==0 and g.symbol('net_state',12)==0,5,'link loss clears interface configuration')
  wait(lambda:b'could not complete the HTTP connection' in browser_block('page',4096),5,'in-flight browser reports network failure')
  wait(lambda:probe.value(browser_pid,browser_app,'phase')==0 and int.from_bytes(browser_block('socket_handle',4),'little',signed=True)==-1,5,'failed browser socket is released')
  g.screen('in-flight-network-loss')
  slow_release.set()
  g.qmp('set_link',name='nic',up=True)
  wait(lambda:g.symbol('net_state')==3 and g.symbol('net_state',12)!=0,15,'DHCP reacquires after link returns')
  key('ctrl-l');old=browser_text('address',256)
  for _ in old:key('backspace')
  type_text(f'http://10.0.2.2:{server.server_port}/page')
  wait(lambda:b'chos-network-fixture-ok' in browser_block('page',4096),12,'same isolated browser recovers after reconnection')
  assert requests==['/redirect','/page','/page','/slow','/page'],requests
  g.screen('http-after-reconnection')
  checks+=['actual in-flight HTTP failure on link loss releases its process-owned socket','DHCP reconnect and new HTTP request in the same isolated browser process']
 (out/'result.json').write_text(json.dumps({'status':'PASS','backend':a.net_model,'browser_elf_sha256':browser_sha256,'browser_pid':browser_pid,'requests':requests,'dns_queries':dns_queries,'checks':checks},indent=2))
except Exception:
 if g.process.poll() is None:
  diagnostics={}
  for name,size in [('net_state',112),('dns_state',280),('e_info',128),('vmx_info',128),('e_rx',512),('e_rx_next',4),('rx_frame',1536)]:
   if name in g.symbols:
    diagnostics[name]=g.block(name,size=size).hex()
  (out/'network-debug.json').write_text(json.dumps(diagnostics,indent=2))
  g.screen('failure')
  if browser_pid:
   try:(out/'page.txt').write_bytes(browser_block('page',4096))
   except AssertionError:pass
  if a.net_model=='vmxnet3':
   (out/'vmx-info.bin').write_bytes(g.block('vmx_info',size=128))
 raise
finally:
 slow_release.set();g.close();server.shutdown();server.server_close();dns_running=False;dns_socket.close()

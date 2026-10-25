#!/usr/bin/env python3
"""Actual owner-bound EL0 TCP6 stream over QEMU Ethernet and host IPv6 loopback.
No remote website, TLS, Fusion, Wi-Fi or full browser result is inferred.
"""
import argparse,hashlib,json,pathlib,re,shutil,socket,struct,subprocess,sys,threading,time
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--out',type=pathlib.Path,required=True)
ap.add_argument('--missing-entropy',action='store_true')
o=ap.parse_args();out=o.out.resolve();out.mkdir(parents=True,exist_ok=False)
disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
app=pathlib.Path('build/userland/tcp6_test.chcx');subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/BIN/TCP6TEST.CHX',str(app)],check=True)
server=socket.socket(socket.AF_INET6,socket.SOCK_STREAM);server.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
server.bind(('::1',0));server.listen(2);server.settimeout(25);port=server.getsockname()[1]
requests=[];errors=[]
def serve():
 try:
  for _ in range(2):
   c,peer=server.accept();c.settimeout(15)
   with c:
    request=b''
    while not request.endswith(b'\n'):
     data=c.recv(256)
     if not data:raise AssertionError('EOF before complete request')
     request+=data
     if len(request)>256:raise AssertionError('unbounded request')
    assert request==b'CHOS IPv6 stream fixture\n',request
    requests.append({'peer':str(peer),'request_bytes':len(request),'response_bytes':6144})
    c.sendall(bytes((i*17+0x31)&255 for i in range(6144)));c.shutdown(socket.SHUT_WR)
    assert c.recv(1)==b'', 'client must send FIN after response EOF'
 except Exception as e:errors.append(repr(e))
thread=None
if not o.missing_entropy:thread=threading.Thread(target=serve,daemon=True);thread.start()
a=argparse.Namespace(mode='direct',kernel=pathlib.Path('build/chos.elf'),memory='512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,no_rng=o.missing_entropy,net_capture=True)
g=Guest(a,out,disk)
try:
 wait(lambda:'chos> ' in g.text(),15,'direct shell')
 marker='[ OK ] EL0 TCP6 missing entropy fails closed' if o.missing_entropy else '[ OK ] EL0 TCP6 wire data, EOF, FIN and syscall bounds passed'
 for iteration in range(1 if o.missing_entropy else 2):
  g.serial('exec /BIN/TCP6TEST.CHX '+('missing' if o.missing_entropy else str(port)))
  wait(lambda:g.text().count(marker)>iteration or '[FAIL] EL0 TCP6' in g.text(),25,'real EL0 TCP6 completion')
  assert '[FAIL] EL0 TCP6' not in g.text(),g.text()[-2000:]
  deadline=time.monotonic()+8
  while True:
   before=g.text().count('CHOS process table (foreground and scheduled EL0 processes)');g.serial('procinfo')
   wait(lambda:g.text().count('CHOS process table (foreground and scheduled EL0 processes)')>before,3,'actual process cleanup diagnostic')
   section=g.text().split('CHOS process table (foreground and scheduled EL0 processes)')[-1]
   if re.search(r'Active: 0;.*pending cleanup: 0',section):break
   if time.monotonic()>=deadline:raise AssertionError('process reaper did not quiesce')
   time.sleep(.1)
 if thread:
  thread.join(2);assert not thread.is_alive() and not errors and len(requests)==2,(requests,errors)
 frames=[];data=(out/'network.pcap').read_bytes();pos=24
 while pos+16<=len(data):
  _,_,n,_=struct.unpack_from('<IIII',data,pos);pos+=16
  if pos+n>len(data):break
  frames.append(data[pos:pos+n]);pos+=n
 tcp=[f for f in frames if len(f)>=74 and f[12:14]==b'\x86\xdd' and f[20]==6]
 if o.missing_entropy:assert not tcp,'entropy absence must emit no TCP6 SYN'
 else:
  assert any(f[67]&2 for f in tcp) and any(f[67]&1 for f in tcp), 'actual TCP6 SYN/FIN exchange'
  assert any(len(f)>74 for f in tcp),'actual TCP6 data frames'
  assert g.text().count('[ OK ] EL0 TCP6 foreign owner denied')==2
 result={'status':'PASS','application_sha256':hashlib.sha256(app.read_bytes()).hexdigest(),'profile':'direct VirtIO-MMIO, real EL0 SVC and QEMU user-net to host IPv6 loopback','tcp6_wire_frames':len(tcp),'host_requests':requests,'checks':['actual EL0 pointer, size and read-only-buffer rejection','no TCP6 emitted when hardware entropy missing'] if o.missing_entropy else ['actual EL0 IPv6 handshake and host-verified request','6144 byte patterned stream exceeds bounded 2048-byte receive queue','actual peer EOF and graceful FIN/ACK','foreign-process socket ownership denied for status/send/receive/close/abort','second launch proves reaper released four intentionally leaked owner slots'],'limits':['no TLS/browser/site claim','QEMU direct evidence does not establish Fusion or Pi IPv6 transport support']}
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
except Exception:
 if g.process.poll() is None:(out/'failure-terminal.txt').write_text(g.terminal())
 raise
finally:
 g.close();server.close()

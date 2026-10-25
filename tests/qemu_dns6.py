#!/usr/bin/env python3
"""Actual EL0 DNS6 over a disposable QEMU socket-backed Ethernet link.
The loopback-only fixture supplies router advertisements/RDNSS and controlled DNS;
it is a protocol peer, not a replacement for a guest driver or guest network API.
"""
import argparse,hashlib,ipaddress,json,pathlib,re,shutil,socket,struct,subprocess,sys,threading,time
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
ROUTER=ipaddress.IPv6Address('fe80::1').packed
ROUTER_MAC=bytes.fromhex('02aabbccdd01')
ALL_NODES=ipaddress.IPv6Address('ff02::1').packed
PREFIX=ipaddress.IPv6Address('2001:db8:1::').packed
ANSWER=ipaddress.IPv6Address('2001:db8:1::7').packed

def checksum(data):
 if len(data)&1:data+=b'\0'
 n=sum(struct.unpack('!%dH'%(len(data)//2),data))
 while n>>16:n=(n&65535)+(n>>16)
 return (~n)&65535

def packet(dst_mac,src,dst,protocol,payload,hop=64,bad_checksum=False):
 data=bytearray(payload);offset=2 if protocol==58 else 6
 data[offset:offset+2]=b'\0\0'
 c=checksum(src+dst+struct.pack('!I3xB',len(data),protocol)+data)
 if not c:c=65535
 if bad_checksum:c^=1
 struct.pack_into('!H',data,offset,c)
 return dst_mac+ROUTER_MAC+b'\x86\xdd'+struct.pack('!IHBB',0x60000000,len(data),protocol,hop)+src+dst+data

def router_advertisement(lifetime=120):
 data=struct.pack('!BBHBBHII',134,0,0,64,0,300,0,0)
 data+=b'\1\1'+ROUTER_MAC
 data+=struct.pack('!BBBBIII',3,4,64,0xc0,300,120,0)+PREFIX
 data+=struct.pack('!BBHI',25,3,0,lifetime)+ROUTER
 return packet(bytes.fromhex('333300000001'),ROUTER,ALL_NODES,58,data,255)

def dns_name(name):return b''.join(bytes([len(x)])+x.encode('ascii') for x in name.split('.'))+b'\0'

class Router:
 def __init__(self,missing):
  self.listener=socket.socket();self.listener.bind(('127.0.0.1',0));self.listener.listen(1);self.listener.settimeout(10)
  self.port=self.listener.getsockname()[1];self.missing=missing;self.stop=False;self.errors=[];self.requests=[];self.injected=[];self.conn=None;self.advertise=True
  self.thread=threading.Thread(target=self.run,daemon=True);self.thread.start()
 def send(self,frame,label):
  self.conn.sendall(struct.pack('!I',len(frame))+frame)
  if label not in self.injected:self.injected.append(label)
 def handle(self,f,pending):
  if len(f)<54 or f[12:14]!=b'\x86\xdd':return
  src,dst=f[22:38],f[38:54];mac=f[6:12];data=f[54:]
  if f[20]==58 and len(data)>=24 and data[0]==135 and src!=b'\0'*16 and data[8:24]==ROUTER:
   na=struct.pack('!BBHI',136,0,0,0x60000000)+ROUTER+b'\2\1'+ROUTER_MAC
   self.send(packet(mac,ROUTER,src,58,na,255),'neighbor-advertisement');return
  if f[20]!=17 or len(data)<20:return
  sport,dport,size,_=struct.unpack('!HHHH',data[:8])
  if dport!=53 or dst!=ROUTER:return
  query=data[8:size];at=12;parts=[]
  while at<len(query) and query[at]:
   n=query[at];at+=1
   if n>63 or at+n>len(query):raise AssertionError('invalid guest DNS QNAME')
   parts.append(query[at:at+n].decode('ascii'));at+=n
  name='.'.join(parts);assert query[at:]==b'\0\0\x1c\0\1',query.hex()
  assert 49152<=sport<=65535
  self.requests.append({'hostname':name,'source_port':sport,'query_id':query[:2].hex()})
  if self.missing:raise AssertionError('guest emitted DNS query without entropy')
  if name=='timeout.chos.test':return
  if name=='withdraw.chos.test':
   self.advertise=False;self.send(router_advertisement(0),'real-rdnss-withdrawal');return
  def reply(message,label,delay=0,origin=ROUTER,bad=False):
   udp=struct.pack('!HHHH',53,sport,len(message)+8,0)+message
   pending.append((time.monotonic()+delay,packet(mac,origin,src,17,udp,bad_checksum=bad),label))
  question=query[12:]
  if name=='missing.chos.test':reply(query[:2]+struct.pack('!HHHHH',0x8183,1,0,0,0)+question,'matching-nxdomain');return
  assert name=='valid.chos.test',name
  head=query[:2]+struct.pack('!HHHHH',0x8180,1,1,0,0)+question
  rr=b'\xc0\x0c'+struct.pack('!HHIH',28,1,30,16)+ANSWER
  valid=head+rr
  # Invalid packets carry a different address: accepting one must fail the
  # actual EL0 result assertion, even if a later valid packet is also sent.
  untrusted=ipaddress.IPv6Address('2001:db8:1::99').packed
  hostile=valid[:-16]+untrusted
  reply(hostile,'spoofed-peer-address',0,ipaddress.IPv6Address('fe80::2').packed)
  reply(bytes([hostile[0]^1])+hostile[1:],'wrong-query-id',.02)
  reply(head+dns_name('unrelated.chos.test')+struct.pack('!HHIH',28,1,30,16)+untrusted,'unrelated-answer-owner',.04)
  reply(hostile,'invalid-udp-checksum',.06,bad=True)
  reply(hostile[:-1],'truncated-dns-answer',.08)
  reply(valid,'matching-aaaa',.25)
 def run(self):
  try:
   self.conn,_=self.listener.accept();self.conn.settimeout(.02);buf=b'';pending=[];last_ra=0
   while not self.stop:
    now=time.monotonic()
    if self.advertise and now-last_ra>=.3:self.send(router_advertisement(),'real-ra-rdnss');last_ra=now
    due=[x for x in pending if x[0]<=now];pending=[x for x in pending if x[0]>now]
    for _,frame,label in due:self.send(frame,label)
    try:data=self.conn.recv(8192)
    except socket.timeout:continue
    if not data:break
    buf+=data
    while len(buf)>=4:
     n=struct.unpack('!I',buf[:4])[0]
     if n<14 or n>1518:raise AssertionError(('guest frame bounds',n))
     if len(buf)<n+4:break
     self.handle(buf[4:n+4],pending);buf=buf[n+4:]
  except Exception as e:
   if not self.stop:self.errors.append(repr(e))
 def close(self):
  self.stop=True
  if self.conn:
   try:self.conn.shutdown(socket.SHUT_RDWR)
   except OSError:pass
   self.conn.close()
  self.listener.close();self.thread.join(2)

ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',type=pathlib.Path,required=True);ap.add_argument('--missing-entropy',action='store_true');o=ap.parse_args()
out=o.out.resolve();out.mkdir(parents=True,exist_ok=False);disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
app=pathlib.Path('build/userland/dns6_test.chcx');subprocess.run(['python3','tools/mkchfs.py','put',str(disk),'/BIN/DNS6TEST.CHX',str(app)],check=True)
r=Router(o.missing_entropy)
a=argparse.Namespace(mode='direct',kernel=pathlib.Path('build/chos.elf'),memory='512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,no_rng=o.missing_entropy,net_capture=True,netdev=f'socket,id=net,connect=127.0.0.1:{r.port}')
g=Guest(a,out,disk)
try:
 wait(lambda:'chos> ' in g.text(),15,'direct shell')
 marker='[ OK ] EL0 DNS6 hardware entropy absence fails closed' if o.missing_entropy else '[ OK ] EL0 DNS6 real AAAA, malformed rejection, NXDOMAIN and retry timeout passed'
 g.serial('exec /BIN/DNS6TEST.CHX'+(' missing' if o.missing_entropy else ''))
 wait(lambda:marker in g.text() or '[FAIL] EL0 DNS6' in g.text() or r.errors,25,'actual EL0 DNS6 result')
 assert marker in g.text() and '[FAIL] EL0 DNS6' not in g.text() and not r.errors,(g.text()[-3000:],r.errors)
 if o.missing_entropy:assert not r.requests
 else:
  assert len([x for x in r.requests if x['hostname']=='timeout.chos.test'])==4,r.requests
  assert {'wrong-query-id','spoofed-peer-address','unrelated-answer-owner','invalid-udp-checksum','truncated-dns-answer','matching-aaaa','matching-nxdomain','real-rdnss-withdrawal'}.issubset(r.injected),r.injected
 result={'status':'PASS','application_sha256':hashlib.sha256(app.read_bytes()).hexdigest(),'profile':'actual direct EL0, VirtIO-MMIO Ethernet and loopback-only socket router/DNS peer','requests':r.requests,'injected_peer_packets':r.injected,'checks':['actual RA/RDNSS and guest SLAAC','EL0 null/read-only/short buffer validation','hardware entropy missing: no DNS datagram sent'] if o.missing_entropy else ['actual RA/RDNSS, neighbor discovery and guest UDP6 AAAA exchange','wrong peer, query ID, unrelated owner, bad checksum and truncation rejected','exact NXDOMAIN preserved as response error','three bounded retries and seven-second unanswered-query timeout','actual zero-lifetime RDNSS withdrawal cancels pending query','EL0 pointer/length validation and same-owner pending query refusal'],'limits':['protocol fixture, not public DNS/DNSSEC/TLS/site tests','no Fusion or Pi IPv6 validation inferred']}
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
except Exception:
 (out/'fixture.json').write_text(json.dumps({'requests':r.requests,'injected':r.injected,'errors':r.errors},indent=2));raise
finally:g.close();r.close()

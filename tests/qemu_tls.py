#!/usr/bin/env python3
"""Real native CHOS TLS1.2 client against ephemeral local certificate fixtures.
No production CA store, modern browser, trusted-time or physical Wi-Fi claim.
"""
import argparse,datetime,hashlib,json,pathlib,re,shutil,socket,ssl,subprocess,sys,threading,time
from cryptography import x509
from cryptography.hazmat.primitives import hashes,serialization
from cryptography.hazmat.primitives.asymmetric import ec,rsa
from cryptography.x509.oid import NameOID
sys.path.insert(0,str(pathlib.Path(__file__).parent))
from qemu_platform import Guest,wait
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--out',type=pathlib.Path,required=True)
p.add_argument('--mode',choices=['direct','uefi'],default='direct')
p.add_argument('--missing-entropy',action='store_true')
p.add_argument('--missing-clock',action='store_true')
o=p.parse_args();out=o.out.resolve();out.mkdir(parents=True,exist_ok=False)
fixture=out/'fixture';fixture.mkdir()
from tls_fixture_certs import certificate as cert,fixture_roots
rootkey=fixture_roots(fixture)
servers=[];events=[];lock=threading.Lock();stop=threading.Event()
def server(kind,begin=2029,end=2031):
 key=rsa.generate_private_key(public_exponent=65537,key_size=2048) if kind.startswith('rsa') else ec.generate_private_key(ec.SECP384R1() if kind=='aes256' else ec.SECP256R1());leaf=cert('fixture.chos.test',key,'CHOS ephemeral test CA',rootkey,begin,end,server_auth=(kind!='wrong-purpose'))
 certfile=fixture/(kind+'.pem');keyfile=fixture/(kind+'.key')
 certfile.write_bytes(leaf.public_bytes(serialization.Encoding.PEM))
 keyfile.write_bytes(key.private_bytes(serialization.Encoding.PEM,serialization.PrivateFormat.PKCS8,serialization.NoEncryption()))
 keyfile.chmod(0o600)
 ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);ctx.minimum_version=ssl.TLSVersion.TLSv1_2;ctx.maximum_version=ssl.TLSVersion.TLSv1_2
 ctx.set_ciphers(('ECDHE-RSA-' if kind.startswith('rsa') else 'ECDHE-ECDSA-')+('AES256-GCM-SHA384' if kind in ('aes256','rsa256') else 'AES128-GCM-SHA256'));
 if kind=='aes256':ctx.set_ecdh_curve('secp384r1')
 ctx.load_cert_chain(certfile,keyfile);ctx.set_alpn_protocols(['http/1.1'])
 def sni(sock,hostname,context):
  with lock:events.append({'kind':kind,'sni':hostname})
 ctx.set_servername_callback(sni)
 listener=socket.socket();listener.bind(('0.0.0.0',0));listener.listen();listener.settimeout(.2);servers.append(listener)
 def handle(raw):
  raw.settimeout(20)
  with lock:events.append({'kind':kind,'accepted':True})
  try:
   if kind=='stall':stop.wait(18);return
   if kind=='malformed':raw.sendall(b'\x16\x03\x03\x00\x04FAIL');return
   conn=ctx.wrap_socket(raw,server_side=True)
   with lock:events.append({'kind':kind,'version':conn.version(),'cipher':conn.cipher()[0],'alpn':conn.selected_alpn_protocol()})
   request=b''
   while b'\r\n\r\n' not in request and len(request)<4096:
    part=conn.recv(512)
    if not part:return
    request+=part
   assert request.startswith(b'GET /fixture HTTP/1.1\r\n'),request
   with lock:events.append({'kind':kind,'request':'GET /fixture'})
   body=b'HTTP/1.1 200 OK\r\nContent-Length: 26\r\nConnection: close\r\n\r\nchos-native-tls-fixture-ok\n'
   # Many authenticated records exercise partial record and application reads.
   for offset in range(0,len(body),17):conn.sendall(body[offset:offset+17])
   if kind=='truncated':conn.close();return
   try:conn.unwrap().close()
   except (ssl.SSLError,OSError):pass # peer abort after receiving close_notify
  except (ssl.SSLError,OSError) as exc:
   with lock:events.append({'kind':kind,'server_error':str(exc)})
  finally:raw.close()
 def accept():
  while not stop.is_set():
   try:raw,_=listener.accept()
   except socket.timeout:continue
   except OSError:return
   threading.Thread(target=handle,args=(raw,),daemon=True).start()
 threading.Thread(target=accept,daemon=True).start();return listener.getsockname()[1]
ports={kind:server(kind,*dates) for kind,dates in [('valid',(2029,2031)),('rsa',(2029,2031)),('rsa256',(2029,2031)),('aes256',(2029,2031)),('wrong-purpose',(2029,2031)),('expired',(2027,2028)),('future',(2032,2033)),('truncated',(2029,2031)),('malformed',(2029,2031)),('stall',(2029,2031))]}
disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
app=pathlib.Path('build/userland/tls_test.elf')
for dest,src in [('/BIN/TLSTEST.ELF',app),('/TLS/CA.DER',fixture/'ca.der'),('/TLS/OTHER.DER',fixture/'other.der'),('/TLS/BAD.DER',fixture/'bad.der')]:
 subprocess.run(['python3','tools/mkchfs.py','put',str(disk),dest,str(src)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=pathlib.Path('build/chos.elf'),uefi_kernel=pathlib.Path('build/uefi-kernel/chos.elf'),iso=pathlib.Path('build/chos-0.30.0.iso'),memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base='1970-01-01T00:00:00' if o.missing_clock else '2030-06-01T12:00:00',trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=o.missing_entropy,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk);cases=[]
def free_pages():
 end=time.monotonic()+8
 while True:
  before=g.text().count('CHOS process table (foreground and scheduled EL0 processes)');g.serial('procinfo')
  wait(lambda:g.text().count('CHOS process table (foreground and scheduled EL0 processes)')>before,3,'process diagnostic')
  section=g.text().rsplit('CHOS process table (foreground and scheduled EL0 processes)',1)[-1]
  if re.search(r'Active: 0;.*pending cleanup: 0',section):break
  if time.monotonic()>end:raise AssertionError('TLS process cleanup did not quiesce')
  time.sleep(.05)
 before=len(re.findall(r'Free pages: (\d+)',g.text()));g.serial('meminfo')
 wait(lambda:len(re.findall(r'Free pages: (\d+)',g.text()))>before,3,'free pages')
 return int(re.findall(r'Free pages: (\d+)',g.text())[-1])
def run(kind,expected=0,host='fixture.chos.test',ca='/TLS/CA.DER'):
 before=len(g.text());started=time.monotonic();g.serial(f'exec /BIN/TLSTEST.ELF {ports[kind]} {host} {expected} {ca}')
 if kind=='stall':
  wait(lambda:any(e.get('kind')=='stall' and e.get('accepted') for e in events),5,'stall transport accepted')
  g.hmp('sendkey f1');wait(lambda:g.symbol('view')==4,3,'desktop diagnostics remains responsive during TLS timeout')
  g.screen('responsive-during-tls-stall');g.hmp('sendkey esc')
 wait(lambda:'[ OK ] native TLS' in g.text()[before:] or '[FAIL]' in g.text()[before:] or '[USER FAULT]' in g.text()[before:] or '[FATAL]' in g.text()[before:],40,'native TLS '+kind)
 section=g.text()[before:];assert '[ OK ] native TLS' in section,section
 cases.append({'kind':kind,'expected':expected,'hostname':host,'seconds':time.monotonic()-started,'result':'PASS'})
try:
 wait(lambda:'chos> ' in g.text(),45,'scheduled runtime')
 if o.mode=='uefi':assert 'UEFI CPU isolation active' in g.text()
 wait(lambda:g.symbol('splash_active')==0,10,'splash')
 wait(lambda:g.symbol('net_state')==3,20,'DHCP lease')
 cold=free_pages();baseline=None
 if o.missing_clock or o.missing_entropy:
  run('valid',991 if o.missing_clock else 992);assert not events,events
 else:
  run('valid');baseline=free_pages()
  for kind,expected,host,ca in [('rsa',0,'fixture.chos.test','/TLS/CA.DER'),('rsa256',0,'fixture.chos.test','/TLS/CA.DER'),('aes256',0,'fixture.chos.test','/TLS/CA.DER'),('valid',4,'wrong.chos.test','/TLS/CA.DER'),('valid',8,'fixture.chos.test','/TLS/OTHER.DER'),('wrong-purpose',4096,'fixture.chos.test','/TLS/CA.DER'),('expired',1,'fixture.chos.test','/TLS/CA.DER'),('future',512,'fixture.chos.test','/TLS/CA.DER'),('malformed',998,'fixture.chos.test','/TLS/CA.DER'),('truncated',997,'fixture.chos.test','/TLS/CA.DER'),('stall',996,'fixture.chos.test','/TLS/CA.DER'),('valid',993,'fixture.chos.test','/TLS/BAD.DER'),('valid',995,'fixture.chos.test','/TLS/CA.DER'),('valid',0,'fixture.chos.test','/TLS/CA.DER')]:
   run(kind,expected,host,ca);observed=free_pages();assert observed==baseline,('TLS lifecycle leaked pages',baseline,observed)
  assert any(e.get('sni')=='fixture.chos.test' for e in events),events
  assert any(e.get('sni')=='wrong.chos.test' for e in events),events
  assert sum('request' in e for e in events)==6,events
  assert all(e.get('alpn')=='http/1.1' for e in events if 'alpn' in e),events
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'profile':'native TLS1.2 ECDHE-ECDSA/RSA AES128/256-GCM; isolated prerequisite, not integrated browser','application_sha256':hashlib.sha256(app.read_bytes()).hexdigest(),'missing_clock':o.missing_clock,'missing_entropy':o.missing_entropy,'cold_free_pages':cold,'free_pages_before':baseline,'free_pages_after':free_pages(),'cases':cases,'server_events':events,'limits':['ephemeral test root only; production trust store/update policy still open','firmware/RTC clock availability is not authenticated time','TLS1.3, browser integration, full modern engine and live Wi-Fi remain open','QEMU evidence; Fusion/physical TLS NOT RUN']},indent=2)+'\n')
except Exception:
 (out/'failure.txt').write_text(g.text());(out/'server-events.json').write_text(json.dumps(events,indent=2));raise
finally:
 stop.set();g.close()
 for s in servers:s.close()

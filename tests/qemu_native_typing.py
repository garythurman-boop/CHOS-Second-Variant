#!/usr/bin/env python3
"""Actual isolated Console/browser/editor input, blink, persistence and bounds."""
import argparse,hashlib,json,pathlib,shutil,subprocess,sys,time
sys.path.insert(0,str(pathlib.Path(__file__).parent))
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from qemu_platform import Guest,wait
from guest_user_probe import GuestUserProbe
import mkchfs

ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',type=pathlib.Path,required=True)
ap.add_argument('--mode',choices=['direct','uefi'],default='uefi')
ap.add_argument('--iso',type=pathlib.Path,default=pathlib.Path('build/chos-0.30.0.iso'))
ap.add_argument('--uefi-kernel',type=pathlib.Path,default=pathlib.Path('build/uefi-kernel/chos.elf'))
o=ap.parse_args();out=o.out.resolve();out.mkdir(parents=True,exist_ok=False)
disk=out/'disposable.img';shutil.copyfile('build/qemu-smoke-disk.img',disk)
entries=[e for e in mkchfs.active_dir(mkchfs.load_image(disk))[2] if e]
if not any(e['path']=='/DOCUMENTS' for e in entries):subprocess.run(['python3','tools/mkchfs.py','mkdir',str(disk),'/DOCUMENTS'],check=True)
document=out/'input.txt';document.write_text('abcdefghijklmnopqrstuvwxyz0123456789')
oversize=out/'large.txt';oversize.write_text('unmodified-large-file\n'*450)
binary=out/'binary.txt';binary.write_bytes(b'binary-prefix\x00must-remain-intact\n')
limit=out/'limit.txt';limit.write_text('q'*8191)
for path,source in [('/DOCUMENTS/REPEAT.TXT',document),('/DOCUMENTS/LARGE.TXT',oversize),('/DOCUMENTS/BINARY.TXT',binary),('/DOCUMENTS/LIMIT.TXT',limit),('/BIN/TEXTVIEW.CHX',pathlib.Path('build/userland/text_viewer.chcx')),('/BIN/BROWSER.CHX',pathlib.Path('build/userland/browser.chcx')),('/BIN/CONSOLE.CHX',pathlib.Path('build/userland/console.chcx'))]:
 subprocess.run(['python3','tools/mkchfs.py','put',str(disk),path,str(source)],check=True)
a=argparse.Namespace(mode=o.mode,kernel=pathlib.Path('build/chos.elf'),uefi_kernel=o.uefi_kernel,iso=o.iso,memory='2G' if o.mode=='uefi' else '512M',qemu_data=pathlib.Path('/workspace/chos-tools/root/usr/share/qemu'),rtc_base=None,trace=False,firmware=pathlib.Path('/workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd'),no_usb=False,usb_pci_bus=0,xhci_model='qemu-xhci',pci_virtio=True,net_model='e1000e',storage_error=False,nvme=False,no_rng=False,hub_depth=0,empty_usb=False,tablet=True)
g=Guest(a,out,disk);probe=GuestUserProbe(g)
console=pathlib.Path('build/userland/console.elf');browser=pathlib.Path('build/userland/browser.elf');editor=pathlib.Path('build/userland/text_viewer.elf')
for app in [console,browser,editor]:probe.app_symbols(app)
checks=[]
def key(k):
 # A10ms pulse can fall entirely between physical USB report polls. Keep the
 # acceptance tap down across multiple polls, including under software-rendered
 # frame load; held-key timing uses the separate explicit down/up helper.
 g.hmp('sendkey '+k+' 50');time.sleep(.08)
def state(k,down):g.qmp('input-send-event',events=[{'type':'key','data':{'down':down,'key':{'type':'qcode','data':k}}}])
def foreground(title):
 c=probe.foreground()
 return c if c and c['title'].startswith(title) and 0<c['pid']<0xffffffff else None
def app_text(pid,app,name,size=128):return probe.text(pid,app,name,size)
def delete(pid,app,name,size=128):
 before=len(app_text(pid,app,name,size));assert before>12
 def snapshot():
  # Freeze only the debugger read, so native state/counters and app text describe
  # the same instant. QEMU's guest virtual clock is stopped during this read.
  g.qmp('stop')
  try:
   return {'key':int.from_bytes(g.block('repeat_key',size=2),'little'),
    'counter':int.from_bytes(g.block('repeat_last_counter',size=8),'little'),
    'delay':int.from_bytes(g.block('repeat_delay_counter',size=8),'little'),
    'frequency':int.from_bytes(g.block('counter_frequency',size=8),'little'),
    'length':len(app_text(pid,app,name,size))}
  finally:g.qmp('cont')
 started=time.monotonic();state('backspace',True);initial=None
 def accepted():
  nonlocal initial
  value=snapshot()
  if value['key']==14 and value['frequency'] and value['delay']==max(1,value['frequency']//2):
   initial=value;return True
  return False
 wait(accepted,3,'hardware Backspace accepted with initial500ms deadline')
 assert before-1<=initial['length']<=before,initial
 first=initial if initial['length']==before-1 else None
 def consumed():
  nonlocal first
  value=snapshot();assert value['key']==14,value
  assert value['delay']==initial['delay'] and value['counter']==initial['counter'],('initial event was not consumed before repeat',initial,value)
  assert before-1<=value['length']<=before,value
  if value['length']<before:first=value;return True
  return False
 # Do not treat an event still in transit as an early-repeat failure. The host
 # GUI tests separately check490/500ms; live guest counters retain the deadline
 # assertion above even if software rendering delays the first visible update.
 if first is None:wait(consumed,3,'native app consumes the initial Backspace')
 # Six compositor pumps can exceed3s under software-emulated, uncached GOP.
 # Keep the actual guest500ms boundary assertion independent of host throughput.
 wait(lambda:len(app_text(pid,app,name,size))<=before-7,6,'six native repeat deletions after the initial delay')
 repeated=snapshot()
 assert repeated['delay']==max(1,initial['frequency']//25) and repeated['counter']-initial['counter']>=initial['delay'],('premature guest repeat',initial,repeated)
 debug={'field':name,'before':before,'first_observed':first,'initial':initial,
  'held':len(app_text(pid,app,name,size)),'elapsed':time.monotonic()-started,
  'view':g.symbol('view'),'repeat_view':g.symbol('repeat_view')}
 with (out/'repeat-debug.jsonl').open('a') as f:f.write(json.dumps(debug)+'\n')
 state('backspace',False)
 wait(lambda:int.from_bytes(g.block('repeat_key',size=2),'little')==0,3,'hardware release cancels repeat')
 def drained():
  client=foreground('CHOS')
  content=app_text(pid,app,name,size)
  # Caret text describes what is shown: Console includes its prompt, and the
  # focused address field displays at most its last30 characters.
  shown=min(40,len(content)+6) if name=='input_line' else min(30,len(content)) if name=='address' else len(content.rsplit(b'\n',1)[-1])
  return probe.queued_events(pid)==0 and client and client['pid']==pid and client['caret']['text_length']==shown
 wait(drained,3,'native app drains legitimate pre-release events and updates its caret')
 after=len(app_text(pid,app,name,size));time.sleep(.25)
 assert len(app_text(pid,app,name,size))==after,'key release must stop repeat';return before,after
def crop_caret(name,window,caret,scale):
 g.screen(name,full=True);data=(out/(name+'.ppm')).read_bytes();header=data.split(b'\n',3)
 width,height=map(int,header[1].split());pixels=header[3];assert len(pixels)==width*height*3
 x=window['x']+(6+caret['x'])*scale;y=window['y']+(34+caret['y'])*scale
 x0=max(0,x-1);x1=min(width,x+caret['width']*scale+2);y0=max(0,y-1);y1=min(height,y+caret['height']*scale+2)
 assert x1>x0 and y1>y0
 return b''.join(pixels[(row*width+x0)*3:(row*width+x1)*3] for row in range(y0,y1))
def blink(label,logical_width):
 def presented(value):
  return g.symbol('text_caret_visible')==value and g.symbol('presented_generation')==g.symbol('dirty_generation')
 wait(lambda:presented(1),3,label+' presented blink on')
 client=probe.foreground();assert client and client['caret']['enabled'],client
 window=g.window();scale=window['width']//logical_width;assert scale in [1,2,3,4]
 on=crop_caret(label+'-on',window,client['caret'],scale)
 wait(lambda:presented(0),3,label+' presented blink off')
 assert probe.foreground()['caret']==client['caret'],'typing fixture must await the completed app caret update'
 off=crop_caret(label+'-off',window,client['caret'],scale)
 assert on!=off,label+' caret pixels must actually blink'
def spawn(path,args,title):
 before={c['pid'] for c in probe.clients()}
 g.serial('spawn '+path+(' '+args if args else ''))
 wait(lambda:(c:=foreground(title)) and c['pid'] not in before,8,'isolated '+title)
 return foreground(title)['pid']
try:
 wait(lambda:'chos> ' in g.text(),45,'scheduled UART shell')
 if o.mode=='uefi':assert 'UEFI CPU isolation active' in g.text()
 wait(lambda:g.symbol('splash_active')==0,10,'splash')
 key('i');assert g.symbol('view')==0,'typing I must not open Setup'
 key('c')
 if o.mode=='uefi':
  wait(lambda:g.symbol('view')==1,6,'trusted desktop Console')
  for c in 'abcdefghijklmnopqrstuvwxyz':key(c)
  wait(lambda:len(g.block('terminal_command',size=128).split(b'\0')[0])==26,3,'trusted Console typing')
  def builtin_text():return g.block('terminal_command',size=128).split(b'\0')[0]
  state('backspace',True);time.sleep(.22);assert len(builtin_text())==25
  wait(lambda:len(builtin_text())<=19,6,'trusted Console held Backspace');state('backspace',False)
  after=len(builtin_text());time.sleep(.25);assert len(builtin_text())==after
  wait(lambda:g.symbol('text_caret_visible')==1 and g.symbol('presented_generation')==g.symbol('dirty_generation'),3,'trusted Console presented caret on')
  on=g.screen('trusted-console-on')
  wait(lambda:g.symbol('text_caret_visible')==0 and g.symbol('presented_generation')==g.symbol('dirty_generation'),3,'trusted Console presented caret off')
  off=g.screen('trusted-console-off');assert on!=off,'trusted Console must visibly blink'
  key('esc');wait(lambda:g.symbol('view')==0,5,'trusted Console closes')
  spawn('/BIN/CONSOLE.CHX','','CHOS Console')
  checks+=['trusted UEFI desktop Console retains its diagnostic path and held Backspace/blinking caret']
 wait(lambda:foreground('CHOS Console'),6,'isolated Console')
 console_pid=foreground('CHOS Console')['pid']
 g.type('netstatus')
 wait(lambda:b'DHCP: bound' in probe.block(console_pid,console,'output_lines',11*73),6,'native Console reads actual bound Ethernet status')
 assert b'Internet reachability is unverified' in probe.block(console_pid,console,'output_lines',11*73)
 wait(lambda:probe.value(console_pid,console,'input_length',8)==0,3,'Console command input resets')
 for c in 'abcdefghijklmnopqrstuvwxyz':key(c)
 wait(lambda:len(app_text(console_pid,console,'input_line'))==26,3,'native Console consumes all characters')
 console_lengths=delete(console_pid,console,'input_line');blink('native-console',640)
 state('a',True)
 wait(lambda:app_text(console_pid,console,'input_line').endswith(b'aaa'),3,'printable repeat')
 state('a',False);time.sleep(.15)
 assert app_text(console_pid,console,'input_line').endswith(b'aaa')
 checks+=['plain I leaves Setup closed','native Console netstatus reads OS Ethernet/DHCP status without claiming Internet access','actual isolated Console held Backspace/letters and visible caret pixels']
 browser_pid=spawn('/BIN/BROWSER.CHX','','CHOS Web Browser')
 assert not g.symbol('kernel_browser_active'),'must exercise isolated browser, not recovery singleton'
 for c in 'abcdefghijklmnopqrstuvwxyz0123456789':key(c)
 wait(lambda:len(app_text(browser_pid,browser,'address',512))==36,3,'native browser address')
 browser_lengths=delete(browser_pid,browser,'address',512);blink('native-browser',760)
 key('esc');wait(lambda:foreground('CHOS Console'),6,'Console focus restored after browser close')
 checks+=['actual isolated HTTP recovery browser address repeat and visible caret (modern-engine gate remains open)']
 state('backspace',True);time.sleep(.05)
 editor_pid=spawn('/BIN/TEXTVIEW.CHX','/DOCUMENTS/REPEAT.TXT','CHOS Text Editor')
 time.sleep(.65);assert app_text(editor_pid,editor,'document',8192)==document.read_bytes(),'focus change must cancel held deletion'
 state('backspace',False)
 document_lengths=delete(editor_pid,editor,'document',8192);blink('native-editor',640)
 old_caret_y=probe.foreground()['caret']['y']
 key('ret');key('a')
 wait(lambda:app_text(editor_pid,editor,'document',8192).endswith(b'\na') and
  (c:=probe.foreground()) and c['caret']['text_length']==1 and c['caret']['y']==old_caret_y+16,
  4,'native editor completes newline text and caret messages')
 blink('native-editor-newline',640)
 expected=app_text(editor_pid,editor,'document',8192);assert expected.endswith(b'\na')
 key('ctrl-s');wait(lambda:probe.value(editor_pid,editor,'modified')==0,5,'native editor durable save')
 assert probe.value(editor_pid,editor,'save_error')==0
 key('esc');wait(lambda:foreground('CHOS Console'),6,'editor close')
 editor_pid=spawn('/BIN/TEXTVIEW.CHX','/DOCUMENTS/REPEAT.TXT','CHOS Text Editor')
 assert app_text(editor_pid,editor,'document',8192)==expected,'save must survive actual reopen'
 state('backspace',True);time.sleep(.62)
 if o.mode=='uefi':
  unplug_start=time.monotonic();g.qmp('device_del',id='kbd')
  # Removal is asynchronous. First require the actual kernel key release, then
  # drain legitimate events queued before removal. A fixed wall-clock sleep can
  # snapshot the document before its last queued event under host CPU load.
  wait(lambda:int.from_bytes(g.block('repeat_key',size=2),'little')==0,4,'USB removal cancels actual kernel repeat')
  wait(lambda:probe.queued_events(editor_pid)==0,4,'editor drains pre-removal key events')
  switches=g.symbol('preemption_switches')
  wait(lambda:g.symbol('preemption_switches')>=switches+8,4,'guest dispatch progresses past consumed input')
  wait(lambda:probe.queued_events(editor_pid)==0 and
       (c:=foreground('CHOS Text Editor')) and c['caret']['text_length']==len(app_text(editor_pid,editor,'document',8192).rsplit(b'\n',1)[-1]),
       4,'editor presents the drained document caret')
  stopped=app_text(editor_pid,editor,'document',8192)
  (out/'unplug-drain.json').write_text(json.dumps({'seconds_until_drained':time.monotonic()-unplug_start,'document_length':len(stopped),'repeat_key':0,'queued_events':probe.queued_events(editor_pid),'dispatches_after_empty_queue':g.symbol('preemption_switches')-switches},indent=2))
  time.sleep(.6);assert int.from_bytes(g.block('repeat_key',size=2),'little')==0 and probe.queued_events(editor_pid)==0 and app_text(editor_pid,editor,'document',8192)==stopped,'USB removal must release repeat after queued input drains'
  g.qmp('device_add',driver='usb-kbd',bus='xhci.0',id='kbd2');time.sleep(.7)
 else:state('backspace',False)
 expected=app_text(editor_pid,editor,'document',8192)
 key('esc');wait(lambda:foreground('CHOS Console'),6,'auto-save close')
 checks+=['actual native editor append/backspace, focus cancellation, newline caret, Ctrl+S and reopen persistence','native editor auto-saves on Escape; USB keyboard removal releases held key' if o.mode=='uefi' else 'native editor auto-saves on Escape']
 large_pid=spawn('/BIN/TEXTVIEW.CHX','/DOCUMENTS/LARGE.TXT','CHOS Text Viewer')
 assert probe.value(large_pid,editor,'truncated')==1 and probe.value(large_pid,editor,'editable')==0
 before=app_text(large_pid,editor,'document',8192);key('a');state('backspace',True);time.sleep(.7);state('backspace',False)
 assert app_text(large_pid,editor,'document',8192)==before;key('ctrl-s');key('esc')
 wait(lambda:foreground('CHOS Console'),6,'read-only preview close')
 checks+=['oversized text remains read-only; typing/Backspace/save cannot truncate original']
 binary_pid=spawn('/BIN/TEXTVIEW.CHX','/DOCUMENTS/BINARY.TXT','CHOS Text Viewer')
 assert probe.value(binary_pid,editor,'editable')==0
 before=probe.block(binary_pid,editor,'document',8192);key('a');key('backspace');key('ctrl-s')
 assert probe.block(binary_pid,editor,'document',8192)==before;key('esc')
 wait(lambda:foreground('CHOS Console'),6,'binary text preview close')
 limit_pid=spawn('/BIN/TEXTVIEW.CHX','/DOCUMENTS/LIMIT.TXT','CHOS Text Editor')
 assert probe.value(limit_pid,editor,'document_length',8)==8191
 key('a');time.sleep(.25);assert probe.value(limit_pid,editor,'document_length',8)==8191 and not probe.value(limit_pid,editor,'modified')
 key('backspace');wait(lambda:probe.value(limit_pid,editor,'document_length',8)==8190,3,'bounded editor can remove at capacity')
 key('a');wait(lambda:probe.value(limit_pid,editor,'document_length',8)==8191,3,'bounded editor can refill capacity')
 key('esc');wait(lambda:foreground('CHOS Console'),6,'capacity editor save and close')
 checks+=['embedded-NUL text remains read-only; exact-capacity edits are bounded and durable']
 assert '[FATAL]' not in g.text() and '[USER FAULT]' not in g.text()
 g.screen('native-typing-final');g.close();g=None
 for name,path,expected_data in [('saved','/DOCUMENTS/REPEAT.TXT',expected),('oversized','/DOCUMENTS/LARGE.TXT',oversize.read_bytes()),('binary','/DOCUMENTS/BINARY.TXT',binary.read_bytes()),('capacity','/DOCUMENTS/LIMIT.TXT',b'q'*8190+b'a')]:
  target=out/(name+'-extracted.txt');subprocess.run(['python3','tools/mkchfs.py','read',str(disk),path,str(target)],check=True)
  assert target.read_bytes()==expected_data,('actual durable file differs',name)
 (out/'result.json').write_text(json.dumps({'status':'PASS','mode':o.mode,'ordinary_tap_down_ms':50,'console_lengths':console_lengths,'browser_lengths':browser_lengths,'document_lengths':document_lengths,'editor_sha256':hashlib.sha256((out/'text_viewer.app.elf').read_bytes()).hexdigest(),'checks':checks},indent=2))
except Exception:
 if g and g.process.poll() is None:
  g.screen('failure')
  diagnostic={'view':g.symbol('view'),'repeat_key':int.from_bytes(g.block('repeat_key',size=2),'little'),
   'control_down':g.symbol('control_down'),'clients':probe.clients(),'registers':g.hmp('info registers')}
  stride,members=probe.layout('gui_app_client_t')
  diagnostic['queues']=[int.from_bytes(g.block('gui_app_clients',off+members['event_count'],size=8),'little') for off in range(0,probe.symbol_sizes['gui_app_clients'],stride)]
  (out/'failure-diagnostic.json').write_text(json.dumps(diagnostic,indent=2))
 raise
finally:
 if g:g.close()

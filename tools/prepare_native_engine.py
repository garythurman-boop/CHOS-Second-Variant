#!/usr/bin/env python3
"""Stage an explicit diagnostic native engine ELF; never changes normal media."""
import argparse,hashlib,json,pathlib,subprocess,struct,tempfile,os
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--application',type=pathlib.Path,required=True)
p.add_argument('--out',type=pathlib.Path,default=root/'build/engine-test')
p.add_argument('--strip-tool',default='llvm-strip');p.add_argument('--cc',default='cc')
o=p.parse_args();app=o.application.resolve();out=o.out.resolve()
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def segments(p):
 data=p.read_bytes()
 assert data[:7]==b'\x7fELF\x02\x01\x01' and len(data)>=64
 offset=struct.unpack_from('<Q',data,32)[0];count=struct.unpack_from('<H',data,56)[0]
 assert struct.unpack_from('<H',data,54)[0]==56 and 0<count<=16 and offset+count*56<=len(data)
 values=[]
 for i in range(count):
  kind,flags,start,va,pa,files,memory,align=struct.unpack_from('<IIQQQQQQ',data,offset+i*56)
  assert start<=len(data) and files<=len(data)-start
  if kind in [1,4]:values.append((kind,flags,va,pa,memory,align,data[start:start+files]))
 return values
assert app.is_file() and app.stat().st_size<=64*1024*1024+4096
out.mkdir(parents=True,exist_ok=True);destination=out/'native-engine.elf';manifest=out/'native-engine.json'
for path in [destination,manifest]:assert not path.is_symlink()
if destination.exists() or manifest.exists():
 assert destination.is_file() and manifest.is_file(),'Refusing unowned diagnostic stage'
 previous=json.loads(manifest.read_text());assert previous['schema']==1 and sha(destination)==previous['staged_sha256'],'Managed diagnostic bytes changed'
with tempfile.TemporaryDirectory(prefix='native-engine-',dir=out) as temp:
 temp=pathlib.Path(temp);checker=temp/'validate-native-elf';stripped=temp/'native-engine.elf'
 command=[o.cc,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(root/'include'),str(root/'tools/validate_native_elf.c'),str(root/'kernel/core/elf_image.c'),str(root/'kernel/core/process_format.c'),'-o',str(checker)]
 subprocess.run(command,check=True)
 original=json.loads(subprocess.check_output([str(checker),str(app),'1'],text=True))
 subprocess.run([o.strip_tool,'--strip-all','-o',str(stripped),str(app)],check=True)
 policy=json.loads(subprocess.check_output([str(checker),str(stripped),'1'],text=True))
 assert segments(app)==segments(stripped),'Strip changed native load or ABI note bytes'
 record={'schema':1,'source':str(app),'source_sha256':sha(app),'source_bytes':app.stat().st_size,'staged_sha256':sha(stripped),'staged_bytes':stripped.stat().st_size,'original_policy':original,'staged_policy':policy,'load_and_note_segments_identical':True,'executed':False,'limits':['Diagnostic ROMFS fixture only;normal images unchanged;no installer payload in diagnostic ISO','Successful staging is not VM/browser execution or Stage3 acceptance']}
 if not destination.exists() or sha(destination)!=record['staged_sha256']:os.replace(stripped,destination)
 stage_manifest=temp/'manifest.json';stage_manifest.write_text(json.dumps(record,indent=2)+'\n');os.replace(stage_manifest,manifest)
 print(json.dumps(record,indent=2))

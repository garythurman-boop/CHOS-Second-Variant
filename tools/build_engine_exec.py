#!/usr/bin/env python3
"""Build an explicit native engine-profile resource fixture, not a browser."""
import argparse,hashlib,json,pathlib,shutil,subprocess,struct
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--native-sdk',type=pathlib.Path,required=True)
p.add_argument('--out',type=pathlib.Path,required=True)
p.add_argument('--cmake',default='cmake');p.add_argument('--ninja',default='ninja')
o=p.parse_args();sdk=o.native_sdk.resolve();out=o.out.resolve();out.mkdir(parents=True,exist_ok=False)
subprocess.run(['python3',str(root/'tools/verify_native_sdk.py'),str(sdk)],check=True)
cmds=[[o.cmake,'-S',str(root/'tests/engine_exec'),'-B',str(out/'cmake'),'-G','Ninja','-DCMAKE_MAKE_PROGRAM='+str(pathlib.Path(shutil.which(o.ninja)).resolve()),'-DCMAKE_TOOLCHAIN_FILE='+str(root/'ports/native/chos-toolchain.cmake'),'-DCHOS_SDK_ROOT='+str(sdk),'-DCHOS_APPLICATION_PROFILE=engine','-DCMAKE_BUILD_TYPE=Release'],[o.cmake,'--build',str(out/'cmake'),'-j2']]
code=0
for i,cmd in enumerate(cmds):
 with (out/('configure.log' if i==0 else 'build.log')).open('wb') as f:code=subprocess.run(cmd,stdout=f,stderr=subprocess.STDOUT).returncode
 if code:break
if not code:
 app=out/'cmake/engine_exec';raw=app.read_bytes()
 assert len(raw)<4*1024*1024 and not subprocess.check_output(['llvm-nm','-u',str(app)],text=True).strip()
 phoff=struct.unpack_from('<Q',raw,32)[0];count=struct.unpack_from('<H',raw,56)[0];notes=[];segments=[]
 for n in range(count):
  at=phoff+n*56;t,flags,offset,va,pa,files,memory,align=struct.unpack_from('<IIQQQQQQ',raw,at)
  if t==4:notes.append((at,offset,files))
  if t==1:segments.append({'va':va,'file_bytes':files,'memory_bytes':memory,'flags':flags});assert flags&3!=3
 assert len(notes)==1;at,offset,size=notes[0];assert size==28 and raw[offset+20:offset+28]==struct.pack('<II',1,1)
 span=max(v['va']+v['memory_bytes'] for v in segments)-0x20000000;assert 48*1024*1024<span<=64*1024*1024
 for name,value in [('unknown-profile',2),('ambiguous-profile',0)]:
  bad=bytearray(raw);struct.pack_into('<I',bad,offset+24,value);(out/(name+'.elf')).write_bytes(bad)
 bad=bytearray(raw);struct.pack_into('<I',bad,offset+4,4);struct.pack_into('<Q',bad,at+32,24);(out/'legacy-large.elf').write_bytes(bad)
 report={'status':'BUILT_NOT_EXECUTED','stage3_complete':False,'application_profile':'engine','file_bytes':len(raw),'virtual_span_bytes':span,'segments':segments,'sdk_manifest_sha256':hashlib.sha256((sdk/'chos-sdk.json').read_bytes()).hexdigest(),'application_sha256':hashlib.sha256(raw).hexdigest(),'commands':cmds,'limits':['native resource fixture only;no VM/JavaScript/browser or hardware qualification']}
else:report={'status':'BUILD_FAILED','stage3_complete':False,'returncode':code,'commands':cmds}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));raise SystemExit(code)

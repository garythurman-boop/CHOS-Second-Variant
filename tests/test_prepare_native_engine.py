#!/usr/bin/env python3
"""Real loader-policy and managed-stage refusal tests; no target execution."""
import argparse,pathlib,subprocess,tempfile,json,hashlib
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--application',type=pathlib.Path,required=True);p.add_argument('--bad-application',type=pathlib.Path,required=True);o=p.parse_args()
with tempfile.TemporaryDirectory() as directory:
 base=pathlib.Path(directory)
 def run(app,out,good):
  r=subprocess.run(['python3',str(root/'tools/prepare_native_engine.py'),'--application',str(app),'--out',str(out)],capture_output=True,text=True)
  assert (r.returncode==0)==good,(r.returncode,r.stdout,r.stderr)
  return r
 target=base/'managed';run(o.application,target,True);binary=target/'native-engine.elf';manifest=target/'native-engine.json'
 before=binary.read_bytes();stamp=binary.stat().st_mtime_ns
 record=json.loads(manifest.read_text());assert record['load_and_note_segments_identical'] and record['staged_policy']['resource_profile']==1
 assert record['staged_sha256']==hashlib.sha256(before).hexdigest()
 run(o.application,target,True);assert binary.read_bytes()==before and binary.stat().st_mtime_ns==stamp
 run(o.bad_application,target,False);assert binary.read_bytes()==before
 unowned=base/'unowned';unowned.mkdir();(unowned/'native-engine.elf').write_bytes(b'user-data')
 run(o.application,unowned,False);assert (unowned/'native-engine.elf').read_bytes()==b'user-data'
 binary.write_bytes(before+b'modified');run(o.application,target,False);assert binary.read_bytes()==before+b'modified'
print('Native engine staging: real policy/segment equality,idempotent bytes/mtime and invalid/unowned/modified refusal passed')

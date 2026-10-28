#!/usr/bin/env python3
"""Import original pinned ICU i18n sources from an existing verified Git clone."""
import argparse,hashlib,json,pathlib,subprocess
root=pathlib.Path(__file__).resolve().parents[1]
commit='ddc6b66926bfac6036144ca9902992a0d6d50af9'
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--repository',type=pathlib.Path,required=True);o=p.parse_args()
out=root/'third_party/icu-i18n'
if out.exists():p.error('refusing an existing output')
# Read only the selected checkout files, verifying their Git blob IDs against
# the pinned commit tree. Avoid fetching unrelated blobs in a sparse clone.
entries=subprocess.check_output(['git','-C',str(o.repository),'ls-tree','-rz',commit,'--','LICENSE','icu4c/source/i18n'])
files={}
for entry in entries.split(b'\0'):
 if not entry:continue
 metadata,path=entry.split(b'\t',1);mode,kind,blob=metadata.split();name=path.decode()
 if kind!=b'blob':continue
 if name=='LICENSE':relative='LICENSE'
 elif name.startswith(('icu4c/source/i18n/',)):
  relative=name.removeprefix('icu4c/')
  if pathlib.Path(name).suffix not in ('.h','.cpp','.c','.inc') and not name.endswith('/sources.txt'):continue
 else:continue
 data=(o.repository/name).read_bytes()
 assert hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()==blob.decode(),'checkout differs from pinned blob: '+name
 target=out/relative;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
 files[relative]=hashlib.sha256(data).hexdigest()
sources=[n for n in (out/'source/i18n/sources.txt').read_text().split() if n.endswith('.cpp')]
assert len(sources)==254 and all((out/'source/i18n'/n).is_file() for n in sources)
manifest={'schema':1,'project':'ICU i18n library source closure','version':'79.1','commit':commit,'tag':'release-79.1','source':'https://github.com/unicode-org/icu','license':'Unicode License V3','scope':'Original i18n sources/public headers. A checked native decimal header removes only the unused signal include. Runtime and complete data qualification remain separate.','sources':sources,'files':dict(sorted(files.items()))}
(root/'docs/dependencies/icu-i18n.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Imported',len(files),'original ICU files and',len(sources),'i18n translation units')

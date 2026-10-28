#!/usr/bin/env python3
"""Extend selected public libc++ headers with original private include closure."""
import argparse, hashlib, json, pathlib, re, subprocess, tarfile
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--archive',type=pathlib.Path,required=True)
p.add_argument('--header',action='append',default=[])
p.add_argument('--source-file',action='append',default=[])
o=p.parse_args()
if not o.header and not o.source_file:p.error('select at least one original header/source')
manifest=root/'docs/dependencies/libcxx-native.json'
m=json.loads(manifest.read_text())
assert hashlib.sha256(o.archive.read_bytes()).hexdigest()==m['release_archive_sha256']['libcxx.tar.xz']
subprocess.run(['python3',str(root/'tools/verify_libcxx.py')],check=True)
generated={'__config_site','__assertion_handler','__external_threading'}
base=root/'third_party/libcxx';pending=['include/'+name for name in o.header]+['src/'+name for name in o.source_file];seen=set();added=[]
with tarfile.open(o.archive) as archive:
    members={member.name.removeprefix('libcxx-'+m['version']+'.src/'):member
             for member in archive if member.isfile()}
    while pending:
        name=pending.pop()
        assert not pathlib.PurePosixPath(name).is_absolute() and '..' not in pathlib.PurePosixPath(name).parts
        if name in seen:continue
        seen.add(name);assert name in members,'missing original header: '+name
        data=archive.extractfile(members[name]).read();target=base/name
        if target.exists():assert target.read_bytes()==data,'modified original header: '+name
        else:
            target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data);added.append(name)
            m['files'][name]=hashlib.sha256(data).hexdigest()
            if name.startswith('src/') and name.endswith('.cpp'):m['sources'].append(name)
        for include in re.findall(rb'^\s*#\s*include\s*<([^>]+)>',data,re.M):
            dependency=include.decode();path='include/'+dependency
            if dependency in generated:continue
            # Public C++ headers already selected are traversed. New public
            # facilities require an explicit --header; no C wrapper shadows
            # the existing native libc as an incidental dependency import.
            if dependency.startswith('__') or path in m['files']:
                assert path in members,'missing private header: '+path
                pending.append(path)
manifest.write_text(json.dumps(m,indent=2)+'\n')
print('Imported',len(added),'original libc++ files:',', '.join(sorted(added)))

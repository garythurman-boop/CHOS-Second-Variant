#!/usr/bin/env python3
"""Check complete pinned original codec and independent test-reference sources."""
import pathlib,json,hashlib
root=pathlib.Path(__file__).resolve().parents[1]
m=json.loads((root/'docs/dependencies/native-png.json').read_text())
for p in [*m['projects'],m['host_reference']]:
 base=root/p['root'];entries=list(base.rglob('*'))
 assert not base.is_symlink() and not any(f.is_symlink() for f in entries),'Codec symlink: '+p['name']
 actual={str(f.relative_to(base)) for f in entries if f.is_file()}
 assert actual==set(p['files']),'Codec inventory changed: '+p['name']
 for name,wanted in p['files'].items():
  file=base/name;assert not file.is_symlink() and file.resolve().is_relative_to(base)
  assert hashlib.sha256(file.read_bytes()).hexdigest()==wanted,name
 print(p['name'],len(actual),'original files verified')
for name,wanted in m['authentication_material'].items():
 file=root/name;assert not file.is_symlink() and file.resolve().is_relative_to(root)
 assert hashlib.sha256(file.read_bytes()).hexdigest()==wanted,name
print('Original dependency signatures,public keys and local verification transcripts verified')

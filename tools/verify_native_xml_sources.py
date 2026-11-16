#!/usr/bin/env python3
"""Verify the original libxml2 implementation, headers, test inputs and licenses."""
from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parents[1]
m=json.loads((root/'docs/dependencies/native-xml.json').read_text());base=root/m['root'];entries=list(base.rglob('*'))
assert not base.is_symlink() and not any(p.is_symlink() for p in entries)
assert {str(p.relative_to(base)) for p in entries if p.is_file()}==set(m['files']),'Original XML inventory changed'
for name,want in m['files'].items():assert hashlib.sha256((base/name).read_bytes()).hexdigest()==want,name
print('libxml2',m['version'],len(m['files']),'original files verified')
for name,want in m.get('authentication_material',{}).items():
 p=root/name;assert not p.is_symlink() and p.resolve().is_relative_to(root)
 assert hashlib.sha256(p.read_bytes()).hexdigest()==want,name
print('XML source authentication receipts verified')

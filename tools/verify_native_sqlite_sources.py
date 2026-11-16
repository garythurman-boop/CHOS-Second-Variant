#!/usr/bin/env python3
"""Verify the original authenticated SQLite amalgamation inventory."""
import pathlib,json,hashlib
root=pathlib.Path(__file__).resolve().parents[1]
m=json.loads((root/'docs/dependencies/native-sqlite.json').read_text());base=root/m['root'];entries=list(base.rglob('*'))
assert not base.is_symlink() and not any(p.is_symlink() for p in entries)
assert {str(p.relative_to(base)) for p in entries if p.is_file()}==set(m['files'])
for name,want in m['files'].items():assert hashlib.sha256((base/name).read_bytes()).hexdigest()==want,name
print('SQLite',m['version'],len(m['files']),'original amalgamation files verified')

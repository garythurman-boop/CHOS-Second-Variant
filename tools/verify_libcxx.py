#!/usr/bin/env python3
"""Verify the original libc++ closure and explicit native source set offline."""
from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parents[1]
base=root/'third_party/libcxx'
m=json.loads((root/'docs/dependencies/libcxx-native.json').read_text())
actual={str(p.relative_to(base)) for p in base.rglob('*') if p.is_file()}
assert actual==set(m['files']),'libc++ closure membership changed'
for name,wanted in m['files'].items():
 path=base/name
 assert path.is_relative_to(base) and '..' not in path.parts
 assert hashlib.sha256(path.read_bytes()).hexdigest()==wanted,'modified original libc++ file '+name
assert set(m['sources'])=={p for p in actual if p.endswith('.cpp')},'libc++ source set changed'
print('LLVM libc++ '+m['version']+': '+str(len(actual))+' original files verified')

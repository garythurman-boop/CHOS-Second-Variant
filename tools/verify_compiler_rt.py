#!/usr/bin/env python3
"""Verify the exact original LLVM emutls dependency closure, without networking."""
from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parents[1]
base=root/'third_party/compiler-rt'
manifest=json.loads((root/'docs/dependencies/compiler-rt-emutls.json').read_text())
actual={str(p.relative_to(base)) for p in base.rglob('*') if p.is_file()}
assert actual==set(manifest['files']),'compiler-rt dependency closure changed'
for name,wanted in manifest['files'].items():
    path=base/name
    assert path.is_relative_to(base) and '..' not in path.parts
    assert hashlib.sha256(path.read_bytes()).hexdigest()==wanted,'modified upstream file '+name
print('LLVM compiler-rt '+manifest['version']+': '+str(len(actual))+' pinned original files verified')

#!/usr/bin/env python3
"""Verify the exact original LLVM emutls/binary128 dependency closures, without networking."""
from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parents[1]
base=root/'third_party/compiler-rt'
manifest=json.loads((root/'docs/dependencies/compiler-rt-emutls.json').read_text())
quad=json.loads((root/'docs/dependencies/compiler-rt-quad.json').read_text())
for name,wanted in quad['files'].items():
    if name in manifest['files']:assert manifest['files'][name]==wanted
    manifest['files'][name]=wanted
actual={str(p.relative_to(base)) for p in base.rglob('*') if p.is_file()}
assert 'lib/builtins/aarch64/fp_mode.c' in quad['sources']
assert 'lib/builtins/fp_mode.c' not in actual,'generic dummy FP mode backend is forbidden'
assert len(quad['sources'])==len(set(quad['sources']))
assert set(quad['sources'])<=actual
assert actual==set(manifest['files']),'compiler-rt dependency closure changed'
for name,wanted in manifest['files'].items():
    path=base/name
    assert path.is_relative_to(base) and '..' not in path.parts
    assert hashlib.sha256(path.read_bytes()).hexdigest()==wanted,'modified upstream file '+name
print('LLVM compiler-rt emutls/binary12819.1.7 with upstream20.1.8 carry header: '+str(len(actual))+' pinned original files verified')

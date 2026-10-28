#!/usr/bin/env python3
"""Verify pinned original math source bytes and the selected hardware backend."""
from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parents[1];base=root/'third_party/musl'
manifest=json.loads((root/'docs/dependencies/musl-math.json').read_text())
scan=json.loads((root/'docs/dependencies/musl-floatscan.json').read_text())
for name,wanted in scan['files'].items():
    if name in manifest['files']:assert manifest['files'][name]==wanted
    manifest['files'][name]=wanted
wide=json.loads((root/'docs/dependencies/musl-wide.json').read_text())
for name,wanted in wide['files'].items():
    if name in manifest['files']:assert manifest['files'][name]==wanted
    manifest['files'][name]=wanted
formatting=json.loads((root/'docs/dependencies/musl-format.json').read_text())
for name,wanted in formatting['files'].items():
    if name in manifest['files']:assert manifest['files'][name]==wanted
    manifest['files'][name]=wanted
actual={str(p.relative_to(base)) for p in base.rglob('*') if p.is_file()}
assert actual==set(manifest['files']),'musl math closure changed'
for name,wanted in manifest['files'].items():
    p=base/name
    assert p.is_relative_to(base) and '..' not in p.parts
    assert hashlib.sha256(p.read_bytes()).hexdigest()==wanted,'modified upstream file '+name
assert 'src/fenv/aarch64/fenv.s' in manifest['sources']
assert 'src/fenv/fenv.c' not in actual,'generic dummy fenv must not be shipped'
assert len(manifest['sources'])==len(set(manifest['sources']))
assert set(manifest['sources'])<=actual and set(manifest['host_sources'])<=actual
print('musl '+manifest['version']+': '+str(len(actual))+' original files verified; real ARM64 fenv selected')

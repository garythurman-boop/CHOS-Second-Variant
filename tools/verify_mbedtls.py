#!/usr/bin/env python3
"""Verify exact pinned upstream crypto bytes; no network or generated trust."""
from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parents[1];base=root/'third_party/mbedtls'
m=json.loads((root/'docs/dependencies/mbedtls.json').read_text())
actual={str(p.relative_to(base)) for p in base.rglob('*') if p.is_file()}
assert actual==set(m['files']),'MbedTLS source closure changed'
for name,wanted in m['files'].items():
 p=base/name;assert p.is_relative_to(base) and '..' not in p.parts
 assert hashlib.sha256(p.read_bytes()).hexdigest()==wanted,'modified upstream file '+name
print('MbedTLS '+m['version']+': '+str(len(actual))+' pinned original files verified')

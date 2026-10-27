#!/usr/bin/env python3
"""Verify pinned ICU component bytes offline; not a full ICU library build."""
from pathlib import Path
import hashlib,json
r=Path(__file__).resolve().parents[1];base=r/'third_party/icu-threading'
m=json.loads((r/'docs/dependencies/icu-threading.json').read_text())
actual={str(p.relative_to(base)) for p in base.rglob('*') if p.is_file()}
assert actual==set(m['files']),'ICU threading closure membership changed'
for name,wanted in m['files'].items():
 p=base/name
 assert '..' not in p.parts and p.is_relative_to(base)
 assert hashlib.sha256(p.read_bytes()).hexdigest()==wanted,'modified original ICU file '+name
print('ICU '+m['version']+' threading component: '+str(len(actual))+' original files verified')

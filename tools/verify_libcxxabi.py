#!/usr/bin/env python3
"""Verify original RTTI dependency bytes offline, not exception-runtime support."""
from pathlib import Path
import json,hashlib
r=Path(__file__).resolve().parents[1];base=r/'third_party/libcxxabi';m=json.loads((r/'docs/dependencies/libcxxabi-rtti.json').read_text())
assert {str(p.relative_to(base)) for p in base.rglob('*') if p.is_file()}==set(m['files'])
for n,w in m['files'].items():
 p=base/n;assert '..' not in p.parts and not Path(n).is_absolute();assert hashlib.sha256(p.read_bytes()).hexdigest()==w,n
print('LLVM libc++abi RTTI: '+str(len(m['files']))+' original files verified')

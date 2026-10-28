#!/usr/bin/env python3
"""Verify the original ICU common closure; no native platform patch is hidden here."""
import hashlib,json,pathlib
root=pathlib.Path(__file__).resolve().parents[1];m=json.loads((root/'docs/dependencies/icu-common.json').read_text());base=root/'third_party/icu-common'
actual={str(p.relative_to(base)) for p in base.rglob('*') if p.is_file()}
assert actual==set(m['files']),'ICU common closure membership changed'
for name,want in m['files'].items():
 assert hashlib.sha256((base/name).read_bytes()).hexdigest()==want,'modified original ICU file '+name
assert len(m['sources'])==202 and all('source/common/'+n in m['files'] for n in m['sources'])
print('ICU',m['version'],'common:',len(actual),'original files verified')

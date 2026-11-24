#!/usr/bin/env python3
"""Verify maintained original key-writer bytes without changing shared inventories."""
from pathlib import Path
import hashlib,json,subprocess
root=Path(__file__).resolve().parents[2]
subprocess.run(['python3',str(root/'tools/verify_mbedtls.py')],check=True)
m=json.loads((root/'docs/dependencies/native-webcore-keys.json').read_text())
base=root/m['root']
files=[p for p in base.rglob('*') if p.is_file()]
assert not base.is_symlink() and not any(p.is_symlink() for p in base.rglob('*'))
assert {str(p.relative_to(base)) for p in files}==set(m['files'])
for name,wanted in m['files'].items():
    data=(base/name).read_bytes()
    assert hashlib.sha256(data).hexdigest()==wanted,name
    assert hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()==m['git_blobs'][name],name
print('Auxiliary original MbedTLS key writer and license verified')

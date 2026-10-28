#!/usr/bin/env python3
"""Verify exact pinned resource bytes, headers and corpus versions."""
import hashlib, json, pathlib, struct
root = pathlib.Path(__file__).resolve().parents[1]
base = root/'third_party/icu-normalization-data'
m = json.loads((root/'docs/dependencies/icu-normalization-data.json').read_text())
assert {str(p.relative_to(base)) for p in base.rglob('*') if p.is_file()} == set(m['files'])
for name, record in m['files'].items():
    d = (base/name).read_bytes()
    assert len(d) == record['bytes'] and hashlib.sha256(d).hexdigest() == record['sha256'], name
    assert hashlib.sha1(b'blob '+str(len(d)).encode()+b'\0'+d).hexdigest() == record['git_blob'], name
    if name.endswith('.nrm'):
        assert d[2:4] == b'\xda\x27' and d[8:12] == b'\0\0\2\0', name
        assert d[12:16] == b'Nrm2' and d[16] == 5 and d[20:24] == b'\x12\0\0\0', name
        assert 24 <= struct.unpack_from('<H', d)[0] < len(d) < 65536, name
assert b'NormalizationTest-18.0.0.txt' in (base/'source/data/unidata/NormalizationTest.txt').read_bytes()[:100]
assert b'# Version: 18.0.0' in (base/'source/test/testdata/IdnaTestV2.txt').read_bytes()[:500]
print('Verified four original ICU little-endian resources and Unicode18 corpora')

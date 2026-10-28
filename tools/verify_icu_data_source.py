#!/usr/bin/env python3
"""Verify every original ICU source archive member; optionally extract safely."""
import argparse, hashlib, json, os, pathlib, stat, tarfile
root = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--extract', type=pathlib.Path)
o = p.parse_args()
m = json.loads((root/'docs/dependencies/icu-data-source.json').read_text())
record = m['archive']; packed = root/record['path']
assert packed.stat().st_size == record['bytes']
assert hashlib.sha256(packed.read_bytes()).hexdigest() == record['sha256']
if o.extract:
    o.extract.mkdir(parents=True, exist_ok=False)
seen = set(); license_bytes = None
with tarfile.open(packed, 'r:gz') as archive:
    for member in archive:
        name = pathlib.PurePosixPath(member.name)
        assert not name.is_absolute() and '..' not in name.parts
        if member.isdir(): continue
        assert member.isfile() and member.name in m['files'] and member.name not in seen
        r = m['files'][member.name]
        assert member.size == r['bytes'] and member.mode == r['mode']
        d = archive.extractfile(member).read()
        if member.name == 'LICENSE': license_bytes = d
        assert hashlib.sha256(d).hexdigest() == r['sha256'], member.name
        assert hashlib.sha1(b'blob '+str(len(d)).encode()+b'\0'+d).hexdigest() == r['git_blob'], member.name
        seen.add(member.name)
        if o.extract:
            target = o.extract/member.name
            target.parent.mkdir(parents=True, exist_ok=True)
            # Output is freshly owned, regular files only; no archive links.
            with target.open('xb') as f: f.write(d)
            target.chmod(stat.S_IMODE(member.mode))
            os.utime(target, (member.mtime, member.mtime))
assert seen == m['files'].keys()
assert (root/'third_party/icu-data-source/LICENSE').read_bytes() == license_bytes
print('Verified', len(seen), 'original ICU source members and archive SHA256')

#!/usr/bin/env python3
"""Vendor a byte-verified, deterministic upstream ICU data-generator source archive."""
import argparse, gzip, hashlib, json, pathlib, shutil, subprocess, tarfile, tempfile
root = pathlib.Path(__file__).resolve().parents[1]
commit = 'ddc6b66926bfac6036144ca9902992a0d6d50af9'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--repository', type=pathlib.Path, required=True)
o = p.parse_args()
out = root/'third_party/icu-data-source'
if out.exists(): p.error('refusing an existing output')
tree = subprocess.check_output(['git', '-C', str(o.repository), 'ls-tree', '-rz', commit,
                                '--', 'LICENSE', 'icu4c/source'])
expected = {}
for entry in tree.split(b'\0'):
    if not entry: continue
    metadata, name = entry.split(b'\t', 1)
    mode, kind, blob = metadata.split()
    assert kind == b'blob' and mode in (b'100644', b'100755')
    expected[name.decode()] = {'mode': int(mode, 8)&0o777, 'git_blob': blob.decode()}
with tempfile.TemporaryDirectory(prefix='chos-icu-source-import-') as temporary:
    raw = pathlib.Path(temporary)/'source.tar'
    with raw.open('wb') as f:
        subprocess.run(['git', '-C', str(o.repository), '-c', 'tar.umask=0022', 'archive', commit,
                        'LICENSE', 'icu4c/source'], stdout=f, check=True)
    records = {}
    with tarfile.open(raw) as archive:
        for member in archive:
            if member.isdir(): continue
            assert member.isfile() and member.name in expected and member.name not in records
            record = expected[member.name]
            data = archive.extractfile(member).read()
            assert member.mode == record['mode'] and len(data) == member.size
            assert hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest() == record['git_blob'], member.name
            records[member.name] = {**record, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
    assert records.keys() == expected.keys()
    out.mkdir()
    packed = out/'icu79-data-source.tar.gz'
    with raw.open('rb') as source, packed.open('wb') as target:
        with gzip.GzipFile(fileobj=target, mode='wb', filename='', mtime=0) as stream:
            shutil.copyfileobj(source, stream)
    with tarfile.open(raw) as archive:
        (out/'LICENSE').write_bytes(archive.extractfile('LICENSE').read())
manifest = {'schema': 1, 'project': 'ICU complete source for original data generators',
            'version': '79.1', 'unicode_version': '18.0', 'commit': commit,
            'source_date_epoch': int(subprocess.check_output(['git', '-C', str(o.repository), 'show', '-s', '--format=%ct', commit])),
            'source': 'https://github.com/unicode-org/icu', 'license': 'Unicode License V3',
            'purpose': 'Original host data generators and data inputs;host executables are not CHOS runtime libraries.',
            'archive': {'path': str(packed.relative_to(root)), 'bytes': packed.stat().st_size,
                        'sha256': hashlib.sha256(packed.read_bytes()).hexdigest()},
            'files': dict(sorted(records.items()))}
(root/'docs/dependencies/icu-data-source.json').write_text(json.dumps(manifest, indent=2)+'\n')
print('Imported', len(records), 'verified original files;', packed.stat().st_size, 'compressed bytes')

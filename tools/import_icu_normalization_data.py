#!/usr/bin/env python3
"""Import pinned original ICU normalization/UTS46 resources and Unicode tests."""
import argparse, hashlib, json, pathlib, subprocess
root = pathlib.Path(__file__).resolve().parents[1]
commit = 'ddc6b66926bfac6036144ca9902992a0d6d50af9'
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--repository', type=pathlib.Path, required=True)
o = p.parse_args()
out = root/'third_party/icu-normalization-data'
if out.exists():
    p.error('refusing an existing output')
paths = ['LICENSE'] + ['icu4c/source/data/in/'+n+'.nrm' for n in
                          ('nfkc', 'nfkc_cf', 'nfkc_scf', 'uts46')] + [
    'icu4c/source/data/unidata/NormalizationTest.txt',
    'icu4c/source/test/testdata/IdnaTestV2.txt']
files = {}
for name in paths:
    entry = subprocess.check_output(['git', '-C', str(o.repository), 'ls-tree',
                                     commit, '--', name]).split()
    assert len(entry) == 4 and entry[1] == b'blob'
    data = subprocess.check_output(['git', '-C', str(o.repository), 'show', commit+':'+name])
    assert hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest() == entry[2].decode()
    relative = name.removeprefix('icu4c/')
    target = out/relative
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
    files[relative] = {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                       'git_blob': entry[2].decode()}
manifest = {'schema': 1, 'project': 'ICU selected normalization and UTS46 data',
            'version': '79.1', 'unicode_version': '18.0.0', 'commit': commit,
            'source': 'https://github.com/unicode-org/icu', 'license': 'Unicode License V3',
            'scope': 'Four original little-endian Nrm2 resources and two complete original test corpora. Not a full ICU/CLDR data package.',
            'files': files}
(root/'docs/dependencies/icu-normalization-data.json').write_text(json.dumps(manifest, indent=2)+'\n')
print('Imported', len(files), 'verified original files')

#!/usr/bin/env python3
"""Verify the complete curated original WebCore image-source inventory offline."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / 'docs/dependencies/native-webcore-images.json'


def verify():
    manifest = json.loads(MANIFEST.read_text())
    base = ROOT / manifest['root']
    entries = list(base.rglob('*'))
    assert not base.is_symlink(), 'WebCore source root is a symlink'
    assert not any(path.is_symlink() for path in entries), 'WebCore source symlink refused'
    actual = {str(path.relative_to(base)) for path in entries if path.is_file()}
    assert actual == set(manifest['files']), 'Curated WebCore source inventory changed'
    for name, record in manifest['files'].items():
        path = base / name
        assert path.resolve().is_relative_to(base), 'Source escapes inventory root'
        data = path.read_bytes()
        assert len(data) == record['bytes'], 'Original source size changed: ' + name
        assert hashlib.sha256(data).hexdigest() == record['sha256'], 'Original source digest changed: ' + name
        blob = b'blob ' + str(len(data)).encode() + b'\0' + data
        assert hashlib.sha1(blob, usedforsecurity=False).hexdigest() == record['git_blob'], 'Pinned Git blob mismatch: ' + name
    assert set(manifest['licenses']).issubset(actual), 'Original license text missing'
    profile = json.loads((ROOT / 'ports/webcore-images/source-list.json').read_text())
    assert profile['upstream_commit'] == manifest['upstream_commit']
    assert set(profile['units'].values()).issubset(actual), 'Component source missing from inventory'
    print('WebKit', manifest['version'], len(actual), 'curated original files and licenses verified')
    return manifest


if __name__ == '__main__':
    verify()

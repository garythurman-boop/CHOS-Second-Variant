#!/usr/bin/env python3
"""Verify the authenticated original libwebp source inventory."""
import hashlib
import json
import pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
manifest = json.loads((ROOT / 'docs/dependencies/native-webp.json').read_text())
base = ROOT / manifest['root']
entries = list(base.rglob('*'))
assert not base.is_symlink() and not any(path.is_symlink() for path in entries)
assert {str(path.relative_to(base)) for path in entries if path.is_file()} == set(manifest['files'])
for name, expected in manifest['files'].items():
    assert hashlib.sha256((base / name).read_bytes()).hexdigest() == expected, name
for name, expected in manifest['authentication_files_sha256'].items():
    assert hashlib.sha256((ROOT / 'ports/webp/authentication' / name).read_bytes()).hexdigest() == expected, name
print('libwebp', manifest['version'], len(manifest['files']), 'original files verified')

#!/usr/bin/env python3
"""Verify original libjpeg-turbo source, licenses and authentication material."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
manifest = json.loads((root / 'docs/dependencies/native-jpeg.json').read_text())
base = root / manifest['root']
entries = list(base.rglob('*'))
assert not base.is_symlink() and not any(p.is_symlink() for p in entries)
actual = {str(p.relative_to(base)) for p in entries if p.is_file()}
assert actual == set(manifest['files']), 'Original JPEG source inventory changed'
for name, expected in manifest['files'].items():
    assert hashlib.sha256((base / name).read_bytes()).hexdigest() == expected, name
for name, expected in manifest['authentication_material'].items():
    path = root / name
    assert not path.is_symlink() and path.resolve().is_relative_to(root)
    assert hashlib.sha256(path.read_bytes()).hexdigest() == expected, name
print('libjpeg-turbo', manifest['version'], len(actual), 'original files and authentication material verified')

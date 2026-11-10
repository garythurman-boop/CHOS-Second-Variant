#!/usr/bin/env python3
"""Verify unchanged selected original Cairo and pixman source/build/license bytes."""
import pathlib,json,hashlib
root=pathlib.Path(__file__).resolve().parents[1]
manifest=json.loads((root/'docs/dependencies/native-graphics.json').read_text())
for project in manifest['projects']:
    base=root/project['root']
    actual={str(p.relative_to(base)) for p in base.rglob('*') if p.is_file()}
    assert actual==set(project['files']),'Graphics source inventory changed'
    for name,wanted in project['files'].items():
        path=base/name
        assert path.is_file() and not path.is_symlink() and path.resolve().is_relative_to(base)
        assert hashlib.sha256(path.read_bytes()).hexdigest()==wanted,name
    print(project['name'],project['version'],len(actual),'original files verified')

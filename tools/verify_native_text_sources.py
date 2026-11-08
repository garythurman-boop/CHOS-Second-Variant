#!/usr/bin/env python3
"""Verify exact original font-library sources and licensed shaping fixtures."""
from pathlib import Path
import json,hashlib
root=Path(__file__).resolve().parents[1]
manifest=json.loads((root/'docs/dependencies/native-fonts.json').read_text())
for project in manifest['projects']:
 base=root/project['root'];actual={str(p.relative_to(base)) for p in base.rglob('*') if p.is_file()}
 assert actual==set(project['files']),'Source inventory changed: '+project['name']
 for name,want in project['files'].items():
  p=base/name;assert p.is_file() and not p.is_symlink() and p.resolve().is_relative_to(base)
  assert hashlib.sha256(p.read_bytes()).hexdigest()==want,name
 print(project['name'],project['version'],len(actual),'original source/build/notice files verified')
fixture=json.loads((root/'docs/dependencies/text-fixtures.json').read_text());base=root/'third_party/text-fixtures'
assert set(fixture['files'])=={str(p.relative_to(base)) for p in base.rglob('*') if p.is_file()}
for name,want in fixture['files'].items():assert hashlib.sha256((base/name).read_bytes()).hexdigest()==want,name
assert len(fixture['fonts'])==5 and fixture['original_harfbuzz_tests']['cases']==64
print('Five actual licensed fonts and complete original64-case shaper corpus verified')

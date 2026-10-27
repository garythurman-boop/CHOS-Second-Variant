#!/usr/bin/env python3
"""Verify the complete staged native SDK inventory, without executing target code."""
import argparse,hashlib,json,pathlib
p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=pathlib.Path);o=p.parse_args();root=o.root.resolve();m=json.loads((root/'chos-sdk.json').read_text())
assert m['schema']==1 and m['target']=='aarch64-none-elf'
actual={str(p.relative_to(root)) for p in root.rglob('*') if p.is_file() and p.name!='chos-sdk.json'}
assert actual==set(m['files']),'SDK membership changed'
for name,wanted in m['files'].items():
 path=root/name
 assert not pathlib.Path(name).is_absolute() and '..' not in path.parts and not path.is_symlink()
 assert path.resolve().is_relative_to(root)
 assert hashlib.sha256(path.read_bytes()).hexdigest()==wanted,'SDK hash mismatch: '+name
print('Native SDK: '+str(len(actual))+' staged files verified')

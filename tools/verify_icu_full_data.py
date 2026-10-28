#!/usr/bin/env python3
"""Verify generated ICU data, all resource bytes and the actual ARM64 archive."""
import argparse, hashlib, json, pathlib, subprocess, tempfile
from icu_data_package import resources
root = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build-dir', type=pathlib.Path, required=True)
p.add_argument('--extract-dir', type=pathlib.Path)
p.add_argument('--resource', action='append', default=[])
o = p.parse_args(); directory = o.build_dir.resolve()
report = json.loads((directory/'build-report.json').read_text())
expected = json.loads((root/'docs/dependencies/icu-generated-data.json').read_text())
source = json.loads((root/'docs/dependencies/icu-data-source.json').read_text())
assert report['source_commit']==expected['source_commit']==source['commit']
assert report['source_archive_sha256']==source['archive']['sha256']
actual = {str(f.relative_to(directory)) for f in directory.rglob('*') if f.is_file() and f.name!='build-report.json'}
assert actual==set(report['files'])
for name, wanted in report['files'].items():
    path = directory/name
    assert not path.is_symlink() and path.resolve().is_relative_to(directory)
    assert hashlib.sha256(path.read_bytes()).hexdigest()==wanted, name
data = (directory/'icudt79l.dat').read_bytes()
assert len(data)==expected['package_bytes'] and hashlib.sha256(data).hexdigest()==expected['package_sha256']
inventory = json.loads((directory/'resources.json').read_text())
assert hashlib.sha256((directory/'resources.json').read_bytes()).hexdigest()==expected['resources_sha256']
items = resources(data, inventory)
assert len(items)==expected['resource_count']
assert (directory/'package-entries.txt').read_text().splitlines()==list(items)
assert subprocess.check_output(['llvm-ar','t',str(directory/'libicudata.a')],text=True).splitlines()==['icudata.o']
with tempfile.TemporaryDirectory(prefix='chos-icu-data-verify-') as temporary:
    obj = pathlib.Path(temporary)/'data.o'
    obj.write_bytes(subprocess.check_output(['llvm-ar','p',str(directory/'libicudata.a'),'icudata.o']))
    elf = obj.read_bytes(); assert elf[:6]==b'\x7fELF\2\1' and elf[16:20]==b'\1\0\xb7\0'
    assert subprocess.check_output(['llvm-nm','--defined-only',str(obj)],text=True).split()==['0000000000000000','R','icudt79_dat']
    payload = pathlib.Path(temporary)/'payload.dat'
    subprocess.run(['llvm-objcopy','--dump-section','.rodata='+str(payload),str(obj)],check=True)
    assert payload.read_bytes()==data
if o.extract_dir:
    o.extract_dir.mkdir(parents=True, exist_ok=False)
    for name in o.resource or items:
        assert name in items
        target = o.extract_dir/name; target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(items[name])
else:
    assert not o.resource, '--resource requires --extract-dir'
print('Verified',len(items),'generated resources and real AArch64 libicudata.a; no native execution implied')

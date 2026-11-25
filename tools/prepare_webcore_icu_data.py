#!/usr/bin/env python3
"""Partition the pinned ICU common data into CHFS-sized packages for WebCore."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from icu_data_package import resources

ROOT = Path(__file__).resolve().parents[1]
PACKAGE_COUNT = 9
FILE_LIMIT = 4 * 1024 * 1024


def digest(data):
    return hashlib.sha256(data).hexdigest()


def align16(size):
    return (size + 15) & ~15


def encode(header, names, data):
    # Original ICU CmnD offsets are relative to the start of the package body.
    names = sorted(names)
    offsets = bytearray(4 + 8 * len(names))
    struct.pack_into('<I', offsets, 0, len(names))
    strings = bytearray()
    bodies = bytearray()
    encoded_names = [('icudt79l/' + name).encode('ascii') + b'\0' for name in names]
    data_start = align16(len(offsets) + sum(map(len, encoded_names)))
    for i, (name, encoded) in enumerate(zip(names, encoded_names)):
        struct.pack_into('<II', offsets, 4 + i * 8, len(offsets) + len(strings), data_start + len(bodies))
        strings.extend(encoded)
        bodies.extend(data[name])
        bodies.extend(bytes(align16(len(bodies)) - len(bodies)))
    offsets.extend(strings)
    offsets.extend(bytes(data_start - len(offsets)))
    return header + offsets + bodies


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='Verified complete ICU data build directory')
    parser.add_argument('--out', type=Path, required=True, help='New directory for the nine CHFS resources')
    args = parser.parse_args()
    expected = json.loads((ROOT / 'docs/dependencies/icu-generated-data.json').read_text())
    original = (args.source / 'icudt79l.dat').read_bytes()
    inventory_bytes = (args.source / 'resources.json').read_bytes()
    if digest(original) != expected['package_sha256'] or digest(inventory_bytes) != expected['resources_sha256']:
        raise ValueError('ICU source package or resource inventory differs from the pinned build')
    inventory = json.loads(inventory_bytes)
    entries = resources(original, inventory)
    header = original[:struct.unpack_from('<H', original)[0]]
    groups = [[] for _ in range(PACKAGE_COUNT)]
    sizes = [len(header) + 4] * len(groups)
    for name in sorted(entries, key=lambda name: (-len(entries[name]), name)):
        index = min(range(len(groups)), key=sizes.__getitem__)
        groups[index].append(name)
        sizes[index] += 8 + len('icudt79l/') + len(name) + 1 + align16(len(entries[name]))
    args.out.mkdir(parents=True, exist_ok=False)
    manifest = {'source_sha256': expected['package_sha256'], 'resource_count': len(entries), 'files': []}
    seen = set()
    for index, names in enumerate(groups):
        package = encode(header, names, entries)
        if len(package) > FILE_LIMIT:
            raise ValueError('Package exceeds the CHFS file size limit')
        verified = resources(package, {name: inventory[name] for name in names})
        if seen.intersection(verified):
            raise ValueError('Duplicate resource')
        seen.update(verified)
        filename = f'ICU{index}.DAT'
        (args.out / filename).write_bytes(package)
        manifest['files'].append({'name': filename, 'bytes': len(package), 'sha256': digest(package),
                                  'entries': len(names)})
    if seen != entries.keys():
        raise ValueError('Resource membership differs from the original package')
    pinned = json.loads((ROOT / 'docs/dependencies/icu-browser-packages.json').read_text())
    if manifest != pinned:
        raise ValueError('Generated ICU package receipt differs from the verified browser distribution')
    (args.out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Verified {len(entries)} resources across {len(groups)} ICU packages')


if __name__ == '__main__':
    main()

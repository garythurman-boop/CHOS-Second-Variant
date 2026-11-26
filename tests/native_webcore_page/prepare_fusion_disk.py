#!/usr/bin/env python3
"""Build a fresh CHFS data disk for the local WebCore page diagnostic."""
import argparse
import pathlib
import runpy
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', required=True, type=pathlib.Path)
    parser.add_argument('--icu-packages', required=True, type=pathlib.Path)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    flat = args.out / 'CHOS-assets-flat.vmdk'
    subprocess.run(['python3', str(ROOT / 'tools/mkchfs.py'), 'create', str(flat), '--size-mib', '64'], check=True)
    harness = runpy.run_path(str(ROOT / 'tests/native_webcore_network_values/qemu.py'))
    harness['stage_page_assets'](flat, args.icu_packages)
    (args.out / 'CHOS-assets.vmdk').write_text('''# Disk DescriptorFile
version=1
encoding="UTF-8"
CID=27da64ef
parentCID=ffffffff
createType="monolithicFlat"

RW 131072 FLAT "CHOS-assets-flat.vmdk" 0

ddb.adapterType = "lsilogic"
ddb.geometry.cylinders = "128"
ddb.geometry.heads = "16"
ddb.geometry.sectors = "64"
''')


if __name__ == '__main__':
    main()

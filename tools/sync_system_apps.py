#!/usr/bin/env python3
"""Refresh CHOS system applications in an existing CHFS disk image safely.

Unlike `make disk-image`, this preserves ordinary user files and settings. Only
system application paths supplied with --app are updated, and identical payloads
are left untouched to avoid needless flash writes and disk-image fragmentation.
"""
from __future__ import annotations
import argparse
import pathlib
import sys

import mkchfs


def sync_apps(image_path: pathlib.Path, specifications: list[str]) -> int:
    image = mkchfs.load_image(image_path)
    changed = 0
    for spec in specifications:
        if '=' not in spec:
            raise ValueError('--app requires /CHFS/PATH=HOST_FILE')
        target, source_text = spec.split('=', 1)
        payload = pathlib.Path(source_text).read_bytes()
        _, _, entries, _ = mkchfs.active_dir(image)
        existing = next((e for e in entries if e is not None and
                         e['path'].casefold() == target.casefold()), None)
        if existing is not None:
            if (existing['crc'] == mkchfs.DIR_TAG and existing['start'] == 0 and
                    existing['sectors'] == 0 and existing['size'] == 0):
                raise ValueError(f'system app path is a directory: {target}')
            start = existing['start'] * mkchfs.SECTOR
            end = start + existing['size']
            current = bytes(image[start:end])
            if (len(current) == existing['size'] and
                    mkchfs.crc(current) == existing['crc'] and current == payload):
                print(f'unchanged {target}')
                continue
        mkchfs.put(image, target, payload)
        changed += 1
        print(f'updated {target}')
    if changed:
        image_path.write_bytes(image)
    print(f'system app sync complete: {changed} updated')
    return changed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=pathlib.Path)
    parser.add_argument('--app', action='append', default=[], help='/CHFS/PATH=HOST_FILE')
    args = parser.parse_args()
    if not args.app:
        parser.error('at least one --app mapping is required')
    try:
        sync_apps(args.image, args.app)
        return 0
    except (OSError, ValueError, UnicodeError) as exc:
        print(f'sync_system_apps: error: {exc}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())

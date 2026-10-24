#!/usr/bin/env python3
"""Create, inspect, populate and verify CHFS v1/v2/v3 raw disk images."""
from __future__ import annotations
import argparse
import pathlib
import struct
import sys
import zlib

SECTOR = 512
VERSION = 3
V2_VERSION = 2
V1_VERSION = 1
MAX_FILES = 511
V2_MAX_FILES = 63
V1_MAX_FILES = 23
DIR_TAG = 0x52494443
PATH_SIZE = 48
ENTRY_SIZE = 64
DIR_SECTORS = 64
V2_DIR_SECTORS = 8
V1_DIR_SECTORS = 3
# Legacy v1 metadata locations. V2 stores metadata in the volume tail; v3 uses
# two front-reserved 64-sector copies so the remaining volume can be data space.
DIR_A = 1
DIR_B = 4
LEGACY_DATA_LBA = 7
V3_DIR_A = 1
V3_DIR_B = 65
DATA_LBA = 129
MAX_FILE = 4 * 1024 * 1024


def crc(data: bytes | bytearray) -> int:
    return zlib.crc32(data) & 0xffffffff


def path_ok(path: str) -> bool:
    if not path.startswith('/') or len(path) < 2 or len(path) >= PATH_SIZE or path.endswith('/'):
        return False
    for comp in path[1:].split('/'):
        if not comp or comp in ('.', '..') or not all(c.isascii() and (c.isalnum() or c in '_-.') for c in comp):
            return False
    return True


def layout_for(version: int, sectors: int) -> dict[str, int]:
    if version == V1_VERSION:
        if sectors < 8:
            raise ValueError('CHFS v1 image is too small')
        return {'version': V1_VERSION, 'total': sectors, 'a': DIR_A, 'b': DIR_B,
                'dir_sectors': V1_DIR_SECTORS, 'data_start': LEGACY_DATA_LBA,
                'data_end': sectors, 'max_files': V1_MAX_FILES}
    if version == V2_VERSION:
        if sectors < 24:
            raise ValueError('CHFS v2 image is too small')
        a = sectors - 2 * V2_DIR_SECTORS
        b = sectors - V2_DIR_SECTORS
        if a <= LEGACY_DATA_LBA:
            raise ValueError('CHFS v2 has no data area')
        return {'version': V2_VERSION, 'total': sectors, 'a': a, 'b': b,
                'dir_sectors': V2_DIR_SECTORS, 'data_start': LEGACY_DATA_LBA,
                'data_end': a, 'max_files': V2_MAX_FILES}
    if version == VERSION:
        if sectors < 130:
            raise ValueError('CHFS v3 image is too small')
        a = V3_DIR_A
        b = V3_DIR_B
        return {'version': VERSION, 'total': sectors, 'a': a, 'b': b,
                'dir_sectors': DIR_SECTORS, 'data_start': DATA_LBA,
                'data_end': sectors, 'max_files': MAX_FILES}
    raise ValueError(f'unsupported CHFS version: {version}')


def superblock(sectors: int, version: int = VERSION) -> bytes:
    layout = layout_for(version, sectors)
    b = bytearray(SECTOR)
    b[0:4] = b'CHFS'
    struct.pack_into('<IIQIIII', b, 4, version, SECTOR, sectors,
                     layout['a'], layout['b'], layout['dir_sectors'], layout['data_start'])
    # v1 had an additional reserved u32 at byte 36; keep it zero in both versions.
    struct.pack_into('<I', b, 508, 0)
    struct.pack_into('<I', b, 508, crc(b))
    return bytes(b)


def parse_super(b: bytes, image_sectors: int) -> dict[str, int]:
    if len(b) != SECTOR or b[:4] != b'CHFS':
        raise ValueError('missing CHFS superblock')
    version, sector_size = struct.unpack_from('<II', b, 4)
    sectors = struct.unpack_from('<Q', b, 12)[0]
    a, bb, dir_sectors, data_start = struct.unpack_from('<IIII', b, 20)
    layout = layout_for(version, int(sectors))
    tmp = bytearray(b)
    found = struct.unpack_from('<I', tmp, 508)[0]
    struct.pack_into('<I', tmp, 508, 0)
    if (sector_size != SECTOR or crc(tmp) != found or sectors > image_sectors or
            (a, bb, dir_sectors, data_start) !=
            (layout['a'], layout['b'], layout['dir_sectors'], layout['data_start'])):
        raise ValueError('invalid CHFS superblock checksum or geometry')
    return layout


def check_super(b: bytes, image_sectors: int) -> int:
    """Compatibility wrapper used by existing scripts/tests."""
    return parse_super(b, image_sectors)['total']


def encode_entry(e: dict | None) -> bytes:
    if e is None:
        return bytes(ENTRY_SIZE)
    name = e['path'].encode('ascii') + b'\0'
    if len(name) > PATH_SIZE:
        raise ValueError('path too long')
    return name.ljust(PATH_SIZE, b'\0') + struct.pack('<IIII', e['start'], e['sectors'], e['size'], e['crc'])


def encode_dir(entries: list[dict | None], generation: int, version: int = VERSION) -> bytes:
    if version == V1_VERSION:
        cap, sectors = V1_MAX_FILES, V1_DIR_SECTORS
    elif version == V2_VERSION:
        cap, sectors = V2_MAX_FILES, V2_DIR_SECTORS
    else:
        cap, sectors = MAX_FILES, DIR_SECTORS
    if len(entries) > cap:
        raise ValueError(f'CHFS v{version} has only {cap} directory slots')
    b = bytearray(sectors * SECTOR)
    b[:4] = b'CHDR'
    struct.pack_into('<III', b, 4, version, generation & 0xffffffff, 0)
    for i in range(cap):
        b[16 + i * ENTRY_SIZE:16 + (i + 1) * ENTRY_SIZE] = encode_entry(entries[i] if i < len(entries) else None)
    struct.pack_into('<I', b, 12, crc(b))
    return bytes(b)


def decode_dir(b: bytes, total_sectors: int, data_end: int | None = None,
               version: int | None = None) -> tuple[int, list[dict | None]]:
    if len(b) < 16 or b[:4] != b'CHDR':
        raise ValueError('invalid CHFS directory header')
    found_version = struct.unpack_from('<I', b, 4)[0]
    if version is not None and found_version != version:
        raise ValueError('CHFS directory version mismatch')
    version = found_version
    if version == V1_VERSION:
        cap, sectors = V1_MAX_FILES, V1_DIR_SECTORS
        if data_end is None:
            data_end = total_sectors
        data_start = LEGACY_DATA_LBA
    elif version == V2_VERSION:
        cap, sectors = V2_MAX_FILES, V2_DIR_SECTORS
        if data_end is None:
            data_end = total_sectors - 2 * V2_DIR_SECTORS
        data_start = LEGACY_DATA_LBA
    elif version == VERSION:
        cap, sectors = MAX_FILES, DIR_SECTORS
        if data_end is None:
            data_end = total_sectors
        data_start = DATA_LBA
    else:
        raise ValueError(f'unsupported CHFS directory version: {version}')
    if len(b) != sectors * SECTOR:
        raise ValueError('invalid CHFS directory length')
    tmp = bytearray(b)
    generation, found = struct.unpack_from('<II', tmp, 8)
    struct.pack_into('<I', tmp, 12, 0)
    if crc(tmp) != found:
        raise ValueError('invalid CHFS directory checksum')

    # Decode records in one pass, then validate namespace and extent overlap
    # with indexed checks. This keeps a large catalogue from turning every
    # host-tool mutation into cubic work across repeated full-image commits.
    entries: list[dict | None] = []
    by_path: dict[str, dict] = {}
    paths_with_children: set[str] = set()
    extents: list[tuple[int, int, str]] = []
    for i in range(cap):
        raw = b[16 + i * ENTRY_SIZE:16 + (i + 1) * ENTRY_SIZE]
        if raw[0] == 0:
            entries.append(None)
            continue
        try:
            name = raw[:PATH_SIZE].split(b'\0', 1)[0].decode('ascii')
        except UnicodeDecodeError as exc:
            raise ValueError('non-ASCII CHFS path') from exc
        start, nsec, size, fcrc = struct.unpack_from('<IIII', raw, PATH_SIZE)
        e = {'path': name, 'start': start, 'sectors': nsec, 'size': size, 'crc': fcrc}
        if not path_ok(name):
            raise ValueError('invalid CHFS directory path')
        key = name.casefold()
        if key in by_path:
            raise ValueError('duplicate CHFS path')
        is_dir = start == 0 and nsec == 0 and size == 0 and fcrc == DIR_TAG
        is_empty = start == 0 and nsec == 0 and size == 0 and fcrc == 0
        is_file = not (is_dir or is_empty)
        if is_file:
            if not nsec or not size or size > nsec * SECTOR or start < data_start or start + nsec > data_end:
                raise ValueError('invalid CHFS file extent')
            if key in paths_with_children:
                raise ValueError('file path is parent of another CHFS entry')
            extents.append((start, start + nsec, name))
        elif not is_dir and not is_empty:
            raise ValueError('invalid CHFS record')

        # An existing file cannot be an ancestor of this record. The ancestor
        # chain is bounded by the on-disk path field (48 bytes).
        parent = key.rsplit('/', 1)[0]
        while parent and parent != '/':
            existing_parent = by_path.get(parent)
            if existing_parent is not None:
                existing_is_dir = (existing_parent['start'] == existing_parent['sectors'] ==
                                   existing_parent['size'] == 0 and existing_parent['crc'] == DIR_TAG)
                if not existing_is_dir:
                    raise ValueError('file path is parent of another CHFS entry')
            if '/' not in parent[1:]:
                break
            parent = parent.rsplit('/', 1)[0]

        by_path[key] = e
        # Record every ancestor so a file added later cannot shadow existing
        # nested children. This remains bounded by path length, not catalogue size.
        ancestor = key.rsplit('/', 1)[0]
        while ancestor and ancestor != '/':
            paths_with_children.add(ancestor)
            if '/' not in ancestor[1:]:
                break
            ancestor = ancestor.rsplit('/', 1)[0]
        entries.append(e)

    extents.sort(key=lambda item: item[0])
    for previous, current in zip(extents, extents[1:]):
        if current[0] < previous[1]:
            raise ValueError('overlapping CHFS extents')
    return generation, entries


def format_image(sectors: int, version: int = VERSION) -> bytearray:
    layout = layout_for(version, sectors)
    image = bytearray(sectors * SECTOR)
    image[:SECTOR] = superblock(sectors, version)
    empty = [None] * layout['max_files']
    image[layout['a'] * SECTOR:(layout['a'] + layout['dir_sectors']) * SECTOR] = encode_dir(empty, 1, version)
    image[layout['b'] * SECTOR:(layout['b'] + layout['dir_sectors']) * SECTOR] = encode_dir(empty, 0, version)
    return image


def active_dir(image: bytearray) -> tuple[int, int, list[dict | None], int]:
    image_sectors = len(image) // SECTOR
    layout = parse_super(bytes(image[:SECTOR]), image_sectors)
    valid = []
    for lba in (layout['a'], layout['b']):
        try:
            start = lba * SECTOR
            end = (lba + layout['dir_sectors']) * SECTOR
            g, entries = decode_dir(bytes(image[start:end]), layout['total'],
                                    data_end=layout['data_end'], version=layout['version'])
            valid.append((g, lba, entries))
        except ValueError:
            pass
    if not valid:
        raise ValueError('both CHFS directory copies are invalid')
    # Generations wrap at uint32; for normal volume lifetimes this signed delta
    # comparison matches the kernel's generation-selection logic.
    valid.sort(key=lambda x: x[0])
    g, lba, entries = valid[-1]
    return layout['total'], g, entries, lba


def commit(image: bytearray, entries: list[dict | None], generation: int, active_lba: int) -> tuple[int, int]:
    layout = parse_super(bytes(image[:SECTOR]), len(image) // SECTOR)
    if len(entries) != layout['max_files']:
        raise ValueError('directory entry count does not match CHFS version')
    target = layout['b'] if active_lba == layout['a'] else layout['a']
    next_generation = (generation + 1) & 0xffffffff
    raw = encode_dir(entries, next_generation, layout['version'])
    start = target * SECTOR
    image[start:start + len(raw)] = raw
    return next_generation, target


def _find(entries: list[dict | None], path: str) -> int | None:
    return next((i for i, e in enumerate(entries) if e is not None and e['path'].casefold() == path.casefold()), None)


def _is_dir(entries: list[dict | None], path: str) -> bool:
    if path == '/':
        return True
    found = _find(entries, path)
    if found is not None:
        e = entries[found]
        return e is not None and e['crc'] == DIR_TAG and e['start'] == e['sectors'] == e['size'] == 0
    prefix = path.rstrip('/') + '/'
    return any(e is not None and e['path'].casefold().startswith(prefix.casefold()) for e in entries)


def _free_slot(entries: list[dict | None]) -> int:
    slot = next((i for i, e in enumerate(entries) if e is None), None)
    if slot is None:
        raise ValueError(f'CHFS metadata is full ({len(entries)} entries)')
    return slot


def _add_directory(entries: list[dict | None], path: str) -> None:
    if not path_ok(path):
        raise ValueError(f'invalid CHFS directory path: {path}')
    found = _find(entries, path)
    if found is not None:
        existing = entries[found]
        if existing is not None and existing['crc'] == DIR_TAG and existing['start'] == existing['sectors'] == existing['size'] == 0:
            return
        raise ValueError(f'a file already exists at {path}')
    if not _is_dir(entries, path.rsplit('/', 1)[0] or '/'):
        raise ValueError(f'parent directory does not exist: {path}')
    entries[_free_slot(entries)] = {'path': path, 'start': 0, 'sectors': 0, 'size': 0, 'crc': DIR_TAG}


def mkdir(image: bytearray, path: str) -> None:
    if not path_ok(path):
        raise ValueError(f'invalid CHFS directory path: {path}')
    _, generation, entries, active_lba = active_dir(image)
    _add_directory(entries, path)
    commit(image, entries, generation, active_lba)


def put(image: bytearray, path: str, data: bytes) -> None:
    if not path_ok(path):
        raise ValueError(f'invalid CHFS absolute path: {path}')
    if len(data) > MAX_FILE:
        raise ValueError('files are limited to 4 MiB')
    total, generation, entries, active_lba = active_dir(image)
    layout = parse_super(bytes(image[:SECTOR]), len(image) // SECTOR)
    entries = list(entries)
    backup_lba = layout['b'] if active_lba == layout['a'] else layout['a']
    backup_start = backup_lba * SECTOR
    backup_end = (backup_lba + layout['dir_sectors']) * SECTOR
    try:
        _, backup_entries = decode_dir(bytes(image[backup_start:backup_end]), total,
                                       data_end=layout['data_end'], version=layout['version'])
    except ValueError:
        backup_entries = []
    parent = path.rsplit('/', 1)[0] or '/'
    if not _is_dir(entries, parent):
        parts = path.strip('/').split('/')[:-1]
        current = ''
        for part in parts:
            current += '/' + part
            if not _is_dir(entries, current):
                _add_directory(entries, current)
    slot = _find(entries, path)
    if slot is not None:
        old = entries[slot]
        if old is not None and old['crc'] == DIR_TAG:
            raise ValueError(f'cannot replace directory with file: {path}')
    else:
        slot = _free_slot(entries)
    nsec = (len(data) + SECTOR - 1) // SECTOR
    reserved = [e for e in entries + list(backup_entries) if e is not None and e['sectors'] > 0]
    candidate = layout['data_start']
    if nsec:
        while candidate + nsec <= layout['data_end']:
            collision = any(candidate < e['start'] + e['sectors'] and e['start'] < candidate + nsec for e in reserved)
            if not collision:
                break
            candidate += 1
        if candidate + nsec > layout['data_end']:
            raise ValueError('no contiguous extent available; delete or compact files')
        image[candidate * SECTOR:(candidate + nsec) * SECTOR] = data.ljust(nsec * SECTOR, b'\0')
    else:
        candidate = 0
    entries[slot] = {'path': path, 'start': candidate, 'sectors': nsec, 'size': len(data), 'crc': crc(data)}
    commit(image, entries, generation, active_lba)


def load_image(path: pathlib.Path) -> bytearray:
    raw = path.read_bytes()
    if len(raw) % SECTOR:
        raise ValueError('image size is not a multiple of 512 bytes')
    b = bytearray(raw)
    active_dir(b)
    return b


def cmd_create(args: argparse.Namespace) -> None:
    path = pathlib.Path(args.image)
    if path.exists() and not args.force:
        raise ValueError(f'{path} already exists; use --force to replace it')
    sectors = args.size_mib * 1024 * 1024 // SECTOR
    if args.size_mib < 1 or sectors > 0xffffffff:
        raise ValueError('size must be 1 MiB or larger')
    b = format_image(sectors)
    for spec in args.put:
        if '=' not in spec:
            raise ValueError('--put requires /CHFS/PATH=HOST_FILE')
        dst, src = spec.split('=', 1)
        put(b, dst, pathlib.Path(src).read_bytes())
    path.write_bytes(b)
    final_entries = active_dir(b)[2]
    file_count = sum(e is not None and not (e['start'] == e['sectors'] == e['size'] == 0 and e['crc'] == DIR_TAG) for e in final_entries)
    directory_count = sum(e is not None and e['start'] == e['sectors'] == e['size'] == 0 and e['crc'] == DIR_TAG for e in final_entries)
    print(f'created {path}: CHFS v{VERSION}, {sectors} sectors ({len(b)} bytes), {file_count} files, {directory_count} directories')


def cmd_list(args: argparse.Namespace) -> None:
    b = load_image(pathlib.Path(args.image)); total, generation, entries, active = active_dir(b)
    layout = parse_super(bytes(b[:SECTOR]), len(b) // SECTOR)
    print(f"CHFS v{layout['version']}: sectors={total}, capacity={total * SECTOR} bytes, generation={generation}, directory-lba={active}, entries={layout['max_files']}")
    found = 0
    for e in entries:
        if e is not None:
            found += 1
            if e['crc'] == DIR_TAG and e['start'] == e['sectors'] == e['size'] == 0:
                print(f"DIR {'':>8}         {e['path']}/")
                continue
            data = bytes(b[e['start'] * SECTOR:e['start'] * SECTOR + e['size']])
            valid = crc(data) == e['crc']
            print(f"{'OK ' if valid else 'BAD'} {e['size']:>8} bytes  {e['path']}")
            if not valid:
                raise ValueError(f"file checksum failed: {e['path']}")
    if not found:
        print('(no files)')


def cmd_mkdir(args: argparse.Namespace) -> None:
    path = pathlib.Path(args.image); b = load_image(path)
    mkdir(b, args.path)
    path.write_bytes(b)
    print(f'created directory {args.path}')


def cmd_put(args: argparse.Namespace) -> None:
    path = pathlib.Path(args.image); b = load_image(path)
    put(b, args.path, pathlib.Path(args.host_file).read_bytes())
    path.write_bytes(b)
    print(f'installed {args.path} from {args.host_file}')


def cmd_read(args: argparse.Namespace) -> None:
    b = load_image(pathlib.Path(args.image)); _, _, entries, _ = active_dir(b)
    e = next((x for x in entries if x is not None and x['path'].casefold() == args.path.casefold()), None)
    if e is None:
        raise ValueError('file not found')
    if e['crc'] == DIR_TAG and e['start'] == e['sectors'] == e['size'] == 0:
        raise ValueError('path is a directory')
    data = bytes(b[e['start'] * SECTOR:e['start'] * SECTOR + e['size']])
    if crc(data) != e['crc']:
        raise ValueError('file data checksum failed')
    pathlib.Path(args.output).write_bytes(data)
    print(f'extracted {args.path} to {args.output}')


def cmd_rm(args: argparse.Namespace) -> None:
    path = pathlib.Path(args.image); b = load_image(path)
    _, gen, entries, active = active_dir(b)
    idx = next((i for i, x in enumerate(entries) if x is not None and x['path'].casefold() == args.path.casefold()), None)
    if idx is None:
        raise ValueError('file not found')
    item = entries[idx]
    if item is not None and item['crc'] == DIR_TAG and item['start'] == item['sectors'] == item['size'] == 0:
        raise ValueError('path is a directory; use rmdir')
    entries[idx] = None
    commit(b, entries, gen, active)
    path.write_bytes(b)
    print(f'removed file {args.path}')


def cmd_rmdir(args: argparse.Namespace) -> None:
    path = pathlib.Path(args.image); b = load_image(path)
    _, gen, entries, active = active_dir(b)
    idx = next((i for i, x in enumerate(entries) if x is not None and x['path'].casefold() == args.path.casefold()), None)
    if idx is None:
        prefix = args.path.rstrip('/') + '/'
        if any(x is not None and x['path'].casefold().startswith(prefix.casefold()) for x in entries):
            raise ValueError('directory is implicit and still has children')
        raise ValueError('directory not found')
    item = entries[idx]
    if item is None or not (item['crc'] == DIR_TAG and item['start'] == item['sectors'] == item['size'] == 0):
        raise ValueError('path is not a directory')
    prefix = args.path.rstrip('/') + '/'
    if any(x is not None and x['path'].casefold().startswith(prefix.casefold()) for x in entries):
        raise ValueError('directory is not empty')
    entries[idx] = None
    commit(b, entries, gen, active)
    path.write_bytes(b)
    print(f'removed directory {args.path}')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest='command', required=True)
    p = subs.add_parser('create', help='create and format a CHFS image')
    p.add_argument('image'); p.add_argument('--size-mib', type=int, default=8); p.add_argument('--force', action='store_true')
    p.add_argument('--put', action='append', default=[], help='install at creation time: /CHFS/PATH=HOST_FILE')
    p.set_defaults(func=cmd_create)
    p = subs.add_parser('list', help='verify image and list files'); p.add_argument('image'); p.set_defaults(func=cmd_list)
    p = subs.add_parser('put', help='install or replace a file'); p.add_argument('image'); p.add_argument('path'); p.add_argument('host_file'); p.set_defaults(func=cmd_put)
    p = subs.add_parser('mkdir', help='create an empty directory'); p.add_argument('image'); p.add_argument('path'); p.set_defaults(func=cmd_mkdir)
    p = subs.add_parser('read', help='extract a file'); p.add_argument('image'); p.add_argument('path'); p.add_argument('output'); p.set_defaults(func=cmd_read)
    p = subs.add_parser('rm', help='remove a file'); p.add_argument('image'); p.add_argument('path'); p.set_defaults(func=cmd_rm)
    p = subs.add_parser('rmdir', help='remove an explicitly stored empty directory'); p.add_argument('image'); p.add_argument('path'); p.set_defaults(func=cmd_rmdir)
    args = parser.parse_args()
    try: args.func(args); return 0
    except (OSError, ValueError, UnicodeError) as exc:
        print(f'mkchfs: error: {exc}', file=sys.stderr); return 2

if __name__ == '__main__':
    raise SystemExit(main())

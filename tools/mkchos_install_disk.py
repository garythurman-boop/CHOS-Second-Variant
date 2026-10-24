#!/usr/bin/env python3
"""Create a bootable ARM64 CHOS disk image with GPT, FAT32 ESP and CHFS data partition.

This produces an image file only; it never writes to a host block device. It is a
safe offline installer-image builder, not the future interactive in-OS wizard.
"""
from __future__ import annotations
import argparse
import hashlib
import math
import os
import struct
import sys
import stat
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.fat32_builder import Fat32Builder  # noqa: E402
from tools import mkchfs, mkuefi_iso  # noqa: E402

SECTOR = 512
GPT_ENTRIES = 128
GPT_ENTRY_SIZE = 128
GPT_ENTRY_BYTES = GPT_ENTRIES * GPT_ENTRY_SIZE
ESP_MIB = 64
ALIGN_SECTORS = 2048
EFI_GUID = uuid.UUID('c12a7328-f81f-11d2-ba4b-00a0c93ec93b').bytes_le
CHOS_GUID = uuid.UUID('0f2ac3ac-2af2-4e10-a8b3-4d41c0a0260e').bytes_le
NAMESPACE = uuid.UUID('22c94c1f-3c2c-4b8a-9a00-52a1ccfe74c6')


def crc32(data: bytes | bytearray) -> int:
    import zlib
    return zlib.crc32(data) & 0xffffffff


def align_up(value: int, alignment: int) -> int:
    return ((value + alignment - 1) // alignment) * alignment


def open_safe_image_output(path: Path, force: bool):
    """Open a regular image file without following symlinks or device nodes."""
    resolved_parent = path.parent.resolve(strict=True)
    resolved_candidate = resolved_parent / path.name
    forbidden_roots = (Path('/dev'), Path('/proc'), Path('/sys'))
    for root in forbidden_roots:
        try:
            resolved_candidate.relative_to(root)
            raise ValueError(f'output path is inside protected system tree {root}: {path}')
        except ValueError as exc:
            if str(exc).startswith('output path is inside protected system tree'):
                raise
            # relative_to failed: this root does not contain the candidate.
            continue

    try:
        info = path.lstat()
        exists = True
    except FileNotFoundError:
        info = None
        exists = False
    if exists:
        if info is None or not stat.S_ISREG(info.st_mode):
            raise ValueError('output exists but is not a regular file (device nodes, directories, and symlinks are refused)')
        if not force:
            raise ValueError('output exists; specify --force to replace this regular image file')
        flags = os.O_WRONLY | getattr(os, 'O_NOFOLLOW', 0)
        # Do not use O_TRUNC until after fstat proves the opened object is a regular file.
        fd = os.open(path, flags)
        try:
            opened = os.fstat(fd)
            if not stat.S_ISREG(opened.st_mode):
                raise ValueError('opened output is not a regular file')
            os.ftruncate(fd, 0)
            os.lseek(fd, 0, os.SEEK_SET)
        except Exception:
            os.close(fd)
            raise
    else:
        flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, 'O_NOFOLLOW', 0)
        fd = os.open(path, flags, 0o600)
        opened = os.fstat(fd)
        if not stat.S_ISREG(opened.st_mode):
            os.close(fd)
            try:
                path.unlink()
            except OSError:
                pass
            raise ValueError('created output is not a regular file')
    return os.fdopen(fd, 'wb')


def partition_entry(type_guid: bytes, unique_guid: bytes, first_lba: int,
                    last_lba: int, name: str) -> bytes:
    if len(type_guid) != 16 or len(unique_guid) != 16 or first_lba > last_lba:
        raise ValueError('invalid GPT partition parameters')
    entry = bytearray(GPT_ENTRY_SIZE)
    entry[0:16] = type_guid
    entry[16:32] = unique_guid
    struct.pack_into('<QQQ', entry, 32, first_lba, last_lba, 0)
    entry[56:128] = name[:36].encode('utf-16le').ljust(72, b'\0')
    return bytes(entry)


def gpt_header(current_lba: int, backup_lba: int, first_usable: int,
               last_usable: int, entries_lba: int, entries_crc: int,
               disk_guid: bytes, total_sectors: int) -> bytes:
    header = bytearray(SECTOR)
    header[:8] = b'EFI PART'
    struct.pack_into('<IIII', header, 8, 0x00010000, 92, 0, 0)
    struct.pack_into('<QQQQ', header, 24, current_lba, backup_lba, first_usable, last_usable)
    header[56:72] = disk_guid
    struct.pack_into('<QIII', header, 72, entries_lba, GPT_ENTRIES, GPT_ENTRY_SIZE, entries_crc)
    struct.pack_into('<I', header, 16, crc32(header[:92]))
    return bytes(header)


def make_gpt(total_sectors: int, esp_first: int, esp_sectors: int,
             data_first: int) -> tuple[bytes, bytes, bytes, bytes, int, int]:
    if total_sectors < 100000:
        raise ValueError('disk too small for requested 64 MiB FAT32 ESP')
    first_usable = 2 + GPT_ENTRY_BYTES // SECTOR
    last_usable = total_sectors - 2 - GPT_ENTRY_BYTES // SECTOR
    esp_last = esp_first + esp_sectors - 1
    data_last = last_usable
    if esp_first < first_usable or esp_last >= data_first or data_first > data_last:
        raise ValueError('partition geometry is invalid')
    disk_guid = uuid.uuid5(NAMESPACE, f'CHOS disk {total_sectors} sectors').bytes_le
    esp_unique = uuid.uuid5(NAMESPACE, f'CHOS EFI {total_sectors}').bytes_le
    data_unique = uuid.uuid5(NAMESPACE, f'CHOS DATA {total_sectors}').bytes_le
    entries = bytearray(GPT_ENTRY_BYTES)
    entries[:GPT_ENTRY_SIZE] = partition_entry(EFI_GUID, esp_unique, esp_first, esp_last, 'CHOS EFI')
    entries[GPT_ENTRY_SIZE:2*GPT_ENTRY_SIZE] = partition_entry(CHOS_GUID, data_unique, data_first, data_last, 'CHOS Data')
    ent_crc = crc32(entries)
    backup_entries_lba = total_sectors - 1 - GPT_ENTRY_BYTES // SECTOR
    primary = gpt_header(1, total_sectors-1, first_usable, last_usable, 2, ent_crc, disk_guid, total_sectors)
    backup = gpt_header(total_sectors-1, 1, first_usable, last_usable,
                        backup_entries_lba, ent_crc, disk_guid, total_sectors)
    mbr = bytearray(SECTOR)
    p = 446
    mbr[p+4] = 0xee
    struct.pack_into('<II', mbr, p+8, 1, min(total_sectors-1, 0xffffffff))
    mbr[510:512] = b'\x55\xaa'
    return bytes(mbr), primary, bytes(entries), backup, backup_entries_lba, data_last


def verify_image(path: Path) -> dict[str, int | str]:
    total_bytes = path.stat().st_size
    if total_bytes % SECTOR:
        raise ValueError('image size is not sector-aligned')
    total_sectors = total_bytes // SECTOR
    with path.open('rb') as f:
        mbr = f.read(SECTOR)
        if mbr[510:512] != b'\x55\xaa' or mbr[450] != 0xee:
            raise ValueError('protective MBR invalid')
        f.seek(SECTOR)
        header = bytearray(f.read(SECTOR))
        if header[:8] != b'EFI PART':
            raise ValueError('primary GPT signature invalid')
        found = struct.unpack_from('<I', header, 16)[0]
        header_size = struct.unpack_from('<I', header, 12)[0]
        struct.pack_into('<I', header, 16, 0)
        if crc32(header[:header_size]) != found:
            raise ValueError('primary GPT header CRC invalid')
        entries_lba, count, entry_size, ent_crc = struct.unpack_from('<QIII', header, 72)
        if count != GPT_ENTRIES or entry_size != GPT_ENTRY_SIZE:
            raise ValueError('unexpected GPT entry geometry')
        f.seek(entries_lba * SECTOR)
        entries = f.read(count * entry_size)
        if len(entries) != count * entry_size or crc32(entries) != ent_crc:
            raise ValueError('primary GPT entry CRC invalid')
        esp_first, esp_last = struct.unpack_from('<QQ', entries, 32)
        data_first, data_last = struct.unpack_from('<QQ', entries, GPT_ENTRY_SIZE + 32)
        f.seek(esp_first * SECTOR)
        boot = f.read(SECTOR)
        if boot[510:512] != b'\x55\xaa' or boot[82:87] != b'FAT32':
            raise ValueError('EFI partition is not FAT32')
        f.seek(data_first * SECTOR)
        superblock = f.read(SECTOR)
        if superblock[:4] != b'CHFS':
            raise ValueError('CHOS data partition is not formatted as CHFS')
        expected_crc = struct.unpack_from('<I', superblock, 508)[0]
        temp = bytearray(superblock); struct.pack_into('<I', temp, 508, 0)
        if crc32(temp) != expected_crc:
            raise ValueError('CHFS superblock CRC invalid')
        f.seek((total_sectors - 1) * SECTOR)
        backup_hdr = bytearray(f.read(SECTOR))
        backup_crc = struct.unpack_from('<I', backup_hdr, 16)[0]
        backup_size = struct.unpack_from('<I', backup_hdr, 12)[0]
        backup_entries_lba = struct.unpack_from('<Q', backup_hdr, 72)[0]
        backup_entries_count, backup_entry_size, backup_entries_crc = struct.unpack_from('<III', backup_hdr, 80)
        struct.pack_into('<I', backup_hdr, 16, 0)
        if (backup_hdr[:8] != b'EFI PART' or backup_size < 92 or backup_size > SECTOR
                or crc32(backup_hdr[:backup_size]) != backup_crc):
            raise ValueError('backup GPT header invalid')
        if backup_entries_count != GPT_ENTRIES or backup_entry_size != GPT_ENTRY_SIZE:
            raise ValueError('backup GPT entry geometry invalid')
        f.seek(backup_entries_lba * SECTOR)
        backup_entries_data = f.read(backup_entries_count * backup_entry_size)
        if len(backup_entries_data) != len(entries) or crc32(backup_entries_data) != backup_entries_crc:
            raise ValueError('backup GPT entry-array CRC invalid')
        if backup_entries_data != entries:
            raise ValueError('primary and backup GPT entry arrays differ')
    return {'size_bytes': total_bytes, 'sectors': total_sectors,
            'esp_first_lba': esp_first, 'esp_last_lba': esp_last,
            'data_first_lba': data_first, 'data_last_lba': data_last}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--kernel', required=True, type=Path, help='UEFI ET_DYN kernel ELF')
    ap.add_argument('--efi', required=True, type=Path, help='AArch64 BOOTAA64.EFI')
    ap.add_argument('--output', required=True, type=Path, help='output disk image path (never a raw device)')
    ap.add_argument('--size-mib', type=int, default=256, help='disk image size in MiB (default: 256)')
    ap.add_argument('--readme', type=Path, help='copy release notes into the ESP')
    ap.add_argument('--app', action='append', default=[], metavar='DEST=FILE',
                    help='copy a CHOS app/data file into CHFS partition; may repeat')
    ap.add_argument('--force', action='store_true', help='replace an existing output image')
    args = ap.parse_args()
    # The output path must name a regular image file. In particular, --force
    # must never follow a symlink or truncate a block/character device.
    if not args.kernel.is_file() or not args.efi.is_file():
        ap.error('kernel and EFI loader inputs must exist')
    if args.size_mib < 96:
        ap.error('minimum disk image size is 96 MiB')
    kernel, efi = args.kernel.read_bytes(), args.efi.read_bytes()
    mkuefi_iso.validate_kernel_elf(kernel)
    if len(efi) < 512 or efi[:2] != b'MZ':
        ap.error('EFI loader must be a PE/COFF image')
    pe_off = struct.unpack_from('<I', efi, 0x3c)[0]
    if pe_off + 6 > len(efi) or efi[pe_off:pe_off+4] != b'PE\0\0' or struct.unpack_from('<H', efi, pe_off+4)[0] != 0xaa64:
        ap.error('EFI loader must target AArch64 PE/COFF')
    total_sectors = args.size_mib * 1024 * 1024 // SECTOR
    esp_sectors = ESP_MIB * 1024 * 1024 // SECTOR
    esp_first = ALIGN_SECTORS
    data_first = align_up(esp_first + esp_sectors, ALIGN_SECTORS)
    mbr, primary, entries, backup, backup_entries_lba, data_last = make_gpt(total_sectors, esp_first, esp_sectors, data_first)
    readme = args.readme.read_bytes() if args.readme and args.readme.is_file() else b'CHOS installed system image; experimental 0.30.0.\r\n'
    fat = Fat32Builder(esp_sectors)
    fat.add_file('EFI/BOOT/BOOTAA64.EFI', efi)
    fat.add_file('EFI/CHOS/CHOS.ELF', kernel)
    fat.add_file('EFI/CHOS/README.TXT', readme)
    esp = fat.build()
    data_sectors = data_last - data_first + 1
    chfs_image = mkchfs.format_image(data_sectors)
    for spec in args.app:
        if '=' not in spec:
            ap.error('--app requires /CHFS/PATH=HOST_FILE')
        dest, src = spec.split('=', 1)
        source = Path(src)
        if not dest.startswith('/') or not source.is_file():
            ap.error('--app destination must be an absolute CHFS path and source must exist')
        mkchfs.put(chfs_image, dest, source.read_bytes())
    backup_entries = entries
    with open_safe_image_output(args.output, args.force) as out:
        out.truncate(total_sectors * SECTOR)
        def write_at(offset: int, blob: bytes | bytearray) -> None:
            out.seek(offset); out.write(blob)
        write_at(0, mbr)
        write_at(SECTOR, primary)
        write_at(2 * SECTOR, entries)
        write_at(esp_first * SECTOR, esp)
        write_at(data_first * SECTOR, chfs_image)
        write_at(backup_entries_lba * SECTOR, backup_entries)
        write_at((total_sectors - 1) * SECTOR, backup)
    info = verify_image(args.output)
    print(f'Created installed CHOS disk image: {args.output} ({info["size_bytes"]} bytes)')
    print(f'FAT32 ESP: LBA {info["esp_first_lba"]}..{info["esp_last_lba"]}; CHFS: LBA {info["data_first_lba"]}..{info["data_last_lba"]}')
    print('Validated protective MBR, primary/backup GPT, FAT32 ESP, AArch64 EFI boot files, and CHFS superblock.')
    print('This tool writes only the specified image file, never a physical block device.')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, struct.error) as exc:
        print(f'mkchos_install_disk: error: {exc}', file=sys.stderr)
        raise SystemExit(1)

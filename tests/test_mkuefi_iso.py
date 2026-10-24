#!/usr/bin/env python3
"""Structural tests for CHOS's pure-Python ARM64 UEFI ISO builder."""
from __future__ import annotations
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools import mkuefi_iso as iso  # noqa: E402


def fat_find(data: bytes, name: str, directory_cluster: int | None = None) -> tuple[int, int, int]:
    bps = struct.unpack_from('<H', data, 11)[0]
    spc = data[13]
    reserved = struct.unpack_from('<H', data, 14)[0]
    fats = data[16]
    roots = struct.unpack_from('<H', data, 17)[0]
    spf = struct.unpack_from('<H', data, 22)[0]
    root_secs = (roots * 32 + bps - 1) // bps
    data_start = reserved + fats * spf + root_secs
    if directory_cluster is None:
        offset = (reserved + fats * spf) * bps
        raw = data[offset:offset + root_secs * bps]
    else:
        offset = (data_start + (directory_cluster - 2) * spc) * bps
        raw = data[offset:offset + spc * bps]
    target = iso.short_name(name)
    for pos in range(0, len(raw), 32):
        ent = raw[pos:pos + 32]
        if len(ent) < 32 or ent[0] == 0:
            break
        if ent[0] == 0xe5 or ent[11] == 0x0f or ent[0] == ord('.'):
            continue
        if ent[:11] == target:
            cluster = struct.unpack_from('<H', ent, 26)[0]
            size = struct.unpack_from('<I', ent, 28)[0]
            return cluster, size, ent[11]
    raise AssertionError(f'FAT entry not found: {name}')


def read_fat_file(data: bytes, cluster: int, size: int) -> bytes:
    bps = struct.unpack_from('<H', data, 11)[0]
    spc = data[13]
    reserved = struct.unpack_from('<H', data, 14)[0]
    fats = data[16]
    roots = struct.unpack_from('<H', data, 17)[0]
    spf = struct.unpack_from('<H', data, 22)[0]
    root_secs = (roots * 32 + bps - 1) // bps
    data_start = reserved + fats * spf + root_secs
    parts = bytearray()
    seen = set()
    while cluster < 0xfff8 and len(parts) < size:
        if cluster < 2 or cluster in seen:
            raise AssertionError('invalid/cyclic FAT cluster chain')
        seen.add(cluster)
        offset = (data_start + (cluster - 2) * spc) * bps
        parts.extend(data[offset:offset + spc * bps])
        fat_offset = reserved * bps + cluster * 2
        cluster = struct.unpack_from('<H', data, fat_offset)[0]
    assert len(parts) >= size
    return bytes(parts[:size])


def fake_elf() -> bytes:
    # Minimal ELF64 AArch64 PIE with one executable PT_LOAD and a file-backed
    # PT_DYNAMIC segment contained in it. Relocation semantics are tested by
    # test_uefi_kernel.py against the real kernel build.
    data = bytearray(0x200)
    data[:7] = b'\x7fELF\x02\x01\x01'
    struct.pack_into('<HHI', data, 16, 3, 183, 1)
    struct.pack_into('<Q', data, 24, 0x40200000)
    struct.pack_into('<Q', data, 32, 64)
    struct.pack_into('<HHHHH', data, 52, 64, 56, 2, 0, 0)
    # PT_LOAD: file offsets 0x100..0x200 map to 0x40200000..0x40200100.
    struct.pack_into('<IIQQQQQQ', data, 64, 1, 5, 0x100, 0x40200000,
                     0x40200000, 0x100, 0x100, 0x100)
    # PT_DYNAMIC contained in PT_LOAD; one DT_NULL entry terminates it.
    struct.pack_into('<IIQQQQQQ', data, 120, 2, 6, 0x120, 0x40200020,
                     0x40200020, 0x20, 0x20, 8)
    struct.pack_into('<QQ', data, 0x120, 0, 0)
    return bytes(data)


def fake_efi() -> bytes:
    data = bytearray(512)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<H', data, 0x84, 0xaa64)
    struct.pack_into('<H', data, 0x94, 0xf0)
    struct.pack_into('<H', data, 0x98, 0x20b)
    struct.pack_into('<H', data, 0x98 + 68, 10)
    struct.pack_into('<I', data, 0x98 + 108, 16)
    struct.pack_into('<II', data, 0x98 + 112 + 5 * 8, 0x4000, 12)
    return bytes(data)


def main() -> None:
    loader = fake_efi()
    kernel = fake_elf()
    iso.validate_kernel_elf(kernel)
    wrong_arch = bytearray(kernel)
    struct.pack_into('<H', wrong_arch, 18, 62)  # EM_X86_64 must not package.
    try:
        iso.validate_kernel_elf(bytes(wrong_arch))
    except ValueError as exc:
        assert 'AArch64 PIE' in str(exc)
    else:
        raise AssertionError('ISO builder accepted a kernel for the wrong architecture')
    non_pie = bytearray(kernel)
    struct.pack_into('<H', non_pie, 16, 2)  # ET_EXEC must not package.
    try:
        iso.validate_kernel_elf(bytes(non_pie))
    except ValueError as exc:
        assert 'AArch64 PIE' in str(exc)
    else:
        raise AssertionError('ISO builder accepted a fixed-address ET_EXEC kernel')
    fat = iso.Fat16Builder(sectors=32768)
    fat.add_file('EFI/BOOT/BOOTAA64.EFI', loader)
    fat.add_file('EFI/CHOS/CHOS.ELF', kernel)
    esp = fat.build()
    assert esp[510:512] == b'\x55\xaa'
    root_efi, _, attr = fat_find(esp, 'EFI')
    assert attr & 0x10
    boot_dir, _, attr = fat_find(esp, 'BOOT', root_efi)
    assert attr & 0x10
    app_cluster, app_size, attr = fat_find(esp, 'BOOTAA64.EFI', boot_dir)
    assert attr & 0x20
    assert read_fat_file(esp, app_cluster, app_size) == loader
    chos_dir, _, _ = fat_find(esp, 'CHOS', root_efi)
    kernel_cluster, kernel_size, _ = fat_find(esp, 'CHOS.ELF', chos_dir)
    assert read_fat_file(esp, kernel_cluster, kernel_size) == kernel

    disk = iso.build_iso(esp)
    iso.validate_iso(disk)
    pvd = disk[16 * iso.SECTOR:17 * iso.SECTOR]
    assert pvd[1:6] == b'CD001'
    assert pvd[40:49] == b'CHOS_0300'
    br = disk[17 * iso.SECTOR:18 * iso.SECTOR]
    cat_lba = struct.unpack_from('<I', br, 71)[0]
    cat = disk[cat_lba * iso.SECTOR:(cat_lba + 1) * iso.SECTOR]
    assert sum(struct.unpack('<16H', cat[:32])) & 0xffff == 0
    assert cat[1] == 0xef
    # The EFI default entry starts immediately after the validation entry.
    assert cat[32] == 0x88 and cat[33] == 0
    assert struct.unpack_from('<H', cat, 38)[0] == 0
    esp_lba = struct.unpack_from('<I', cat, 40)[0]
    assert esp_lba * iso.SECTOR < len(disk)
    assert len(disk) - esp_lba * iso.SECTOR == len(esp)
    print('UEFI ISO: FAT16 boot path, kernel file, PE/COFF architecture, and El Torito EFI catalog passed')


if __name__ == '__main__':
    main()

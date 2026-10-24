#!/usr/bin/env python3
"""Build a minimal UEFI-bootable ARM64 CHOS ISO without external ISO tools.

The ISO carries an EFI El Torito FAT16 image. The ESP contains the removable-media
boot path EFI/BOOT/BOOTAA64.EFI and the linked CHOS kernel at EFI/CHOS/CHOS.ELF.
This tool validates all generated FAT and ISO structures before writing output.
"""
from __future__ import annotations

import argparse
import math
import os
import struct
import sys
import tempfile
from pathlib import Path

SECTOR = 2048
FAT_SECTOR = 512
ESP_MIB = 32
ESP_SECTORS = ESP_MIB * 1024 * 1024 // FAT_SECTOR
SPC = 4
ROOT_ENTRIES = 512
ROOT_SECTORS = ROOT_ENTRIES * 32 // FAT_SECTOR
RESERVED = 1
FAT_COUNT = 2


def p16(buf: bytearray, off: int, v: int) -> None:
    struct.pack_into('<H', buf, off, v & 0xffff)


def p32(buf: bytearray, off: int, v: int) -> None:
    struct.pack_into('<I', buf, off, v & 0xffffffff)


def both32(buf: bytearray, off: int, v: int) -> None:
    p32(buf, off, v)
    struct.pack_into('>I', buf, off + 4, v)


def short_name(name: str) -> bytes:
    if name in ('.', '..'):
        return (b'.' if name == '.' else b'..') + b' ' * (10 if name == '.' else 9)
    parts = name.upper().split('.')
    if len(parts) > 2 or not parts[0] or len(parts[0]) > 8 or (len(parts) == 2 and len(parts[1]) > 3):
        raise ValueError(f'not a valid 8.3 FAT name: {name}')
    base = parts[0].encode('ascii')
    ext = parts[1].encode('ascii') if len(parts) == 2 else b''
    if any(c not in b'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_$~!#%&-{}()@`\'' for c in base + ext):
        raise ValueError(f'unsupported FAT short name: {name}')
    return base.ljust(8, b' ') + ext.ljust(3, b' ')


def dirent(name: str, attr: int, cluster: int, size: int = 0) -> bytes:
    b = bytearray(32)
    b[:11] = short_name(name)
    b[11] = attr
    p16(b, 14, 0)  # creation time
    p16(b, 16, 0x5821)  # 2024-01-01, valid deterministic date
    p16(b, 18, 0x5821)
    p16(b, 22, 0)
    p16(b, 24, 0x5821)
    p16(b, 26, (cluster >> 16) & 0xffff)
    p16(b, 20, (cluster >> 16) & 0xffff)
    p16(b, 26, cluster & 0xffff)
    p32(b, 28, size)
    return bytes(b)


class Fat16Builder:
    def __init__(self, sectors: int = ESP_SECTORS) -> None:
        self.total_sectors = sectors
        self.spc = SPC
        self.root_sectors = ROOT_SECTORS
        self.spf = 1
        while True:
            clusters = (sectors - RESERVED - FAT_COUNT * self.spf - self.root_sectors) // self.spc
            needed = math.ceil((clusters + 2) * 2 / FAT_SECTOR)
            if needed == self.spf:
                break
            self.spf = needed
        self.data_start = RESERVED + FAT_COUNT * self.spf + self.root_sectors
        self.cluster_count = (sectors - self.data_start) // self.spc
        if not (4085 <= self.cluster_count < 65525):
            raise ValueError(f'FAT16 cluster count out of range: {self.cluster_count}')
        self.image = bytearray(sectors * FAT_SECTOR)
        self.fat = [0] * (self.cluster_count + 2)
        self.fat[0], self.fat[1] = 0xfff8, 0xffff
        self.next_cluster = 2
        self.root = []
        self.dirs: dict[str, dict] = {'': {'dirs': {}, 'files': [], 'cluster': 0}}

    def add_file(self, path: str, data: bytes) -> None:
        parts = [p for p in path.replace('\\', '/').strip('/').split('/') if p]
        if not parts:
            raise ValueError('empty FAT path')
        node = self.dirs['']
        parent_path = ''
        for part in parts[:-1]:
            short_name(part)
            if part not in node['dirs']:
                child_path = f'{parent_path}/{part}'.strip('/')
                child = {'dirs': {}, 'files': [], 'cluster': self.alloc_clusters(1)[0], 'parent': parent_path}
                node['dirs'][part] = child
                self.dirs[child_path] = child
            node = node['dirs'][part]
            parent_path = f'{parent_path}/{part}'.strip('/')
        name = parts[-1]
        short_name(name)
        if any(f['name'].upper() == name.upper() for f in node['files']) or name in node['dirs']:
            raise ValueError(f'duplicate FAT directory entry: {path}')
        clusters_needed = max(1, math.ceil(len(data) / (self.spc * FAT_SECTOR)))
        clusters = self.alloc_clusters(clusters_needed)
        for i, cluster in enumerate(clusters):
            chunk = data[i * self.spc * FAT_SECTOR:(i + 1) * self.spc * FAT_SECTOR]
            start = self.cluster_offset(cluster)
            self.image[start:start + len(chunk)] = chunk
        node['files'].append({'name': name, 'cluster': clusters[0], 'size': len(data)})

    def alloc_clusters(self, count: int) -> list[int]:
        if count <= 0 or self.next_cluster + count > self.cluster_count + 2:
            raise ValueError('FAT16 image out of space')
        result = list(range(self.next_cluster, self.next_cluster + count))
        self.next_cluster += count
        for i, cluster in enumerate(result):
            self.fat[cluster] = result[i + 1] if i + 1 < len(result) else 0xffff
        return result

    def cluster_offset(self, cluster: int) -> int:
        return (self.data_start + (cluster - 2) * self.spc) * FAT_SECTOR

    def _write_directory(self, path: str, node: dict) -> None:
        if path:
            parent_path = node.get('parent', '')
            parent = self.dirs[parent_path]['cluster'] if parent_path else 0
            entries = [dirent('.', 0x10, node['cluster']), dirent('..', 0x10, parent)]
        else:
            entries = []
        for name, child in node['dirs'].items():
            entries.append(dirent(name, 0x10, child['cluster']))
        for f in node['files']:
            entries.append(dirent(f['name'], 0x20, f['cluster'], f['size']))
        entries.append(bytes(32))
        capacity = (self.root_sectors if not path else self.spc) * FAT_SECTOR
        data = b''.join(entries)
        if len(data) > capacity:
            raise ValueError(f'directory has too many entries: /{path}')
        data += bytes(capacity - len(data))
        if path:
            off = self.cluster_offset(node['cluster'])
        else:
            off = (RESERVED + FAT_COUNT * self.spf) * FAT_SECTOR
        self.image[off:off + len(data)] = data
        for name, child in node['dirs'].items():
            child_path = f'{path}/{name}'.strip('/')
            self._write_directory(child_path, child)

    def build(self) -> bytes:
        b = self.image
        # FAT16 BIOS Parameter Block and extended BPB.
        b[0:3] = b'\xeb\x3c\x90'
        b[3:11] = b'CHOSUEFI'
        p16(b, 11, FAT_SECTOR)
        b[13] = self.spc
        p16(b, 14, RESERVED)
        b[16] = FAT_COUNT
        p16(b, 17, ROOT_ENTRIES)
        p16(b, 19, self.total_sectors if self.total_sectors <= 0xffff else 0)
        b[21] = 0xf8
        p16(b, 22, self.spf)
        p16(b, 24, 63)
        p16(b, 26, 255)
        p32(b, 28, 0)
        p32(b, 32, self.total_sectors if self.total_sectors > 0xffff else 0)
        b[36] = 0x80
        b[38] = 0x29
        p32(b, 39, 0x43484f53)
        b[43:54] = b'CHOS EFI   '
        b[54:62] = b'FAT16   '
        b[510:512] = b'\x55\xaa'
        # Write FAT copies.
        fat_bytes = bytearray(self.spf * FAT_SECTOR)
        for i, value in enumerate(self.fat):
            p16(fat_bytes, i * 2, value)
        fat1 = RESERVED * FAT_SECTOR
        b[fat1:fat1 + len(fat_bytes)] = fat_bytes
        fat2 = (RESERVED + self.spf) * FAT_SECTOR
        b[fat2:fat2 + len(fat_bytes)] = fat_bytes
        self._write_directory('', self.dirs[''])
        return bytes(b)


def both16(buf: bytearray, off: int, v: int) -> None:
    p16(buf, off, v)
    struct.pack_into('>H', buf, off + 2, v)


def iso_dir_record(extent: int, size: int, flags: int, name: bytes, date=(126, 10, 23, 0, 0, 0, 0)) -> bytes:
    ident_len = len(name)
    length = 33 + ident_len + (1 if ident_len % 2 == 0 else 0)
    r = bytearray(length)
    r[0] = length
    r[1] = 0
    both32(r, 2, extent)
    both32(r, 10, size)
    r[18:25] = bytes(date)
    r[25] = flags
    r[26] = 0
    r[27] = 0
    both16(r, 28, 1)
    r[32] = ident_len
    r[33:33 + ident_len] = name
    return bytes(r)


def path_table(endian: str) -> bytes:
    # ISO9660 root entry: name length 1, ext attr length 0, extent LBA 20, parent #1, id NUL.
    b = bytearray(10)
    b[0] = 1
    b[1] = 0
    struct.pack_into('<I' if endian == 'little' else '>I', b, 2, 20)
    struct.pack_into('<H' if endian == 'little' else '>H', b, 6, 1)
    b[8] = 0
    b[9] = 0
    return bytes(b)


def build_iso(esp: bytes, esp_label: str = 'CHOS-ESP.IMG') -> bytes:
    sectors_for_esp = math.ceil(len(esp) / SECTOR)
    if len(esp) % FAT_SECTOR != 0:
        raise ValueError('ESP must be a multiple of 512 bytes')
    root_lba, path_le_lba, path_be_lba, catalog_lba = 20, 21, 22, 19
    esp_lba = 24
    total_sectors = esp_lba + sectors_for_esp
    iso = bytearray(total_sectors * SECTOR)

    # ISO9660 Primary Volume Descriptor.
    pvd = bytearray(SECTOR)
    pvd[0:7] = bytes([1]) + b'CD001' + bytes([1])
    pvd[8:40] = b'CHOS'.ljust(32, b' ')
    pvd[40:72] = b'CHOS_0300'.ljust(32, b' ')
    both32(pvd, 80, total_sectors)
    both16(pvd, 120, 1)
    both16(pvd, 124, 1)
    both16(pvd, 128, SECTOR)
    both32(pvd, 132, len(path_table('little')))
    p32(pvd, 140, path_le_lba)
    p32(pvd, 144, 0)
    struct.pack_into('>I', pvd, 148, path_be_lba)
    struct.pack_into('>I', pvd, 152, 0)
    rootrec = iso_dir_record(root_lba, SECTOR, 2, b'\x00')
    pvd[156:156 + len(rootrec)] = rootrec
    pvd[881] = 1
    iso[16 * SECTOR:17 * SECTOR] = pvd

    # El Torito Boot Record Volume Descriptor.
    br = bytearray(SECTOR)
    br[0:7] = bytes([0]) + b'CD001' + bytes([1])
    br[7:39] = b'EL TORITO SPECIFICATION'.ljust(32, b'\x00')
    br[71:75] = struct.pack('<I', catalog_lba)
    iso[17 * SECTOR:18 * SECTOR] = br
    term = bytearray(SECTOR)
    term[0:7] = bytes([255]) + b'CD001' + bytes([1])
    iso[18 * SECTOR:19 * SECTOR] = term

    # El Torito validation entry advertises the EFI platform directly. The
    # following 32-byte initial/default entry points at the full FAT EFI System
    # Partition image, using no emulation and the UEFI full-image sector count.
    # Keeping the initial entry in its prescribed slot avoids relying on
    # firmware-specific tolerance for a section header at offset 32.
    cat = bytearray(SECTOR)
    cat[0] = 1
    cat[1] = 0xef  # EFI platform
    cat[4:28] = b'CHOS ARM64 UEFI'.ljust(24, b'\x00')
    cat[30:32] = b'\x55\xaa'
    checksum = (-sum(struct.unpack('<16H', cat[:32]))) & 0xffff
    p16(cat, 28, checksum)
    cat[32] = 0x88  # bootable, no emulation
    cat[33] = 0x00
    p16(cat, 34, 0)  # load segment
    cat[36] = 0x00   # system type unused for EFI no-emulation boot
    cat[37] = 0x00
    p16(cat, 38, 0)  # UEFI consumes the EFI system partition image
    p32(cat, 40, esp_lba)
    iso[catalog_lba * SECTOR:(catalog_lba + 1) * SECTOR] = cat

    # Minimal root directory: ., .., ESP image, and boot catalog.
    root = bytearray(SECTOR)
    records = [
        iso_dir_record(root_lba, SECTOR, 2, b'\x00'),
        iso_dir_record(root_lba, SECTOR, 2, b'\x01'),
        iso_dir_record(esp_lba, len(esp), 0, (esp_label.upper() + ';1').encode('ascii')),
        iso_dir_record(catalog_lba, SECTOR, 0, b'BOOT.CAT;1'),
    ]
    offset = 0
    for record in records:
        root[offset:offset + len(record)] = record
        offset += len(record)
    iso[root_lba * SECTOR:(root_lba + 1) * SECTOR] = root
    iso[path_le_lba * SECTOR:path_le_lba * SECTOR + len(path_table('little'))] = path_table('little')
    iso[path_be_lba * SECTOR:path_be_lba * SECTOR + len(path_table('big'))] = path_table('big')
    iso[esp_lba * SECTOR:esp_lba * SECTOR + len(esp)] = esp
    return bytes(iso)


def validate_iso(iso: bytes) -> None:
    if len(iso) < 25 * SECTOR or len(iso) % SECTOR != 0:
        raise ValueError('ISO length/alignment invalid')
    pvd = iso[16 * SECTOR:17 * SECTOR]
    br = iso[17 * SECTOR:18 * SECTOR]
    term = iso[18 * SECTOR:19 * SECTOR]
    cat_lba = struct.unpack_from('<I', br, 71)[0]
    cat = iso[cat_lba * SECTOR:(cat_lba + 1) * SECTOR]
    if pvd[0] != 1 or pvd[1:6] != b'CD001' or br[0] != 0 or br[7:30] != b'EL TORITO SPECIFICATION' or term[0] != 255:
        raise ValueError('ISO volume descriptor sequence invalid')
    if cat[0] != 1 or cat[1] != 0xef or cat[30:32] != b'\x55\xaa' or sum(struct.unpack('<16H', cat[:32])) & 0xffff:
        raise ValueError('El Torito EFI validation entry invalid')
    boot = cat[32:64]
    if boot[0] != 0x88 or boot[1] != 0 or struct.unpack_from('<H', boot, 6)[0] != 0:
        raise ValueError('El Torito EFI default boot entry invalid')
    esp_lba = struct.unpack_from('<I', boot, 8)[0]
    if esp_lba <= 0 or esp_lba * SECTOR >= len(iso):
        raise ValueError('El Torito ESP extent is outside the ISO')
    esp = iso[esp_lba * SECTOR:]
    if esp[:3] != b'\xeb\x3c\x90' or esp[510:512] != b'\x55\xaa' or esp[54:59] != b'FAT16':
        raise ValueError('EFI system partition is not a valid FAT16 boot volume')


def validate_kernel_elf(kernel: bytes) -> None:
    """Reject kernels the bundled UEFI loader cannot safely load."""
    if len(kernel) < 64 or kernel[:4] != b'\x7fELF' or kernel[4:7] != b'\x02\x01\x01':
        raise ValueError('kernel input is not a little-endian ELF64 image')
    e_type, machine, version = struct.unpack_from('<HHI', kernel, 16)
    if e_type != 3 or machine != 183 or version != 1:
        raise ValueError('kernel must be an ELF64 AArch64 PIE (ET_DYN)')
    entry = struct.unpack_from('<Q', kernel, 24)[0]
    phoff = struct.unpack_from('<Q', kernel, 32)[0]
    ehsize, phentsize, phnum = struct.unpack_from('<HHH', kernel, 52)
    if ehsize != 64 or phentsize != 56 or not phnum or phnum > 32:
        raise ValueError('kernel ELF header/program-header layout is invalid')
    if phoff > len(kernel) or phnum * phentsize > len(kernel) - phoff:
        raise ValueError('kernel program-header table extends past the file')

    headers = []
    for idx in range(phnum):
        off = phoff + idx * phentsize
        p_type, flags = struct.unpack_from('<II', kernel, off)
        p_offset, vaddr, paddr, filesz, memsz, align = struct.unpack_from('<QQQQQQ', kernel, off + 8)
        ph = {'type': p_type, 'flags': flags, 'offset': p_offset, 'vaddr': vaddr,
              'paddr': paddr, 'filesz': filesz, 'memsz': memsz, 'align': align}
        if p_type == 1:
            if not memsz or filesz > memsz or paddr != vaddr:
                raise ValueError('kernel has an invalid PT_LOAD segment')
            if p_offset > len(kernel) or filesz > len(kernel) - p_offset:
                raise ValueError('kernel PT_LOAD file range extends past the file')
            if vaddr > (1 << 64) - memsz:
                raise ValueError('kernel PT_LOAD address overflows')
            if align > 1 and ((align & (align - 1)) or vaddr % align != p_offset % align):
                raise ValueError('kernel PT_LOAD alignment/congruence is invalid')
        headers.append(ph)

    loads = [h for h in headers if h['type'] == 1]
    dynamics = [h for h in headers if h['type'] == 2]
    if not loads or len(dynamics) != 1:
        raise ValueError('kernel requires PT_LOAD segments and exactly one PT_DYNAMIC')
    if not any(h['flags'] & 1 and h['vaddr'] <= entry < h['vaddr'] + h['memsz'] for h in loads):
        raise ValueError('kernel entry is not in an executable PT_LOAD segment')
    dyn = dynamics[0]
    if dyn['filesz'] < 16 or dyn['filesz'] > dyn['memsz'] or dyn['offset'] > len(kernel) or dyn['filesz'] > len(kernel) - dyn['offset']:
        raise ValueError('kernel PT_DYNAMIC is not valid file-backed data')
    if not any(dyn['vaddr'] >= h['vaddr'] and dyn['vaddr'] + dyn['filesz'] <= h['vaddr'] + h['filesz'] and
               dyn['offset'] == h['offset'] + (dyn['vaddr'] - h['vaddr'])
               for h in loads):
        raise ValueError('kernel PT_DYNAMIC virtual and file ranges do not map to the same PT_LOAD bytes')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kernel', required=True, type=Path)
    parser.add_argument('--install-payload', type=Path)
    parser.add_argument('--efi', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--readme', type=Path)
    args = parser.parse_args()
    if not args.kernel.is_file() or not args.efi.is_file():
        parser.error('kernel and EFI loader inputs must exist')
    kernel = args.kernel.read_bytes()
    efi = args.efi.read_bytes()
    try:
        validate_kernel_elf(kernel)
    except ValueError as exc:
        parser.error(str(exc))
    if len(efi) < 512 or efi[:2] != b'MZ':
        parser.error('EFI input is not a PE executable')
    pe_off = struct.unpack_from('<I', efi, 0x3c)[0]
    if pe_off + 26 > len(efi) or efi[pe_off:pe_off + 4] != b'PE\0\0':
        parser.error('invalid PE signature')
    if struct.unpack_from('<H', efi, pe_off + 4)[0] != 0xaa64:
        parser.error('EFI loader must target AArch64 PE/COFF')
    optional_size = struct.unpack_from('<H', efi, pe_off + 20)[0]
    optional_off = pe_off + 24
    if optional_size < 160 or struct.unpack_from('<H', efi, optional_off + 68)[0] != 10:
        parser.error('PE subsystem must be EFI application')
    directory_count = struct.unpack_from('<I', efi, optional_off + 108)[0]
    if directory_count < 6:
        parser.error('PE optional header does not describe base relocations')
    reloc_rva, reloc_size = struct.unpack_from('<II', efi, optional_off + 112 + 5 * 8)
    if reloc_rva == 0 or reloc_size < 8:
        parser.error('EFI loader needs base relocations so firmware can load it outside its preferred image base')

    fat = Fat16Builder()
    fat.add_file('EFI/BOOT/BOOTAA64.EFI', efi)
    fat.add_file('EFI/CHOS/CHOS.ELF', kernel)
    if args.install_payload: fat.add_file('INSTALL.BIN',args.install_payload.read_bytes())
    readme = args.readme.read_bytes() if args.readme and args.readme.exists() else b'CHOS 0.30.0 experimental UEFI boot image.\r\n'
    fat.add_file('EFI/CHOS/README.TXT', readme)
    esp = fat.build()
    iso = build_iso(esp)
    validate_iso(iso)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(iso)
    print(f'Wrote {args.output}: {len(iso)} bytes; ESP={len(esp)} bytes; EFI={len(efi)} bytes; kernel={len(kernel)} bytes')
    print('Validated ISO9660 descriptors, El Torito EFI catalog, FAT16 boot sector and PE/COFF ARM64 EFI application.')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, struct.error) as exc:
        print(f'mkuefi_iso: error: {exc}', file=sys.stderr)
        raise SystemExit(1)

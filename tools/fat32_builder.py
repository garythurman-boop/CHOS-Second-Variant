#!/usr/bin/env python3
"""Small deterministic FAT32 builder for CHOS's installed EFI System Partition."""
from __future__ import annotations
import math
import struct

SECTOR = 512
FAT_COUNT = 2
RESERVED = 32
SPC = 1
ROOT_CLUSTER = 2
EOC = 0x0FFFFFFF


def p16(b: bytearray, off: int, value: int) -> None:
    struct.pack_into('<H', b, off, value & 0xffff)


def p32(b: bytearray, off: int, value: int) -> None:
    struct.pack_into('<I', b, off, value & 0xffffffff)


def short_name(name: str) -> bytes:
    if name in ('.', '..'):
        return (b'.' if name == '.' else b'..') + b' ' * (10 if name == '.' else 9)
    parts = name.upper().split('.')
    if len(parts) > 2 or not parts[0] or len(parts[0]) > 8 or (len(parts) == 2 and len(parts[1]) > 3):
        raise ValueError(f'not a valid FAT 8.3 name: {name}')
    base = parts[0].encode('ascii')
    ext = parts[1].encode('ascii') if len(parts) == 2 else b''
    allowed = b"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_$~!#%&-{}()@`'"
    if any(c not in allowed for c in base + ext):
        raise ValueError(f'unsupported FAT short name: {name}')
    return base.ljust(8, b' ') + ext.ljust(3, b' ')


def dirent(name: str, attr: int, cluster: int, size: int = 0) -> bytes:
    b = bytearray(32)
    b[:11] = short_name(name)
    b[11] = attr
    p16(b, 14, 0)
    p16(b, 16, 0x5821)  # deterministic 2024-01-01
    p16(b, 18, 0x5821)
    p16(b, 22, 0)
    p16(b, 24, 0x5821)
    p16(b, 20, (cluster >> 16) & 0xffff)
    p16(b, 26, cluster & 0xffff)
    p32(b, 28, size)
    return bytes(b)


class Fat32Builder:
    """Bounded FAT32 creator intended for the installer ESP (not general filesystems)."""
    def __init__(self, sectors: int = 64 * 1024 * 1024 // SECTOR) -> None:
        if sectors < (64 * 1024 * 1024 // SECTOR):
            raise ValueError('installed EFI System Partition must be at least 64 MiB for FAT32')
        self.total_sectors = sectors
        self.spc = SPC
        self.reserved = RESERVED
        self.fat_count = FAT_COUNT
        self.spf = 1
        while True:
            clusters = (sectors - self.reserved - self.fat_count * self.spf) // self.spc
            needed = math.ceil((clusters + 2) * 4 / SECTOR)
            if needed <= self.spf:
                break
            self.spf = needed
        self.data_start = self.reserved + self.fat_count * self.spf
        self.cluster_count = (sectors - self.data_start) // self.spc
        if self.cluster_count < 65525 or self.cluster_count + 2 > self.spf * SECTOR // 4:
            raise ValueError(f'FAT32 cluster geometry invalid: {self.cluster_count} clusters')
        self.image = bytearray(sectors * SECTOR)
        self.fat = [0] * (self.cluster_count + 2)
        self.fat[0], self.fat[1], self.fat[ROOT_CLUSTER] = 0x0FFFFFF8, EOC, EOC
        self.next_cluster = ROOT_CLUSTER + 1
        self.dirs: dict[str, dict] = {'': {'dirs': {}, 'files': [], 'cluster': ROOT_CLUSTER, 'parent': ''}}

    def alloc_clusters(self, count: int) -> list[int]:
        if count <= 0 or self.next_cluster + count > self.cluster_count + 2:
            raise ValueError('FAT32 ESP out of space')
        result = list(range(self.next_cluster, self.next_cluster + count))
        self.next_cluster += count
        for i, cluster in enumerate(result):
            self.fat[cluster] = result[i + 1] if i + 1 < len(result) else EOC
        return result

    def cluster_offset(self, cluster: int) -> int:
        return (self.data_start + (cluster - 2) * self.spc) * SECTOR

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
        if name in node['dirs'] or any(f['name'].upper() == name.upper() for f in node['files']):
            raise ValueError(f'duplicate FAT path: {path}')
        clusters_needed = max(1, math.ceil(len(data) / (self.spc * SECTOR)))
        clusters = self.alloc_clusters(clusters_needed)
        for idx, cluster in enumerate(clusters):
            chunk = data[idx * self.spc * SECTOR:(idx + 1) * self.spc * SECTOR]
            off = self.cluster_offset(cluster)
            self.image[off:off + len(chunk)] = chunk
        node['files'].append({'name': name, 'cluster': clusters[0], 'size': len(data)})

    def _write_directory(self, path: str, node: dict) -> None:
        if path:
            parent = self.dirs[node.get('parent', '')]['cluster']
            entries = [dirent('.', 0x10, node['cluster']), dirent('..', 0x10, parent)]
        else:
            entries = []
        entries += [dirent(name, 0x10, child['cluster']) for name, child in node['dirs'].items()]
        entries += [dirent(f['name'], 0x20, f['cluster'], f['size']) for f in node['files']]
        entries.append(bytes(32))
        capacity = self.spc * SECTOR
        raw = b''.join(entries)
        if len(raw) > capacity:
            raise ValueError(f'FAT32 directory has too many entries: /{path}')
        raw = raw.ljust(capacity, b'\0')
        off = self.cluster_offset(node['cluster'])
        self.image[off:off + len(raw)] = raw
        for name, child in node['dirs'].items():
            child_path = f'{path}/{name}'.strip('/')
            self._write_directory(child_path, child)

    def build(self) -> bytes:
        b = self.image
        b[0:3] = b'\xeb\x58\x90'
        b[3:11] = b'CHOSUEFI'
        p16(b, 11, SECTOR)
        b[13] = self.spc
        p16(b, 14, self.reserved)
        b[16] = self.fat_count
        p16(b, 17, 0)
        p16(b, 19, 0)
        b[21] = 0xf8
        p16(b, 22, 0)
        p16(b, 24, 63)
        p16(b, 26, 255)
        p32(b, 28, 0)
        p32(b, 32, self.total_sectors)
        p32(b, 36, self.spf)
        p16(b, 40, 0)  # mirroring enabled
        p16(b, 42, 0)
        p32(b, 44, ROOT_CLUSTER)
        p16(b, 48, 1)  # FSInfo sector
        p16(b, 50, 6)  # backup boot sector
        b[64] = 0x80
        b[66] = 0x29
        p32(b, 67, 0x43484f53)
        b[71:82] = b'CHOS EFI   '
        b[82:90] = b'FAT32   '
        b[510:512] = b'\x55\xaa'
        fsinfo = bytearray(SECTOR)
        p32(fsinfo, 0, 0x41615252)
        p32(fsinfo, 484, 0x61417272)
        p32(fsinfo, 488, self.cluster_count - (self.next_cluster - 3))
        p32(fsinfo, 492, self.next_cluster)
        fsinfo[510:512] = b'\x55\xaa'
        b[SECTOR:2*SECTOR] = fsinfo
        b[6*SECTOR:7*SECTOR] = bytes(b[:SECTOR])
        b[7*SECTOR:8*SECTOR] = fsinfo
        fat_bytes = bytearray(self.spf * SECTOR)
        for i, value in enumerate(self.fat):
            p32(fat_bytes, i * 4, value)
        first_fat = self.reserved * SECTOR
        b[first_fat:first_fat + len(fat_bytes)] = fat_bytes
        second_fat = (self.reserved + self.spf) * SECTOR
        b[second_fat:second_fat + len(fat_bytes)] = fat_bytes
        self._write_directory('', self.dirs[''])
        return bytes(b)

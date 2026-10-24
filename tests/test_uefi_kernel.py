#!/usr/bin/env python3
"""Check that the CHOS kernel is a well-formed relocatable AArch64 UEFI ELF."""
from __future__ import annotations

import struct
import sys
from pathlib import Path

PT_LOAD = 1
PT_DYNAMIC = 2
ET_DYN = 3
EM_AARCH64 = 183
DT_NULL = 0
DT_RELA = 7
DT_RELASZ = 8
DT_RELAENT = 9
R_AARCH64_RELATIVE = 1027
PAGE_SIZE = 4096
MIN_PHYSICAL_BASE = 0x01000000
MAX_PHYSICAL_LIMIT = 0x0000fffffffff000
MAX_IMAGE_SIZE = 256 * 1024 * 1024


def u16(data: bytes, off: int) -> int:
    return struct.unpack_from('<H', data, off)[0]


def u32(data: bytes, off: int) -> int:
    return struct.unpack_from('<I', data, off)[0]


def u64(data: bytes, off: int) -> int:
    return struct.unpack_from('<Q', data, off)[0]


def main() -> int:
    if len(sys.argv) != 2:
        print(f'usage: {sys.argv[0]} KERNEL.ELF', file=sys.stderr)
        return 2
    path = Path(sys.argv[1])
    data = path.read_bytes()
    if len(data) < 64 or data[:4] != b'\x7fELF' or data[4:7] != b'\x02\x01\x01':
        raise SystemExit('kernel is not a little-endian ELF64 image')
    if u16(data, 16) != ET_DYN or u16(data, 18) != EM_AARCH64 or u32(data, 20) != 1:
        raise SystemExit('kernel must be an ELF64 AArch64 PIE (ET_DYN)')
    entry = u64(data, 24)
    phoff = u64(data, 32)
    # Ensure the linked UEFI entry really targets the assembly entry that
    # preserves the boot-info pointer and marks the UEFI path before C startup.
    shoff = u64(data, 40)
    shentsize = u16(data, 58)
    shnum = u16(data, 60)
    if shoff > len(data) or shentsize < 64 or shnum == 0 or shnum > 4096 or shnum * shentsize > len(data) - shoff:
        raise SystemExit('invalid ELF section-header table; cannot validate UEFI entry symbol')
    uefi_entry_symbol = None
    for i in range(shnum):
        sh = shoff + i * shentsize
        sh_type = u32(data, sh + 4)
        if sh_type != 2:  # SHT_SYMTAB
            continue
        sym_off = u64(data, sh + 24)
        sym_size = u64(data, sh + 32)
        str_index = u32(data, sh + 40)
        sym_ent = u64(data, sh + 56)
        if str_index >= shnum or sym_ent < 24 or sym_size % sym_ent:
            raise SystemExit('invalid ELF symbol table metadata')
        if sym_off > len(data) or sym_size > len(data) - sym_off:
            raise SystemExit('ELF symbol table extends beyond file')
        str_sh = shoff + str_index * shentsize
        str_off = u64(data, str_sh + 24)
        str_size = u64(data, str_sh + 32)
        if str_off > len(data) or str_size > len(data) - str_off:
            raise SystemExit('ELF symbol string table extends beyond file')
        strings = data[str_off:str_off + str_size]
        for sym_at in range(sym_off, sym_off + sym_size, sym_ent):
            name_off = u32(data, sym_at)
            if name_off >= len(strings):
                continue
            end = strings.find(b'\0', name_off)
            if end < 0:
                continue
            if strings[name_off:end] == b'chos_uefi_start':
                uefi_entry_symbol = u64(data, sym_at + 8)
                break
        if uefi_entry_symbol is not None:
            break
    if uefi_entry_symbol is None or entry != uefi_entry_symbol:
        raise SystemExit(f'ELF entry 0x{entry:x} does not match chos_uefi_start symbol {uefi_entry_symbol!r}')
    phentsize = u16(data, 54)
    phnum = u16(data, 56)
    if u16(data, 52) != 64 or phentsize != 56 or not phnum or phnum > 32:
        raise SystemExit('invalid ELF header or program-header table')
    if phoff > len(data) or phnum * phentsize > len(data) - phoff:
        raise SystemExit('program-header table extends beyond the file')

    headers = []
    for i in range(phnum):
        off = phoff + i * phentsize
        ph = {
            'type': u32(data, off), 'flags': u32(data, off + 4),
            'offset': u64(data, off + 8), 'vaddr': u64(data, off + 16),
            'paddr': u64(data, off + 24), 'filesz': u64(data, off + 32),
            'memsz': u64(data, off + 40), 'align': u64(data, off + 48),
        }
        if ph['type'] == PT_LOAD:
            if ph['memsz'] == 0 or ph['filesz'] > ph['memsz'] or ph['paddr'] != ph['vaddr']:
                raise SystemExit('invalid PT_LOAD segment size or address')
            if ph['offset'] > len(data) or ph['filesz'] > len(data) - ph['offset']:
                raise SystemExit('PT_LOAD file range extends beyond the ELF')
            align = ph['align']
            if align > 1 and ((align & (align - 1)) or ph['vaddr'] % align != ph['offset'] % align):
                raise SystemExit('PT_LOAD alignment/congruence is invalid')
            if ph['vaddr'] > (1 << 64) - ph['memsz']:
                raise SystemExit('PT_LOAD address overflows')
        headers.append(ph)

    loads = [h for h in headers if h['type'] == PT_LOAD]
    dynamics = [h for h in headers if h['type'] == PT_DYNAMIC]
    if not loads or len(dynamics) != 1:
        raise SystemExit('kernel requires PT_LOAD and exactly one PT_DYNAMIC segment')
    low = min(h['vaddr'] for h in loads) & ~(PAGE_SIZE - 1)
    high_raw = max(h['vaddr'] + h['memsz'] for h in loads)
    high = (high_raw + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1)
    span = high - low
    if not span or span > MAX_IMAGE_SIZE:
        raise SystemExit(f'kernel load span is invalid: {span}')
    if not any(h['vaddr'] <= entry < h['vaddr'] + h['memsz'] and (h['flags'] & 1) for h in loads):
        raise SystemExit('ELF entry does not fall in an executable PT_LOAD segment')

    dyn = dynamics[0]
    if dyn['filesz'] > dyn['memsz'] or dyn['offset'] > len(data) or dyn['filesz'] > len(data) - dyn['offset'] or dyn['filesz'] < 16:
        raise SystemExit('PT_DYNAMIC does not point to valid file-backed data')
    if not any(dyn['vaddr'] >= h['vaddr'] and dyn['vaddr'] + dyn['filesz'] <= h['vaddr'] + h['filesz'] and
               dyn['offset'] == h['offset'] + (dyn['vaddr'] - h['vaddr'])
               for h in loads):
        raise SystemExit('PT_DYNAMIC virtual and file ranges do not map to the same PT_LOAD bytes')
    values: dict[int, int] = {}
    saw_null = False
    for off in range(dyn['offset'], dyn['offset'] + dyn['filesz'] - 15, 16):
        tag, value = struct.unpack_from('<QQ', data, off)
        if tag == DT_NULL:
            saw_null = True
            break
        if tag in (DT_RELA, DT_RELASZ, DT_RELAENT):
            values[tag] = value
    if not saw_null or not all(tag in values for tag in (DT_RELA, DT_RELASZ, DT_RELAENT)):
        raise SystemExit('PT_DYNAMIC lacks a terminated RELA relocation table')
    rela_vaddr, rela_size, rela_ent = values[DT_RELA], values[DT_RELASZ], values[DT_RELAENT]
    if rela_ent != 24 or not rela_size or rela_size % rela_ent:
        raise SystemExit('invalid RELA size/entry size')

    def va_to_file(va: int, size: int) -> int | None:
        for h in loads:
            if va >= h['vaddr'] and va + size >= va and va + size <= h['vaddr'] + h['filesz']:
                return h['offset'] + (va - h['vaddr'])
        return None

    rela_off = va_to_file(rela_vaddr, rela_size)
    if rela_off is None or rela_off + rela_size > len(data):
        raise SystemExit('RELA table is not inside a file-backed PT_LOAD segment')

    reloc_count = 0
    for off in range(rela_off, rela_off + rela_size, rela_ent):
        target, info, addend = struct.unpack_from('<QQq', data, off)
        if info >> 32 != 0 or (info & 0xffffffff) != R_AARCH64_RELATIVE:
            raise SystemExit(f'unsupported AArch64 relocation type at file offset 0x{off:x}')
        if addend < low or addend > high:
            raise SystemExit(f'relocation addend 0x{addend:x} is outside image bounds')
        if not any(target >= h['vaddr'] and target <= h['vaddr'] + h['memsz'] - 8 for h in loads):
            raise SystemExit(f'relocation target 0x{target:x} is outside a load segment')
        reloc_count += 1

    # Exercise placements below, inside and above the former 1-2 GiB window.
    # Placement must remain page-aligned and within the 48-bit physical ceiling.
    upper_base = (MAX_PHYSICAL_LIMIT - span) & ~(PAGE_SIZE - 1)
    if span > MAX_PHYSICAL_LIMIT - MIN_PHYSICAL_BASE:
        raise SystemExit('kernel image cannot fit in the supported AArch64 physical-address range')
    placements = (MIN_PHYSICAL_BASE, 0x40000000, 0x100000000, upper_base)
    for simulated_base in placements:
        if simulated_base < MIN_PHYSICAL_BASE or simulated_base + span > MAX_PHYSICAL_LIMIT:
            raise SystemExit('simulated kernel placement escapes the supported physical-address range')
        if simulated_base & (PAGE_SIZE - 1):
            raise SystemExit('simulated kernel placement is not page aligned')
        bias = simulated_base - low
        for off in range(rela_off, rela_off + rela_size, rela_ent):
            _target, _info, addend = struct.unpack_from('<QQq', data, off)
            relocated = addend + bias
            if relocated != simulated_base + (addend - low):
                raise SystemExit('relocation arithmetic changed the image-relative offset')
            if not simulated_base <= relocated <= simulated_base + span:
                raise SystemExit('relocated pointer falls outside the runtime image')

    print(f'UEFI kernel: AArch64 PIE valid; span={span} bytes; RELA relocations={reloc_count}; physical placements=4 passed')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

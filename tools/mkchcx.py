#!/usr/bin/env python3
"""Package and validate CHCX v1 flat or v2 segmented AArch64 executables."""
import argparse
import pathlib
import re
import struct
import sys

MAGIC = b"CHCX"
VERSION = 1
VERSION_SEGMENTS = 2
HEADER_SIZE = 24
V2_HEADER_SIZE = 56
PAGE_SIZE = 4096
MAX_IMAGE_SIZE = 16 * PAGE_SIZE
MAX_STACK_PAGES = 8


def pack(payload: bytes, entry_offset: int = 0, stack_pages: int = 1) -> bytes:
    if not payload:
        raise ValueError("payload must not be empty")
    if len(payload) > MAX_IMAGE_SIZE:
        raise ValueError(f"payload exceeds {MAX_IMAGE_SIZE} bytes")
    if entry_offset < 0 or entry_offset >= len(payload) or entry_offset % 4:
        raise ValueError("entry offset must be 4-byte aligned and inside payload")
    if len(payload) - entry_offset < 4:
        raise ValueError("payload must contain at least one instruction at entry")
    if not 1 <= stack_pages <= MAX_STACK_PAGES:
        raise ValueError(f"stack pages must be between 1 and {MAX_STACK_PAGES}")
    header = struct.pack("<4sHHIIHHI", MAGIC, VERSION, HEADER_SIZE,
                         len(payload), entry_offset, stack_pages, 0, 0)
    return header + payload


def parse_symbols(path: pathlib.Path) -> dict[str, int]:
    """Read the stable, default `llvm-nm`/`nm` symbol listing format."""
    symbols: dict[str, int] = {}
    pattern = re.compile(r"^\s*([0-9a-fA-F]+)\s+[A-Za-z?]\s+([A-Za-z_][A-Za-z0-9_]*)\s*$")
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = pattern.match(line)
        if match:
            symbols[match.group(2)] = int(match.group(1), 16)
    required = ("_start", "__chos_code_start", "__chos_code_end",
                "__chos_rodata_start", "__chos_rodata_end",
                "__chos_data_start", "__chos_data_end",
                "__chos_bss_start", "__chos_bss_end", "__chos_virtual_end")
    missing = [name for name in required if name not in symbols]
    if missing:
        raise ValueError("symbol file is missing: " + ", ".join(missing))
    return symbols


def pack_v2(flat_payload: bytes, symbols: dict[str, int], stack_pages: int = 1) -> bytes:
    if not 1 <= stack_pages <= MAX_STACK_PAGES:
        raise ValueError(f"stack pages must be between 1 and {MAX_STACK_PAGES}")
    code_start = symbols["__chos_code_start"]
    code_end = symbols["__chos_code_end"]
    ro_start = symbols["__chos_rodata_start"]
    ro_end = symbols["__chos_rodata_end"]
    data_start = symbols["__chos_data_start"]
    data_end = symbols["__chos_data_end"]
    bss_start = symbols["__chos_bss_start"]
    bss_end = symbols["__chos_bss_end"]
    virtual_end = symbols["__chos_virtual_end"]
    entry = symbols["_start"]

    values = (code_start, code_end, ro_start, ro_end, data_start,
              data_end, bss_start, bss_end, virtual_end, entry)
    if any(value < 0 or value > 0xFFFFFFFF for value in values):
        raise ValueError("linker symbol is outside CHCX v2's 32-bit offset range")
    if code_start != 0 or code_end <= code_start:
        raise ValueError("code must start at zero and have nonzero size")
    if ro_start != (code_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1):
        raise ValueError("rodata start must follow code at the next page boundary")
    if ro_start % PAGE_SIZE or data_start % PAGE_SIZE:
        raise ValueError("rodata/data starts must be page-aligned")
    if not (code_end <= ro_start <= ro_end <= data_start <= data_end == bss_start <= bss_end <= virtual_end):
        raise ValueError("linker symbols describe overlapping or out-of-order segments")
    if data_start != (ro_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1):
        raise ValueError("data start must follow rodata at the next page boundary")
    if virtual_end != ((bss_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1)):
        raise ValueError("virtual image end must be page-aligned after BSS")
    if virtual_end == 0 or virtual_end > MAX_IMAGE_SIZE:
        raise ValueError(f"virtual image exceeds {MAX_IMAGE_SIZE} bytes")
    if entry >= code_end or entry % 4 or code_end - entry < 4:
        raise ValueError("entry point must be aligned and inside executable code")
    data_size = data_end - data_start
    initialized_size = data_end
    if initialized_size > len(flat_payload):
        # A completely empty .data section contributes no trailing bytes to
        # objcopy's flat image; its page-alignment hole is defined as zero.
        if data_size != 0 or len(flat_payload) < ro_end:
            raise ValueError(f"flat payload is too short ({len(flat_payload)} bytes; need {initialized_size})")
        flat_payload = flat_payload.ljust(initialized_size, b"\0")
    payload = flat_payload[:initialized_size]
    if initialized_size > MAX_IMAGE_SIZE:
        raise ValueError(f"initialized payload exceeds {MAX_IMAGE_SIZE} bytes")

    code_size = code_end - code_start
    ro_size = ro_end - ro_start
    bss_size = bss_end - bss_start
    header = struct.pack(
        "<4sHHIIHHI" + "I" * 8,
        MAGIC, VERSION_SEGMENTS, V2_HEADER_SIZE, len(payload), entry,
        stack_pages, 0, 0, code_size, ro_start, ro_size,
        data_start, data_size, bss_start, bss_size, virtual_end,
    )
    if len(header) != V2_HEADER_SIZE:
        raise AssertionError("internal CHCX v2 header size mismatch")
    image = header + payload
    validate(image)
    return image


def validate(image: bytes) -> tuple[int, int, int]:
    if len(image) < HEADER_SIZE:
        raise ValueError("image is shorter than CHCX header")
    magic, version, header_size, size, entry, stack, flags, reserved = struct.unpack(
        "<4sHHIIHHI", image[:HEADER_SIZE]
    )
    if magic != MAGIC or flags or reserved:
        raise ValueError("invalid CHCX magic or unsupported flags/reserved fields")
    if not 1 <= stack <= MAX_STACK_PAGES:
        raise ValueError("invalid CHCX stack size")
    if version == VERSION:
        if header_size != HEADER_SIZE:
            raise ValueError("invalid CHCX v1 header size")
        if size == 0 or size > MAX_IMAGE_SIZE or len(image) != HEADER_SIZE + size:
            raise ValueError("invalid CHCX v1 payload size")
        if entry >= size or entry % 4 or size - entry < 4:
            raise ValueError("invalid CHCX v1 entry offset")
        return size, entry, stack
    if version != VERSION_SEGMENTS or header_size != V2_HEADER_SIZE:
        raise ValueError("unsupported CHCX version/header size")
    if len(image) < V2_HEADER_SIZE:
        raise ValueError("image is shorter than CHCX v2 header")
    code_size, ro_off, ro_size, data_off, data_size, bss_off, bss_size, virtual_size = struct.unpack(
        "<IIIIIIII", image[24:V2_HEADER_SIZE]
    )
    initialized_end = data_off + data_size
    bss_end = bss_off + bss_size
    if size == 0 or size > MAX_IMAGE_SIZE or len(image) != V2_HEADER_SIZE + size:
        raise ValueError("invalid CHCX v2 initialized payload size")
    if code_size < 4 or entry >= code_size or entry % 4 or code_size - entry < 4:
        raise ValueError("invalid CHCX v2 entry offset/code size")
    if ro_off != ((code_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1)):
        raise ValueError("invalid CHCX v2 rodata offset")
    if data_off != ((ro_off + ro_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1)):
        raise ValueError("invalid CHCX v2 data offset")
    if bss_off != initialized_end or size != initialized_end or size < data_off:
        raise ValueError("invalid CHCX v2 initialized segment bounds")
    if virtual_size != ((bss_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1)):
        raise ValueError("invalid CHCX v2 virtual size")
    if virtual_size == 0 or virtual_size > MAX_IMAGE_SIZE or virtual_size < size:
        raise ValueError("invalid CHCX v2 virtual image size")
    return size, entry, stack


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("payload", type=pathlib.Path, help="flat payload or CHCX image to verify")
    parser.add_argument("output", type=pathlib.Path, nargs="?", help="output .CHX file")
    parser.add_argument("--entry", type=lambda x: int(x, 0), default=0,
                        help="entry offset for v1 packaging (default: 0)")
    parser.add_argument("--stack-pages", type=int, default=1,
                        help="user stack pages, 1-8 (default: 1)")
    parser.add_argument("--v2-symbols", type=pathlib.Path,
                        help="pack a linked flat image as CHCX v2 using an nm symbol listing")
    parser.add_argument("--verify", action="store_true", help="validate an existing CHCX image")
    args = parser.parse_args()
    try:
        if args.verify:
            image = args.payload.read_bytes()
            size, entry, stack = validate(image)
            version = struct.unpack("<H", image[4:6])[0]
            virtual = struct.unpack("<I", image[52:56])[0] if version == VERSION_SEGMENTS else size
            print(f"CHCX v{version} valid: payload={size} entry=0x{entry:x} stack_pages={stack} virtual_size={virtual}")
            return 0
        if args.output is None:
            parser.error("output is required unless --verify is used")
        flat = args.payload.read_bytes()
        if args.v2_symbols:
            image = pack_v2(flat, parse_symbols(args.v2_symbols), args.stack_pages)
        else:
            image = pack(flat, args.entry, args.stack_pages)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(image)
        size, entry, stack = validate(image)
        version = struct.unpack("<H", image[4:6])[0]
        print(f"Packed CHCX v{version}: {args.payload} -> {args.output}; payload={size}, entry=0x{entry:x}, stack={stack}")
        return 0
    except (OSError, ValueError, struct.error) as exc:
        print(f"mkchcx: error: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Embed user-replaceable PNG assets as bounded ARGB8888 arrays.

Only standard-library Python is required. Supported input PNGs are non-interlaced,
8-bit grayscale, RGB, indexed, grayscale+alpha, and RGBA. This runs at build time;
CHOS does not need a PNG/zlib decoder in EL1 for bundled icons and wallpaper.
"""
from __future__ import annotations
import argparse
import pathlib
import struct
import zlib

PNG_SIG = b"\x89PNG\r\n\x1a\n"
MAX_ICON = 128
MAX_WALLPAPER = 1024


def parse_png(path: pathlib.Path, limit: int) -> tuple[int, int, list[int]]:
    raw = path.read_bytes()
    if not raw.startswith(PNG_SIG):
        raise ValueError(f"{path}: not a PNG file")
    off = len(PNG_SIG)
    width = height = bit_depth = color_type = interlace = None
    palette: bytes | None = None
    transparency: bytes | None = None
    idat = bytearray()
    seen_iend = False
    while off + 12 <= len(raw):
        n = struct.unpack_from(">I", raw, off)[0]
        if n > len(raw) or off + 12 + n > len(raw):
            raise ValueError(f"{path}: truncated PNG chunk")
        typ = raw[off + 4:off + 8]
        data = raw[off + 8:off + 8 + n]
        crc_found = struct.unpack_from(">I", raw, off + 8 + n)[0]
        if (zlib.crc32(typ + data) & 0xFFFFFFFF) != crc_found:
            raise ValueError(f"{path}: CRC mismatch in {typ.decode('ascii', 'replace')} chunk")
        if typ == b"IHDR":
            if n != 13 or width is not None:
                raise ValueError(f"{path}: invalid IHDR")
            width, height, bit_depth, color_type, comp, filt, interlace = struct.unpack(">IIBBBBB", data)
            if width < 1 or height < 1 or width > limit or height > limit:
                raise ValueError(f"{path}: dimensions {width}x{height} exceed {limit}x{limit} limit")
            if bit_depth != 8 or color_type not in (0, 2, 3, 4, 6) or comp != 0 or filt != 0 or interlace != 0:
                raise ValueError(f"{path}: use non-interlaced 8-bit grayscale/RGB/indexed/RGBA PNG")
        elif typ == b"PLTE":
            palette = bytes(data)
        elif typ == b"tRNS":
            transparency = bytes(data)
        elif typ == b"IDAT":
            idat.extend(data)
            if len(idat) > 64 * 1024 * 1024:
                raise ValueError(f"{path}: compressed PNG is too large")
        elif typ == b"IEND":
            seen_iend = True
            break
        off += n + 12
    if width is None or height is None or not seen_iend or not idat:
        raise ValueError(f"{path}: missing required PNG chunks")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color_type]
    stride = width * channels
    expected = height * (stride + 1)
    if expected > 64 * 1024 * 1024:
        raise ValueError(f"{path}: decoded PNG exceeds 64 MiB limit")
    decoder = zlib.decompressobj()
    inflated = decoder.decompress(bytes(idat), expected + 1)
    # Do not call flush() on an overlong/incomplete stream: flush has no output
    # bound and could turn a small, malicious IDAT into an unbounded allocation.
    if (len(inflated) > expected or decoder.unconsumed_tail or decoder.unused_data or
            not decoder.eof):
        raise ValueError(f"{path}: invalid PNG decompressed size or trailing data")
    tail = decoder.flush()
    if tail:
        if len(inflated) + len(tail) > expected:
            raise ValueError(f"{path}: PNG decompressed output exceeds expected size")
        inflated += tail
    if len(inflated) != expected:
        raise ValueError(f"{path}: invalid PNG decompressed size")
    rows: list[bytearray] = []
    src_off = 0
    for y in range(height):
        ft = inflated[src_off]
        src_off += 1
        row = bytearray(inflated[src_off:src_off + stride])
        src_off += stride
        prev = rows[-1] if rows else bytearray(stride)
        for i in range(stride):
            left = row[i - channels] if i >= channels else 0
            up = prev[i]
            up_left = prev[i - channels] if i >= channels else 0
            if ft == 1:
                row[i] = (row[i] + left) & 255
            elif ft == 2:
                row[i] = (row[i] + up) & 255
            elif ft == 3:
                row[i] = (row[i] + ((left + up) // 2)) & 255
            elif ft == 4:
                p = left + up - up_left
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - up_left)
                predictor = left if pa <= pb and pa <= pc else (up if pb <= pc else up_left)
                row[i] = (row[i] + predictor) & 255
            elif ft != 0:
                raise ValueError(f"{path}: unsupported PNG row filter {ft}")
        rows.append(row)
    pixels: list[int] = []
    for row in rows:
        if color_type == 3 and (palette is None or len(palette) % 3):
            raise ValueError(f"{path}: indexed PNG has no valid palette")
        for x in range(width):
            k = x * channels
            if color_type == 6:
                r, g, b, a = row[k:k+4]
            elif color_type == 2:
                r, g, b = row[k:k+3]; a = 255
            elif color_type == 0:
                r = g = b = row[k]; a = 255
            elif color_type == 4:
                r = g = b = row[k]; a = row[k+1]
            else:
                pi = row[k]
                po = pi * 3
                if po + 2 >= len(palette or b""):
                    raise ValueError(f"{path}: palette index out of range")
                r, g, b = palette[po:po+3]
                a = transparency[pi] if transparency is not None and pi < len(transparency) else 255
            pixels.append((a << 24) | (r << 16) | (g << 8) | b)
    return width, height, pixels


def c_ident(s: str) -> str:
    return "".join(ch.upper() if ch.isalnum() else "_" for ch in s)


def emit_array(out, name: str, w: int, h: int, pixels: list[int]) -> None:
    out.write(f"static const uint32_t {name}_pixels[{w * h}u] = {{\n")
    for i in range(0, len(pixels), 8):
        out.write("    " + ", ".join(f"0x{p:08x}u" for p in pixels[i:i+8]) + ",\n")
    out.write("};\n")
    out.write(f"static const gui_embedded_image_t {name} = {{{w}u, {h}u, {name}_pixels}};\n\n")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--icons-dir", type=pathlib.Path, required=True)
    ap.add_argument("--wallpaper", type=pathlib.Path, required=True)
    ap.add_argument("--cursor", type=pathlib.Path, required=True, help="transparent PNG cursor bitmap")
    ap.add_argument("--output", type=pathlib.Path, required=True)
    args = ap.parse_args()
    icon_files = sorted(args.icons_dir.glob("*.png"))
    if not icon_files:
        raise SystemExit(f"no PNG icons found in {args.icons_dir}")
    decoded: list[tuple[str, int, int, list[int]]] = []
    for path in icon_files:
        w, h, pix = parse_png(path, MAX_ICON)
        if w > 128 or h > 128:
            raise SystemExit(f"icon dimensions exceed 128x128: {path}")
        decoded.append((path.stem.lower().replace("-", "_"), w, h, pix))
    wall_w, wall_h, wall_pixels = parse_png(args.wallpaper, MAX_WALLPAPER)
    cursor_w, cursor_h, cursor_pixels = parse_png(args.cursor, MAX_ICON)
    if cursor_w > 64 or cursor_h > 64:
        raise SystemExit(f"cursor dimensions exceed 64x64: {args.cursor}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="ascii") as out:
        out.write("/* Generated from user-replaceable PNGs by tools/png_to_header.py. */\n")
        out.write("#ifndef CHOS_GENERATED_ASSETS_H\n#define CHOS_GENERATED_ASSETS_H\n\n")
        for name, w, h, pixels in decoded:
            emit_array(out, f"chos_asset_{name}", w, h, pixels)
        emit_array(out, "chos_asset_wallpaper", wall_w, wall_h, wall_pixels)
        emit_array(out, "chos_asset_cursor", cursor_w, cursor_h, cursor_pixels)
        out.write(f"#define CHOS_ICON_ASSET_COUNT {len(decoded)}u\n")
        out.write("static const gui_embedded_named_image_t chos_icon_assets[CHOS_ICON_ASSET_COUNT] = {\n")
        for name, w, h, _ in decoded:
            out.write(f'    {{"{name}", &chos_asset_{name}}},\n')
        out.write("};\n\n#endif /* CHOS_GENERATED_ASSETS_H */\n")
    print(f"generated {args.output}: {len(decoded)} icons + {wall_w}x{wall_h} wallpaper + {cursor_w}x{cursor_h} cursor")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())

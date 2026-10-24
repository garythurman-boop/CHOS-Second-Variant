#!/usr/bin/env python3
"""Regression tests for the bounded build-time PNG asset converter."""
import pathlib
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
GEN = ROOT / "tools" / "png_to_header.py"
SIG = b"\x89PNG\r\n\x1a\n"


def chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)


def png_rgba(width: int, height: int, data: bytes) -> bytes:
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return SIG + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(data)) + chunk(b"IEND", b"")


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="chos-png-test-") as temp:
        base = pathlib.Path(temp)
        icons = base / "icons"
        icons.mkdir()
        wallpaper = base / "wallpaper.png"
        cursor = base / "cursor.png"
        output = base / "assets.h"
        sample = png_rgba(2, 2, b"\x00" + bytes((255, 0, 0, 255, 0, 255, 0, 255)) +
                          b"\x00" + bytes((0, 0, 255, 255, 255, 255, 255, 255)))
        (icons / "test-icon.png").write_bytes(sample)
        wallpaper.write_bytes(sample)
        cursor.write_bytes(sample)
        proc = subprocess.run([sys.executable, str(GEN), "--icons-dir", str(icons),
                               "--wallpaper", str(wallpaper), "--cursor", str(cursor), "--output", str(output)],
                              capture_output=True, text=True)
        assert proc.returncode == 0, proc.stderr
        text = output.read_text(encoding="ascii")
        assert "chos_asset_test_icon" in text
        assert "0xffff0000u" in text and "0xff00ff00u" in text
        assert "chos_asset_wallpaper" in text
        assert "chos_asset_cursor" in text

        bad_crc = bytearray(sample)
        bad_crc[29] ^= 0x01  # corrupt IHDR CRC byte
        wallpaper.write_bytes(bad_crc)
        proc = subprocess.run([sys.executable, str(GEN), "--icons-dir", str(icons),
                               "--wallpaper", str(wallpaper), "--cursor", str(cursor), "--output", str(output)],
                              capture_output=True, text=True)
        assert proc.returncode != 0 and "CRC mismatch" in proc.stderr

        # IHDR claims 1x1 (five decoded bytes), but compressed payload inflates
        # much further. The converter must reject before an unbounded flush.
        bad_bomb = png_rgba(1, 1, b"\x00" + bytes((0, 0, 0, 255)) + b"X" * 8192)
        wallpaper.write_bytes(bad_bomb)
        proc = subprocess.run([sys.executable, str(GEN), "--icons-dir", str(icons),
                               "--wallpaper", str(wallpaper), "--cursor", str(cursor), "--output", str(output)],
                              capture_output=True, text=True)
        assert proc.returncode != 0, "inflated size mismatch should be rejected"
    print("png assets: valid PNG conversion, CRC rejection and inflated-size cap passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

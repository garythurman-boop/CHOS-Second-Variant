#!/usr/bin/env python3
"""Check CHCX stack reservations for interactive EL0 applications."""
import pathlib
import struct
import sys

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "build/userland")
expected = {"console.chcx": 4, "file_manager.chcx": 8, "text_viewer.chcx": 2, "gui_demo.chcx": 2, "browser.chcx": 4}
for name, wanted in expected.items():
    data = (root / name).read_bytes()
    if len(data) < 24 or data[:4] != b"CHCX":
        raise SystemExit(f"{name}: invalid CHCX image")
    version, header_size = struct.unpack_from("<HH", data, 4)
    if version != 2 or header_size != 56:
        raise SystemExit(f"{name}: expected CHCX v2")
    stack_pages = struct.unpack_from("<H", data, 16)[0]
    if stack_pages != wanted:
        raise SystemExit(f"{name}: expected {wanted} stack pages, got {stack_pages}")
    print(f"{name}: CHCX v2 stack_pages={stack_pages}")
print("interactive application stack reservations passed")

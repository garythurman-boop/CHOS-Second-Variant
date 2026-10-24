#!/usr/bin/env python3
"""Ensure QEMU and UEFI receive the ELF type each loader supports."""
from pathlib import Path
import struct
import sys

ET_EXEC = 2
ET_DYN = 3
EM_AARCH64 = 183


def inspect(path: str) -> tuple[int, int]:
    data = Path(path).read_bytes()
    if len(data) < 64 or data[:4] != b"\x7fELF" or data[4:7] != b"\x02\x01\x01":
        raise SystemExit(f"{path}: not a little-endian ELF64 file")
    return struct.unpack_from("<H", data, 16)[0], struct.unpack_from("<H", data, 18)[0]


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: test_kernel_build_modes.py QEMU_KERNEL UEFI_KERNEL", file=sys.stderr)
        return 2
    qemu_type, qemu_machine = inspect(sys.argv[1])
    uefi_type, uefi_machine = inspect(sys.argv[2])
    if qemu_machine != EM_AARCH64 or qemu_type != ET_EXEC:
        raise SystemExit(f"QEMU kernel must be static AArch64 ET_EXEC; got type={qemu_type}, machine={qemu_machine}")
    if uefi_machine != EM_AARCH64 or uefi_type != ET_DYN:
        raise SystemExit(f"UEFI kernel must be relocatable AArch64 ET_DYN; got type={uefi_type}, machine={uefi_machine}")
    print("Kernel build modes: QEMU ET_EXEC and UEFI ET_DYN validated")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

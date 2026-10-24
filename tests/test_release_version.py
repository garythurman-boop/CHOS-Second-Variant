#!/usr/bin/env python3
"""Catch stale release labels in the active CHOS 0.30.0 build path."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
checks = {
    'Makefile': ['CHOS_VERSION ?= 0.30.0', 'RELEASE_NOTES_0.30.0.md'],
    'kernel/core/kernel.c': ['CHOS ARM64 kernel 0.30.0'],
    'kernel/core/shell.c': ['#define CHOS_VERSION "0.30.0"'],
    'kernel/platform/uefi_console.c': ['CHOS 0.30.0'],
    'kernel/gfx/gui.c': ['CHOS ARM64 0.30.0'],
    'boot/uefi/bootaa64.c': ['0.30.0'],
    'tools/mkuefi_iso.py': ["b'CHOS_0300'", '0.30.0 experimental UEFI boot image'],
    'tools/mkchos_install_disk.py': ['experimental 0.30.0'],
    'tests/qemu_smoke.py': ['CHOS ARM64 kernel 0.30.0'],
}
for name, needles in checks.items():
    text = (ROOT / name).read_text(encoding='utf-8')
    for needle in needles:
        assert needle in text, f'{name} is missing the active release marker {needle!r}'
for name in ('README.md', 'CHANGELOG.md', 'docs/RELEASE_NOTES_0.30.0.md',
             'docs/VALIDATION_0.30.0.md', 'docs/ROADMAP_0.30.0.md'):
    assert (ROOT / name).is_file(), f'missing release document: {name}'
print('Release contract: active kernel/GUI/EFI/ISO labels and release documentation agree on CHOS 0.30.0')

#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
loader = (root / "boot/uefi/bootaa64.c").read_text()
start = (root / "boot/aarch64/start.S").read_text()
kernel = (root / "kernel/core/kernel.c").read_text()
makefile = (root / "Makefile").read_text()

assert "capture_uefi_input(boot_info)" not in loader
assert "CHOS_BOOT_FLAG_UEFI_SERVICES_ACTIVE" not in loader
assert "status = g_st->BootServices->ExitBootServices(image_handle, map_key);" in loader
assert "CHOS_BOOT_FLAG_UEFI_SERVICES_ACTIVE" not in start
assert "chos_uefi_input_poll" not in kernel
assert "chos_uefi_input_init" not in kernel
assert "uefi_fdt_has_virtio_mmio(boot_info)" in kernel
assert "input_ready = chos_input_init() > 0" in kernel
assert "if (input_ready) chos_input_poll()" in kernel
# Never cache or invoke UEFI protocol function pointers after ExitBootServices.
assert "uefi_text_input_address" not in kernel
assert "uefi_simple_pointer_address" not in kernel
assert "kernel/platform/uefi_input.c" not in makefile
assert not (root / "kernel/platform/uefi_input.c").exists()
print("UEFI handoff safety contract tests passed")

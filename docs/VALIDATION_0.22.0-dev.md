# CHOS 0.22.0-dev revision 2 validation record

## Build and format validation

- Clean `make clean && make CC=clang LD=ld.lld`: passed. The normal `build/chos.elf` is a statically linked AArch64 `ET_EXEC`, preserving the format expected by QEMU's generic `loader` device.
- `make uefi-iso CC=clang LD=ld.lld`: passed. The ISO embeds a separately compiled AArch64 `ET_DYN` PIE at `EFI/CHOS/CHOS.ELF`.
- `python3 tests/test_uefi_kernel.py build/uefi-kernel/chos.elf`: passed. The image's PT_LOAD/PT_DYNAMIC segments and supported AArch64 RELATIVE relocation table were checked.
- `python3 tests/test_kernel_build_modes.py build/chos.elf build/uefi-kernel/chos.elf`: passed; verifies the two loader-specific ELF types.
- `python3 tests/test_mkuefi_iso.py`: passed; checks the EFI boot path, FAT16 image, kernel file, PE/COFF architecture and El Torito EFI catalog.

## Regression tests

- Complete `make test CC=clang LD=ld.lld`: passed on the built source revision. The first invocation exceeded the execution window while it was still compiling the separate UEFI object tree; a subsequent run completed all tests, including the new build-mode check and local browser HTTP fixtures.
- `make test-sanitize CC=clang LD=ld.lld`: passed with AddressSanitizer and UndefinedBehaviorSanitizer enabled for the configured host tests.

## Runtime status and limits

- The build container does not include `qemu-system-aarch64`; the `make run-gui` guest boot has not been run here. This revision restores its kernel file to ET_EXEC and removes the prior build-format mismatch, but runtime confirmation is still needed on the user's QEMU host.
- VMware Fusion boot has not been run in this build environment. The EFI loader now inspects actual EFI conventional-memory descriptors and reports the number and largest size of candidate ranges in the supported 1–2 GiB window. This is a source/build improvement, not proof that Fusion's firmware exposes usable RAM there.
- Even if kernel allocation succeeds, CHOS's current kernel still expects a QEMU-virt FDT and QEMU-specific PL011/GICv2/VirtIO-MMIO devices. A Fusion desktop is not claimed until its firmware/platform handoff and virtual-device model are supported and tested.

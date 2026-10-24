# CHOS 0.26.0-dev validation report

## Scope

This is an integrated development snapshot with the existing UEFI/GOP boot path, guarded PCI access, experimental polled NVMe I/O, read-only GPT parsing and partition slices, and a safe host-side installed-disk image builder. It is **not yet a fully functional VMware guest or graphical installer**. Native xHCI/USB HID and VMware vmxnet3 drivers are not implemented, and the NVMe runtime path has not been validated against a real virtual controller.

## Build and artifact checks

- Clean Clang/LLD build completed for the static AArch64 QEMU kernel and the separate relocatable AArch64 UEFI kernel.
- UEFI ISO generation completed and validated the ISO9660 descriptors, El Torito EFI catalog, FAT boot structures, and ARM64 PE/COFF loader.
- UEFI kernel test passed: AArch64 PIE, supported RELA records, and relocation placement checks.
- The 256 MiB installed-disk image builder completed. Image checks validated protective MBR, primary and backup GPT structures and CRCs, FAT32 EFI System Partition, EFI loader/kernel files, and CHFS data partition/application payload.
- Installer image tool safety test passed, including rejection of symlink and protected device-path outputs. The tool writes a regular image file; it does not write a physical device.
- SHA-256 checksums were generated and verified for the final release artifacts.

## Host regression and sanitizer tests

The host regression suite's component tests passed, including boot-info/ACPI/PCI/NVMe parsing, GPT bounds and partition slices, PMM/kheap/VMM/scheduler, networking packets/stack/TCP, CHFS, graphics/GUI, app image/stack checks, ISO generation, and installer-image checks. The final independent checks for UEFI handoff contract, kernel relocation, kernel build-mode separation, HTTP redirect fixtures, installer-image safety, and ISO format also passed.

The aggregate `make test CC=clang LD=ld.lld -j4` run from the clean extracted source archive exceeded the runner's five-minute command limit while invoking `tests/test_mkchos_install_disk.py`. That test passes independently, as do the subsequent UEFI handoff, kernel build-mode, ISO and browser fixture checks. Therefore, this report does **not** claim that the aggregate `make test` invocation itself completed successfully.

The standalone `make test-sanitize CC=clang LD=ld.lld -j1` run completed successfully; all configured ASan/UBSan host test binaries passed.

## Runtime validation not performed

- This build environment does not have `qemu-system-aarch64` and matching AArch64 UEFI firmware available, so the new ISO and installed disk image were not booted here.
- VMware Fusion is not available in this environment. NVMe controller initialization and actual device reads/writes remain unverified at runtime.
- The generated installed-disk image passed structural checks, but a live first boot from that image has not been demonstrated.

## Known limitations

- Native PCI xHCI, USB hub/device enumeration, and HID keyboard/mouse drivers are not implemented. In VMware Fusion, the desktop may appear without usable keyboard or mouse input.
- VMware vmxnet3 networking is not implemented. The existing VirtIO network backend is for the current QEMU path.
- NVMe support is experimental and limited to namespace 1, plain 512-byte logical blocks, and no metadata. It must be tested using a disposable virtual disk before relying on it.
- There is no graphical in-OS installation wizard or partition editor. The host-side image builder creates a prepared image file, not an interactive installer.
- Browser limitations include HTTPS/TLS, JavaScript, complete CSS layout, real image rendering, web forms, and downloads.

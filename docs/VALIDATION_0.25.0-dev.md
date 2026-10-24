# CHOS 0.25.0-dev-r9 validation report

## Build artifacts

- Standard AArch64 QEMU kernel: `ET_EXEC`.
- UEFI kernel: AArch64 `ET_DYN` with supported RELA relocations.
- ARM64 `BOOTAA64.EFI` and ISO9660 image with EFI El Torito catalog / FAT16 EFI System Partition.
- Release artifacts are built with Clang/LLD using `-Werror`.

## New PCI inventory checks

- Valid MCFG-declared ECAM can be scanned when the firmware memory map omits the ECAM aperture.
- Any overlap with conventional RAM or other RAM-backed/unknown EFI memory types is rejected.
- Malformed descriptor ranges with arithmetic overflow reject the ECAM window (fail-closed).
- Scanner reads vendor/device, class/subclass/programming-interface, command/status, and assigned BARs only. It does not write PCI config space or enable devices.
- xHCI candidates are identified by class `0x0c`, subclass `0x03`, programming interface `0x30`.
- The Welcome screen displays PCI/xHCI identity and BAR0/BAR1 status only when the UEFI inventory pass was actually run; QEMU shows its VirtIO input path.

## Test results

- Clean Clang/LLD kernel, EFI loader, UEFI kernel, and ISO build: passed.
- `test-pci` and `test-gui`: passed.
- Full host regression component tests: passed. The aggregate `make test` command exceeded the execution limit during its final kernel-mode test; that test and the following browser test passed individually.
- Full standalone `make test-sanitize`: passed with AddressSanitizer and UndefinedBehaviorSanitizer.
- Browser HTTP fixture tests (links and all five redirect codes): passed independently.
- UEFI ELF relocation, ISO/catalog/FAT, boot handoff contract, and QEMU-`ET_EXEC` / UEFI-`ET_DYN` build-mode checks: passed.
- Package checksum verification: performed on the final delivered files.

## Limits / runtime status

- This release does not contain a native xHCI controller driver or USB HID keyboard/mouse support.
- PCI inventory is read-only; no devices are enabled or initialized. NVMe and `vmxnet3` drivers are not included.
- VMware runtime device discovery and QEMU runtime launch are not available from this build environment and must be confirmed on the target VM.

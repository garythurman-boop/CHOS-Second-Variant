# CHOS 0.24.0-dev validation record

Date: 2026-10-23

## Build

- **PASS** — Clang/LLD AArch64 kernel compile/link with warnings treated as errors.
- **PASS** — Separate UEFI PIE kernel and ARM64 PE/COFF EFI loader compile/link.
- **PASS** — Generated ISO: ISO9660 descriptors, EFI El Torito catalog, FAT16 EFI System Partition, `BOOTAA64.EFI`, embedded kernel, and release-note payload validated by the ISO builder/test.
- **PASS** — Build-mode check verifies QEMU kernel is `ET_EXEC` and UEFI kernel is `ET_DYN`.

## Automated tests

- **PASS** — Full `make CC=clang LD=ld.lld -j2 test` suite, including GUI, networking/TCP, filesystems, userland packaging, browser HTTP fixtures, boot-info, UEFI PMM, ACPI, PCI ECAM enumeration, ELF/relocation, and ISO structure checks.
- **PASS** — Full `make CC=clang LD=ld.lld -j2 test-sanitize` suite with AddressSanitizer and UndefinedBehaviorSanitizer.
- **PASS** — ACPI parser tests cover RSDP and XSDT checksums, MADT GICD/GICR/GICC, GTDT timer GSIVs, MCFG ECAM metadata, malformed table bounds and rejection of a bad checksum on a recognized platform table. PCI tests cover device/function enumeration, class decoding, bounded output, and strict gating on verified reserved/MMIO ECAM windows.
- **PASS** — UEFI PMM tests cover adjacent conventional-memory region coalescing, region limits, kernel reservation, allocation/free, and malformed boot-info descriptor handling.
- **PASS** — Browser fixture server test covers direct content, relative links, 301/302/303/307/308 redirects, redirect loop, HTTPS redirect rejection and wrong content type.
- **PASS** — Source ZIP can be extracted and the code compiles from the extracted tree (the final artifact will contain source/docs/tests only, not cached build products).

## Runtime validation

- **KNOWN WORKING BASELINE** — The user confirmed 0.22.0-dev-r3 enters the kernel under VMware Fusion, reports valid ACPI/GOP information, and then intentionally halts at the diagnostic-only stage.
- **PENDING — 0.24.0-dev on QEMU** — This build environment does not provide `qemu-system-aarch64` with firmware to execute the guest. The host build/regression tests do not prove runtime boot.
- **PENDING — 0.24.0-dev on VMware Fusion** — The new UEFI path now attempts PMM/heap/scheduler and GOP GUI initialization, but this exact image has not been booted in Fusion here.
- **PENDING — VMware peripherals** — GICv3/GTDT and read-only PCI ECAM inventory are now implemented as experimental paths but need Fusion runtime validation. USB xHCI/HID input, NVMe storage and vmxnet3 networking drivers are still not implemented; input, persistent storage and networking should not be described as working on VMware.

## Known limitations

The UEFI path establishes a common boot-info layer, conventional-RAM allocator input, ACPI parsing, and a GOP-backed drawing surface. It does not yet replace the QEMU platform layer with a complete set of ACPI/PCI-described device drivers. PCI support is currently read-only ECAM inventory; it does not enumerate BAR requirements for allocation, enable bus mastering, or attach NVMe/xHCI/vmxnet3 drivers. The browser continues to be a safe HTTP text-preview browser with bounded redirects and links; HTTPS/TLS, JavaScript and full HTML/CSS/image rendering remain unsupported.

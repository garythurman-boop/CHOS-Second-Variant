# CHOS 0.24.0-dev — platform memory, ACPI discovery and GOP desktop path

## Implementation in this development snapshot

- Removes the EFI loader's former fixed 1–2 GiB requirement. It searches conventional-memory descriptors for a contiguous, aligned kernel placement, validates AArch64 ELF load and dynamic segments, and applies supported `R_AARCH64_RELATIVE` relocations.
- Preserves separate kernel build modes: the normal QEMU generic-loader kernel is static `ET_EXEC`; the UEFI ISO uses a separate PIE `ET_DYN` image.
- Defines a versioned EFI-to-kernel boot-info ABI containing the firmware memory map and descriptor stride, relocated kernel span/entry, optional FDT and ACPI RSDP, and optional GOP framebuffer details.
- Adds `pmm_init_from_uefi()`: conventional RAM descriptors are validated/coalesced and converted to the existing PMM representation; the kernel image and boot metadata are reserved. Host tests cover coalescing, region counts, allocation/free, and malformed ABI input.
- Expands memory-region tracking from the old FDT-oriented limit to 32 regions to accommodate a more fragmented UEFI memory map.
- Adds a bounded ACPI parser for RSDP, RSDT/XSDT, MADT ARM GIC structures, GTDT timer GSIVs, and MCFG PCI ECAM segments. It validates checksums, lengths, memory-map containment and known-table structure; if a revision-2+ RSDP has no XSDT address, it falls back to its RSDT pointer.
- Adds a GOP presentation backend for the existing CHOS ARGB backbuffer, including RGBX/BGRX/bitmask pixel conversion and bounded rectangle copies.
- Adds a minimal conditional single-PE GICv3 Group-1 initialization path using ACPI MADT/GTDT data for the architectural physical timer. GICD/GICR register windows must be advertised by the EFI memory map as reserved/MMIO; otherwise the kernel keeps IRQs masked and can run a polled GUI. Runtime support is still experimental.
- Adds read-only PCI ECAM inventory based on MCFG. Configuration reads are permitted only for complete 1 MiB bus windows explicitly marked reserved/MMIO in the saved EFI map; this detects virtual hardware without enabling buses, BARs, or bus mastering.
- The UEFI kernel path validates boot info, reports ACPI/platform data, initializes the production physical-page allocator, heap and scheduler, then attempts to initialize the CHOS GUI on the firmware GOP framebuffer. It avoids blindly initializing QEMU's PL011/VirtIO-MMIO/FDT-specific devices.
- Keeps the working HTTP browser, redirects, navigation, and TCP lifecycle changes unchanged, with regression tests still enabled.

## Not yet implemented / release scope limits

This is a development step toward a VMware port. It is **not yet a claim of full VMware guest functionality**. The UEFI path conditionally initializes a minimal GICv3 physical-timer path and read-only PCI ECAM inventory, but those paths need runtime validation on real firmware. USB xHCI/HID keyboard and mouse, PCI device resource setup, VMware virtual NVMe storage, and `vmxnet3` networking drivers are not completed in this snapshot. Consequently, the GUI may render via GOP, but VMware mouse/keyboard, disk persistence, and network/browser access are not yet guaranteed. Do not assume that a successful kernel-entry screen proves the complete desktop or hardware stack works.

The existing QEMU path remains the known compatibility baseline and uses a separate ET_EXEC kernel and QEMU `virt` drivers. HTTPS/TLS, JavaScript, full CSS layout, image decoding, forms, downloads, 3D acceleration, shared folders, clipboard integration, and suspend/resume are outside this iteration.

## Validation expectations/results

- Clean Clang/LLD builds of the normal kernel, separate UEFI PIE kernel and ARM64 EFI loader.
- Full host regression suite and ASan/UBSan suite.
- Boot-info, UEFI PMM, ACPI parser, PCI ECAM inventory/MMIO gating, ELF/relocation, build-mode, browser fixture, and ISO structure tests.
- The build container does not include a runnable `qemu-system-aarch64` plus matching firmware or VMware Fusion, so actual guest boot, GUI responsiveness, and device-driver behavior require runtime tests on the developer's host. Such tests must be reported separately from host-side validation.

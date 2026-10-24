# CHOS 0.25.0-dev-r9 — PCI inventory and native-input discovery

## Purpose

The 0.25.0-dev-r8 UEFI path can reach a stable desktop in VMware Fusion, but keyboard and mouse input remain unavailable after the unsafe UEFI firmware-input bridge was removed. r9 improves hardware discovery so the native xHCI/HID implementation can be matched to the actual virtual device.

## Changes

- PCI ECAM enumeration uses a checksum-validated ACPI MCFG aperture even if the EFI memory map omits ECAM descriptors. Any overlapping RAM-backed/unknown memory descriptor or malformed overflowing descriptor range is rejected.
- PCI inventory snapshots vendor/device identity, class/subclass/programming interface, command/status, and assigned BAR values without writing config space or enabling devices.
- xHCI-class devices are identified by class `0x0c`, subclass `0x03`, programming interface `0x30`; early diagnostics print the first 16 PCI functions and xHCI BAR0/BAR1 values when found.
- The Welcome screen reports PCI function count, xHCI identity, and raw BAR0/BAR1 values after a UEFI inventory pass. The standard QEMU screen accurately reports its existing VirtIO input path when UEFI inventory was not run.
- Existing QEMU VirtIO input remains unchanged. The unsafe firmware input bridge is not restored.
- Old r7 version labels were updated to r9.

## Scope boundary

This is a read-only discovery/inventory build, not the native USB input driver. It does not reset or enable the xHCI controller, enumerate USB devices, or submit HID reports. Fusion keyboard/mouse input is still not expected to work until a native xHCI host-controller and USB HID stack are implemented and runtime-tested.

## Validation

The PCI host tests cover MCFG-declared ECAM missing from the EFI map, rejection of EFI RAM overlaps, malformed descriptor overflow, vendor/device and class decoding, multifunction scanning, read-only BAR snapshots, and bounded output. The full `make test` invocation in this environment exceeded its overall time limit during the final build-mode test, but that test and the last browser-fixture test passed when run separately. The full standalone ASan/UBSan suite passed. The clean AArch64/UEFI build, ISO structure checks, UEFI ELF validation, and QEMU/UEFI kernel-mode check passed. Runtime discovery on the user's Fusion VM remains to be confirmed.

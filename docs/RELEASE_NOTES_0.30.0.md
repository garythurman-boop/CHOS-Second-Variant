# CHOS 0.30.0 — integration, input foundation, and desktop refinement

## Summary

CHOS 0.30.0 builds on the 0.26.0-dev storage/UEFI foundation. It focuses on improving the current UEFI input path where it is safe to do so, establishing a tested USB HID descriptor parser, restoring the classic Windows 9x/2000-era desktop direction, providing hardware-entropy and installer-planning groundwork, and making build artifacts and runtime smoke-test setup easier to reproduce.

## Implemented in this snapshot

- **UEFI FDT validation:** before parsing, the kernel checks the FDT header and its full claimed size against a preserved EFI memory descriptor and rejects blobs outside the bounded 16 MiB limit.
- **UEFI VirtIO input integration:** the UEFI desktop now initializes and polls the existing VirtIO-MMIO input backend only when the FDT advertises a `virtio,mmio` node in the QEMU aperture (`0x0a000000..0x0a004000`) with sufficient register size. Other UEFI systems are not probed through this fixed QEMU address range.
- **USB HID descriptor parser:** bounded field extraction and parsing of input/output/feature reports, usage metadata, report IDs, variable/array fields, relative/signed axes, pointer/keyboard/mouse/wheel capability hints, and errors for truncated or malformed report descriptors. No allocation is required. This parser is a foundation for USB HID integration, not the controller transport or device-enumeration stack.
- **Host tests:** synthetic keyboard and mouse descriptors, report-ID behavior, cross-byte bitfield extraction, signed extraction, truncated short/long items, unmatched stack structures, report-ID validation, and descriptors without input reports.
- **Classic desktop styling:** gray panels and bevels, classic navy active title bars, a teal default desktop, and original CHOS wallpaper/artwork. It is a cohesive visual direction, not full Windows 9x/2000 feature parity.
- **VirtIO RNG groundwork:** modern-MMIO driver with bounded reset/queue checks, host-mock tests, nonblocking kernel read API, and `CHOS_SYS_RANDOM_BYTES` / `chos_random_bytes()` user API. QEMU launch targets attach a VirtIO RNG device. Hardware and DMA behavior are not yet runtime-validated.
- **Installer planner:** pure no-write GPT geometry planner with disk identity binding, 1 MiB alignment, ESP/data size limits, exact destructive confirmation and monotonic stages. No UI or block writes exist.
- **HTTPS groundwork:** URLs default to port 443, relative/scheme-relative references and redirects preserve secure scheme. Browser still blocks HTTPS before connect until TLS and peer verification are integrated, and never downgrades to HTTP.
- **Repeatable validation:** versioned 0.30.0 ISO and installed-image outputs, a QEMU serial smoke-test script, and a throwaway smoke-test disk under `build/`.
- **Image payload:** generated prepared disk image includes the demo, GUI demo, text viewer, console, browser, and file manager app payloads.

## Build artifacts

- `build/chos-0.30.0.iso`
- `build/chos-0.30.0-installed.img`
- `build/chos.elf` — direct QEMU kernel
- `build/uefi-kernel/chos.elf` — UEFI relocatable kernel
- `build/uefi/BOOTAA64.EFI` — ARM64 UEFI loader

## Known limitations and release blockers

- The native PCI xHCI driver, USB device/hub enumeration, USB HID report transport, and VMware Fusion keyboard/mouse path are **not implemented**. The HID parser alone cannot make USB devices work.
- The VMware `vmxnet3` driver is not implemented.
- There is no interactive graphical installer, disk/partition selection wizard, on-device GPT/ESP creation/copy flow, recovery wizard, or confirmed boot from the generated installed image. The image builder is host-side tooling only.
- NVMe remains experimental and has no live controller I/O validation here.
- TLS/HTTPS transport and certificate verification are not implemented; HTTPS is deliberately unavailable. See `docs/HTTPS_TLS_BOUNDARY.md`. JavaScript, complete CSS, image rendering, form processing and downloads also remain unsupported.
- The native USB xHCI path and VMware keyboard/mouse support are not implemented; HID report parsing alone does not connect devices.
- QEMU/AAVMF and VMware runtime testing were unavailable in this build environment. The release must not be called a verified VMware-ready release until those tests are performed. See `docs/DRIVER_STATUS.md`, `docs/INSTALLER_PLAN.md`, and `docs/CODEX_HANDOFF_0.30.0.md`.

# CHOS 0.30.0 validation report

## Overall release status

**Source and build artifacts are reproducible; the OS is not runtime-certified as stable.** This is a stability-oriented development snapshot for Codex continuation, not a production-ready OS release. The kernel and UEFI/ISO build, prepared image generation, configured ASan/UBSan suite, and component tests pass. Live QEMU/UEFI/VMware tests are blocked because QEMU and ARM64 UEFI firmware are not installed in the build environment.

## Clean build and image outputs

- `make clean`: **PASS**.
- `make CC=clang LD=ld.lld -j4`: **PASS** — freestanding AArch64 direct-boot kernel and app payloads build.
- `make uefi-iso install-disk-image CC=clang LD=ld.lld`: **PASS** — UEFI PIE kernel, ARM64 EFI app, ISO9660/El Torito ISO and prepared 256 MiB disk image generated.
- `tests/test_uefi_kernel.py`: **PASS** — UEFI ET_DYN and RELA layout; span 49,512,448 bytes and 202 relocations.
- `tests/test_mkuefi_iso.py`: **PASS** — ISO descriptors, El Torito EFI catalog, FAT/FAT32 boot structures and PE/COFF ARM64 application.
- `tests/test_mkchos_install_disk.py`: **PASS** when run independently — protective MBR, primary/backup GPT CRCs, FAT32 ESP, EFI files, CHFS partition and app payload; destructive destination-path safeguards also checked.
- `tests/test_kernel_build_modes.py`: **PASS** — direct-boot kernel is ET_EXEC; UEFI kernel is ET_DYN.
- `tests/test_release_version.py`: **PASS** — active release labels agree on CHOS 0.30.0.
- `tests/test_uefi_handoff_contract.py`: **PASS** — post-ExitBootServices and FDT bounds contract.

## Host regression and sanitizer tests

- `make CC=clang LD=ld.lld -j1 test-sanitize`: **PASS** — all configured ASan/UBSan host tests passed. This includes the USB HID descriptor parser, VirtIO RNG host mock, installer planner, ACPI/PCI, GPT/block slices, networking, memory allocators, scheduler, CHFS, graphics/GUI/window manager and browser core.
- `make test-usb-hid`: **PASS** — bounded HID descriptor parsing and malformed input cases.
- `build/test-virtio-rng`: **PASS** when run directly — bounded polled device mock, feature/queue checks, and no synthetic entropy fallback.
- `build/test-installer-plan`: **PASS** when run directly — geometry, target identity, explicit confirmation and monotonic stage gates.
- `build/test-browser-core`: **PASS** — URL validation, default HTTPS port 443, scheme preservation for relative links/redirects and fail-closed transport gate.
- `tests/test_browser_test_server.py`: **PASS** — HTTP fixture, all redirect classes, relative links, redirect loops and misleading content type.
- `tests/test_mkchcx.py`, `tests/test_app_stack_pages.py`, `tests/test_mkchfs.py`, `tests/test_sync_system_apps.py`, `tests/test_png_to_header.py`, `tests/test_irq_sp_el0_contract.py`: **PASS**.
- The Makefile's aggregate `make test CC=clang LD=ld.lld -j1` invocation was **NOT COMPLETED**: the execution environment terminated it at its command time limit while the installer-image test was running. That exact installer-image test passed independently in approximately 3.2 seconds, and the remaining UEFI handoff, kernel layout, browser fixture, RNG and installer-planner tests passed independently. The aggregate command is therefore not labeled PASS.

## Security / feature status

- **VirtIO RNG:** driver, nonblocking entropy API and random-bytes syscall compile and pass host-mock tests. Actual device queue/DMA/cache behavior is not runtime-validated inside QEMU. Do not yet treat this alone as proof that cryptographic entropy is production-ready.
- **HTTPS:** URL parsing, port 443 default, redirect/relative link handling are host-tested. TLS transport, certificate-chain and hostname verification, CA root store, trusted clock policy and public HTTPS tests are **not implemented**. The browser deliberately refuses HTTPS before connecting and never downgrades to HTTP.
- **Installer:** GPT layout planner and destructive-confirmation gate compile and pass host tests. Planner is pure/no-write. Graphical UI, disk writer, partition creation, image deployment/recovery and first-boot workflow are **not implemented**.
- **USB/HID:** HID report descriptor parser is implemented and host-tested. xHCI controller, USB enumeration/transfer engine and USB HID event transport are **not implemented**.
- **VMware:** native USB keyboard/mouse integration and `vmxnet3` are not implemented/runtime-validated.
- **NVMe:** experimental implementation exists, but live controller I/O is not validated; use only a disposable image/disk during testing.

## Live runtime validation

- Direct QEMU boot: **NOT RUN / BLOCKED** — `qemu-system-aarch64` is absent from this environment.
- UEFI ISO boot: **NOT RUN / BLOCKED** — ARM64 AAVMF/EDK2 firmware is unavailable.
- `make qemu-smoke`: **NOT RUN** — requires `qemu-system-aarch64`.
- VMware Fusion: **NOT RUN / BLOCKED** — Fusion was unavailable in this build environment.
- ISO → graphical install → reboot from installed disk: **NOT IMPLEMENTED / NOT RUN**.

## What must be true before calling CHOS stable

1. Live QEMU direct-boot and UEFI smoke tests pass, including keyboard, pointer, display, storage persistence, network and entropy device initialization.
2. VMware Fusion target hardware is inventoried and native USB keyboard/mouse works, or the exact supported device configuration is documented; `vmxnet3` only claimed if implemented and tested.
3. A graphical installer writes only an explicitly selected disposable target, verifies primary/backup GPT/EFI/data writes, survives injected errors, and boots independently of the ISO.
4. HTTPS succeeds only with a reviewed TLS provider, certificate-chain/hostname verification, CA root policy, trustworthy clock and valid entropy; invalid certificates and missing entropy fail closed.
5. Every release test result is recorded exactly. A timeout is never considered a pass.

See `docs/DRIVER_STATUS.md`, `docs/HTTPS_TLS_BOUNDARY.md`, `docs/INSTALLER_PLAN.md`, and `docs/CODEX_HANDOFF_0.30.0.md` for details.

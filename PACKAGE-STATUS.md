# CHOS 0.30.0 Classic — source and prebuilt handoff

This bundle contains the full working source tree, tests, documentation, assets, a prebuilt ISO, experimental prepared disk image, direct/UEFI kernels, EFI loader and a SHA-256 manifest. Generated build intermediates are intentionally omitted; rebuild with the instructions in `README.md`.

## Release characterization

This is a clean, reproducible **development handoff snapshot**, not a claim that CHOS is a stable, production-ready operating system. The configured ASan/UBSan suite and clean direct/UEFI builds pass. The aggregate `make test` command exceeded the environment execution limit; its remaining checks were run independently. No live QEMU, ARM64 UEFI firmware or VMware Fusion was available.

## Important feature boundaries

- The desktop uses an original Windows 9x/2000-era visual direction.
- USB HID report parsing and a guarded QEMU VirtIO input path exist; native xHCI/USB transport and VMware keyboard/mouse integration do not.
- A VirtIO RNG driver and nonblocking random-bytes syscall are present and host-mock tested, but the guest hardware path is not runtime-verified.
- HTTPS URL parsing/redirect resolution is tested; TLS and certificate verification are not integrated. HTTPS intentionally fails closed.
- The installer layout planner is non-destructive; the graphical installer and disk writer are not implemented.
- VMware `vmxnet3` is not implemented. NVMe remains experimental.

## Best next document for Codex

Start with `docs/CODEX_HANDOFF_0.30.0.md`. It contains the work order, security boundaries, implementation requirements and explicit acceptance gates. Also read `docs/DRIVER_STATUS.md`, `docs/HTTPS_TLS_BOUNDARY.md`, `docs/INSTALLER_PLAN.md` and `docs/VALIDATION_0.30.0.md` before modifying drivers or security-sensitive paths.

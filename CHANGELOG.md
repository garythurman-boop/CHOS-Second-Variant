# CHOS 0.30.0

A broad development integration milestone focused on a classic Windows 9x/2000-era visual direction, bounded USB HID parsing, guarded UEFI paths, hardware-backed entropy groundwork, a safe install planner, secure HTTPS fail-closed behavior, and reproducible validation. It is not yet a feature-complete or runtime-verified stable OS release.

## Added and changed

- Corrected the UEFI main loop to initialize/poll the existing VirtIO input backend when, and only when, the preserved FDT confirms a QEMU VirtIO-MMIO device in the expected aperture.
- Hardened the UEFI FDT gate by validating the full claimed blob range against EFI memory descriptors before following device-tree offsets.
- Added a bounded, allocation-free USB HID report-descriptor parser and safe bitfield/sign-extension helpers, with host regression tests for keyboard, mouse, report IDs, malformed descriptors, stack imbalance, and bit boundaries.
- Restored a Windows 9x/2000-era desktop direction: neutral gray panels/bevels, navy active title bars, teal default wallpaper, and original CHOS artwork.
- Added a QEMU serial smoke-test script and disposable smoke disk that does not overwrite the developer's persistent `chos-disk.img`. The QEMU targets now attach a VirtIO RNG device.
- Added a polled modern VirtIO-MMIO RNG driver, host mock tests, nonblocking entropy API, and bounded user syscall for entropy-backed random bytes. This has not been runtime-tested inside a VM.
- Added a no-write installer GPT planner with alignment, identity validation, explicit destructive confirmation and monotonic stage transition checks. No graphical installer or disk writer was added.
- Expanded HTTPS URL parsing/relative resolution/redirect correctness and port 443 defaults, while preserving an explicit fail-closed policy because TLS and certificate verification are not integrated.
- Versioned ISO/preinstalled-image outputs as 0.30.0, updated active OS version strings, and expanded the prepared image's app payload to include demo, GUI demo, text viewer, console, browser, and file manager.

## Still incomplete

- Reviewed TLS backend, certificate-chain and hostname verification, CA root management, trustworthy clock policy, and live HTTPS verification. See `docs/HTTPS_TLS_BOUNDARY.md`.

- Native xHCI controller/USB enumeration/HID transport required for VMware Fusion input.
- VMware vmxnet3 network driver.
- Interactive graphical installation/partition UI, disk-sector writer and end-to-end tested install-to-disk flow. The current planner only calculates and validates geometry.
- Runtime validation of native NVMe and QEMU/UEFI guest boot in this environment.

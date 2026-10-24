# CHOS 0.30.0 — remaining work and release gate

This file distinguishes verified host-side work from goals that remain open. Host mocks do not prove hardware reliability. A feature is not considered complete because a parser compiles or an image has valid partition structures.

## Additional foundations completed in this snapshot

- [x] Add a polled VirtIO-MMIO RNG implementation with host-mock tests, nonblocking entropy interface and bounded user syscall; runtime hardware behavior remains unverified.
- [x] Attach VirtIO RNG to QEMU run targets and check successful initialization in the QEMU serial smoke-test script.
- [x] Add a pure installer geometry/confirmation planner with disk-identity revalidation; it performs no disk I/O.
- [x] Expand HTTPS URL normalization, port 443 default, and secure-scheme link/redirect handling; keep transport fail-closed until TLS verification exists.
- [x] Restore Windows 9x/2000-era classic desktop colors and beveled UI elements.

## Completed or added in 0.30.0

- [x] Validate the whole claimed UEFI FDT range before traversing FDT offsets.
- [x] Poll existing VirtIO input in the UEFI path only when the FDT confirms the expected QEMU MMIO aperture.
- [x] Add host-tested USB HID descriptor parsing and bounded bitfield helpers.
- [x] Refresh the embedded wallpaper and core taskbar palette.
- [x] Add a disposable QEMU smoke-test workflow and package all current demo/apps in the prepared image.
- [x] Regenerate release outputs, source, validation documentation and checksums.

## Still required to meet the full 0.30.0 ambition

- [ ] Implement and review a native xHCI host-controller driver: BAR mapping, reset/run state, command/event rings, transfers, timeout paths, and bounded DMA ownership.
- [ ] Implement USB enumeration, hub support where needed, control transfers, endpoint discovery, and interrupt-IN HID report transport, then attach keyboard/mouse events to the shared input subsystem.
- [ ] Test real interactive keyboard and mouse input in VMware Fusion. PCI identification alone is not proof of a working driver.
- [ ] Integrate a reviewed freestanding TLS library and real TLS transport, CA-root management, certificate-chain and hostname verification, trustworthy-clock policy, SNI, fail-closed entropy handling, and live HTTPS tests. Do not write cryptography from scratch or enable verify-disabled mode.
- [ ] Implement native vmxnet3 only after validating its BAR, queue, DMA, interrupt/polling, and reset behavior on an exposed VMware controller; preserve the working QEMU VirtIO path.
- [ ] Implement a real graphical installer with disk selection, destructive-operation confirmation, GPT creation, FAT32 ESP creation/file deployment, CHFS partition initialization, verification, failure recovery, and persistent first-run configuration.
- [ ] Demonstrate ISO boot → installer → installation onto a disposable disk → reboot without ISO → desktop.
- [ ] Runtime-validate native NVMe read/write/flush on a disposable virtual disk or disable automatic probing by default until validation is available.
- [ ] Run QEMU direct-boot and UEFI smoke tests, including keyboard, mouse, display, storage, networking and shutdown. Run Fusion integration tests on actual Fusion.
- [ ] Continue GUI refinements and add user-visible settings/system-information utilities without weakening input, storage or window-manager regression coverage.

## Release rule

Do not call the build fully validated unless actual guest boot and end-to-end install tests pass. Report each test as `PASS`, `FAIL`, `NOT RUN`, or `BLOCKED`, and distinguish compile-time/parser checks from hardware runtime checks.

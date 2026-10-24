# Codex engineering handoff — CHOS 0.30.0 Classic Desktop

## Objective

Turn the supplied CHOS ARM64 source into a tested, stable development release with a polished Windows 9x/Windows 2000-era original visual identity, dependable input/display/storage/network drivers, safe graphical installation, and real HTTPS support. Do not equate a successful compile with release readiness.

## Repository orientation

- `kernel/core/`, `kernel/platform/`, `kernel/arch/aarch64/`: kernel/boot/platform/drivers.
- `kernel/gfx/`: freestanding GUI, desktop, window manager, taskbar and classic theme.
- `kernel/fs/`: CHFS and GPT/block partition views.
- `userland/examples/browser*.c`: bounded browser and HTTP client code.
- `userland/libchos/`: userland ABI/syscall wrappers.
- `tools/mkchos_install_disk.py`: host-side prepared image generator, not an installer.
- `include/chos/usb_hid.h`, `kernel/platform/usb_hid.c`: descriptor parser foundation only.
- `include/chos/entropy.h`, `kernel/platform/virtio_rng.c`: polled VirtIO RNG groundwork; runtime unverified.
- `include/chos/installer.h`, `kernel/core/installer_plan.c`: pure no-write GPT layout and confirmation planner.
- `docs/HTTPS_TLS_BOUNDARY.md`, `docs/INSTALLER_PLAN.md`, `docs/DRIVER_STATUS.md`: explicit security and scope boundaries.

## Required work, in priority order

### A. Build a real USB input path

1. Inspect the existing PCI config/BAR APIs, ACPI/FDT/boot memory map, MMU and VirtIO/device abstractions before adding a driver.
2. Implement xHCI only when the controller is safely identified and mapped; use bounded reset/run state machines, validated capabilities, command/event rings, transfer rings, DMA-safe aligned memory, cache maintenance, event completion handling, and teardown/recovery.
3. Enumerate root ports and USB devices using control transfers; implement device/configuration/interface/endpoint descriptor parsing and robust timeouts. Add hub support if the target configuration requires it.
4. Implement HID keyboard and mouse interrupt-IN report transport, connect to the shared input/event path, handle report IDs and keyboard rollover, pointer buttons/scrolling, hotplug, malformed descriptors and endpoint stalls.
5. Do not hardcode a PCI ID or controller address without checking the VM's actual PCI inventory. Verify VMware Fusion device behavior and preserve QEMU VirtIO input.
6. Add host unit tests for ring logic, descriptors, and fault paths, then live tests for keyboard, mouse, wheel, hot-unplug and no-input timeout.

### B. Implement real HTTPS; do not write homemade crypto

1. Select and vendor a reviewed TLS implementation suitable for a freestanding AArch64 CHOS userland. Verify the upstream version, license, required compilation units, entropy callbacks, portability, and API. Do not rely on downloading dependencies at build time.
2. Adapt CHOS TCP sockets for TLS partial I/O, handshake state, timeouts, orderly shutdown, connection reset and cancellation.
3. Integrate `chos_random_bytes()` as an entropy callback only after reviewing the VirtIO RNG hardware path and testing guest entropy. Fail handshakes on missing/short entropy; no pseudo-random fallback.
4. Implement and test certificate-chain and hostname verification, SNI, CA-root management, trustworthy clock policy, expiration handling, and resource limits. Never ship “verify=false”.
5. Keep HTTPS blocked until all checks are available. Never retry an HTTPS failure over HTTP.
6. Add local TLS fixtures and tests for valid chain/hostname, wrong hostname, untrusted/expired certificates, malformed/truncated records, short reads/writes, timeout, redirects, and downgrade prevention. Then test public HTTPS from a live QEMU guest.
7. Document protocol-version policy, root-update lifecycle, clock source, TLS library license and exact supported behaviors.

### C. Complete the installation path

1. Implement a block-device interface that supports reliable capacity/sector geometry, stable identities, error propagation, cache/flush semantics and test doubles.
2. Keep `chos_installer_prepare_plan()` side-effect-free; revalidate the selected disk identity and geometry immediately before any write. Never treat its plan cookie as a security primitive.
3. Implement GPT read/write with primary and backup header + entry-array CRC correctness, 1 MiB alignment, overflow checking and rollback/failure tracking.
4. Implement FAT32 ESP creation, copy the ARM64 UEFI app and kernel, initialize the CHOS data partition, verify writeback, and support interruption/retry without falsely advancing state.
5. Build the graphical wizard with original classic styling: welcome, license/project info if needed, target drive, explicit data-loss confirmation, partition summary, progress, error/retry, finish/reboot. Full keyboard navigation is required.
6. Add first-boot/user configuration only for settings that are actually supported. Ensure the installed system boots independently of the ISO.
7. Use disposable block-device fixtures and write-failure injection, then test ISO boot → install → poweroff/reboot from disk → input works → read/write persistence.

### D. Improve existing platform drivers and stability

- Build from clean with `make CC=clang LD=ld.lld -j4`, UEFI ISO and installer-image targets.
- Preserve QEMU VirtIO GPU/input/block/network and the direct/UEFI boot paths.
- Runtime-test VirtIO RNG, network, block and graphics. Check DMA addresses, memory map/cache requirements and bounded timeouts.
- Validate NVMe on a disposable virtual device before exposing it as stable; otherwise keep experimental or disable probing by default.
- Implement VMware `vmxnet3` only if the selected VM configuration actually presents it and the networking abstractions support it. Do not claim it from VirtIO tests.
- Keep driver failures nonfatal where safe, log why support is unavailable, and avoid a kernel hang if a controller never responds.
- Review process/user memory validation in the new random-bytes syscall and every related syscall path.

### E. Refine the classic UI

- Use original CHOS artwork, a Windows 9x/2000-era gray beveled taskbar, classic navy title bars, teal desktop option and proportional readable typography; do not redistribute Microsoft artwork/fonts or use Microsoft branding.
- Apply one coherent widget/button/focus/selection/dialog palette; use pressed states, keyboard focus, consistent borders, and proper disabled/error states.
- Verify every clickable control does something and every keyboard interaction works. Avoid screenshot-only polish that does not match state/behavior.
- Persist supported theme, wallpaper, and display settings without corrupting user configuration. Test all UI scales and safe fallback resolutions.
- Preserve accessibility basics: contrast, focus visibility, keyboard navigation, usable hit targets, and clear progress/error text.

## Required validation and release gates

- Run `make clean`, clean direct kernel build, UEFI ISO build, prepared image build, `make test CC=clang LD=ld.lld -j1`, and `make test-sanitize CC=clang LD=ld.lld -j1`.
- Run Python tests independently too if an aggregate command hits an execution timeout; record each exact result, never convert timeout into pass.
- Install QEMU plus ARM64 UEFI firmware on a capable test machine and perform actual direct-boot and UEFI boots. Capture serial logs and record the machine/software configuration.
- For every driver, test both successful operation and device-unavailable/failed-initialization behavior.
- Run live VMware Fusion tests on the user's target virtual hardware for keyboard/mouse and network. If the driver is incompatible with actual presented devices, capture PCI/USB inventory and document exactly what is missing.
- Demonstrate a full end-to-end fresh install to a disposable disk and boot without installation media. Test write persistence, reboot and recovery/error paths.
- Demonstrate HTTPS against local valid/invalid TLS fixtures and public websites. Verify failures for bad certificate/hostname and missing entropy.
- Generate an artifact SHA-256 manifest after all changes. Include source, ISO, installed-image candidate, EFI loader, direct/UEFI kernels, complete test results and known issues.

## Non-negotiable rules

- Do not claim a driver, HTTPS, installation, or VMware feature works because it compiles or its parser passes host tests.
- Do not implement cryptographic primitives from scratch or disable certificate verification to make a demo load.
- Do not autoformat a disk, overwrite the only working disk, or silently switch a failed HTTPS URL to HTTP.
- Do not hide failing, skipped, timed-out, or unavailable tests. Mark PASS / FAIL / NOT RUN / BLOCKED independently.
- Do not remove the explicit safe-failure behavior until the replacement is implemented and tested.
- Preserve user data and existing boot paths. Make changes incrementally and maintainable; explain architectural changes.

## Current known gaps in the supplied tree

The HTTPS URL parser works but TLS transport/certificate verification is not integrated. The installer is a no-write geometry/confirmation planner plus a host image builder. USB HID parsing exists but xHCI, USB enumeration and VMware USB keyboard/mouse integration do not. Native VMware `vmxnet3` is absent. The VirtIO RNG driver and syscall passed host-mock/build tests but have not been run in a live guest. NVMe remains experimental. See the three boundary/status documents before changing these subsystems.

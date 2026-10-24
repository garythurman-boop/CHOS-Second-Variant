# CHOS ARM64 Operating System — 0.30.0

Current [Fusion follow-up](docs/STAGE1_FUSION_FOLLOWUP.md) tracks the reported boot/input failure and subsequent fixes.

Stage 1 development changes and current runtime evidence are described in
[current Stage 1 report](docs/STAGE1_FINAL_REPORT.md), [driver status](docs/DRIVER_STATUS.md)
and the [hardware support matrix](docs/HARDWARE_SUPPORT.md). See the
[build expectations and test checklist](docs/STAGE1_TEST_CHECKLIST.md) before testing. The original 0.30.0
feature notes below describe the supplied baseline; they are not the latest driver status.

For this cloud machine, run `bash scripts/setup-cloud.sh`, then source
`/workspace/chos-tools/activate`. The original ZIP/prebuilt baseline is preserved.

CHOS is a lightweight AArch64 hobby OS with a classic Windows 9x/2000-era desktop prototype, a CHFS filesystem, a bounded HTTP text-preview browser, and separate QEMU direct-boot and UEFI boot paths. This is a substantial development snapshot, **not yet a production-ready general-purpose OS**. Native VMware USB/HID transport, real TLS/HTTPS, the graphical on-device installer, and VMware `vmxnet3` remain incomplete and are explicitly listed in the validation report.

## What changed in 0.30.0

- **UEFI input-path fix:** the UEFI desktop now initializes and polls the existing VirtIO input driver only when a valid FDT advertises the QEMU VirtIO-MMIO aperture. The full claimed FDT range is checked against the preserved EFI memory map before parsing. This is a QEMU-specific safeguard, not native VMware USB support.
- **USB HID parser foundation:** bounded, allocation-free parsing of HID report descriptors, report IDs, input field offsets and flags, signed field extraction, and mouse/keyboard capability hints. Host tests cover keyboard/mouse descriptors, report IDs, malformed/truncated items, invalid stacks, cross-byte bitfields, and range checks. It is not an xHCI/USB transport driver and by itself does not fix VMware input.
- **Classic desktop styling:** neutral-gray taskbar and controls, 3-D bevels, classic navy title bars, teal default background, and original CHOS landscape wallpaper as an optional alternative.
- **Build and test workflow:** versioned ISO/image outputs, a separate disposable disk for the optional automated QEMU serial smoke test, expanded application payload inclusion in the prepared disk image, and improved host-side regression coverage.
- **VirtIO RNG groundwork:** a bounded nonblocking modern VirtIO-MMIO RNG driver, host-mock tests, and a bounded `CHOS_SYS_RANDOM_BYTES`/`chos_random_bytes()` API. QEMU targets now expose `virtio-rng-device`; the actual guest driver path remains unverified here.
- **Installer groundwork:** a pure GPT layout planner with 1 MiB alignment, 64 MiB EFI/data minimums, plan geometry checks, disk identity revalidation, mandatory destructive confirmation, and monotonic stage gates. It never writes disk sectors; no graphical installer is included.
- **HTTPS groundwork:** HTTPS URL parsing and relative-link/redirect handling, port 443 defaults, and fail-closed refusal to open an unencrypted connection. A TLS implementation, certificate/hostname verification, managed CA roots, and trusted-clock policy are not included, so HTTPS browsing remains unavailable.
- **Existing functionality retained:** AArch64 QEMU kernel, UEFI loader and relocatable kernel, VirtIO graphics/input/block/network paths, CHFS storage, read-only GPT parsing, the prepared-image builder, and host-testable NVMe parsing.

## Build

Requirements: Clang/LLD with AArch64 bare-metal support, a host C compiler, Python 3, and GNU Make.

```sh
make clean
make CC=clang LD=ld.lld -j4
make uefi-iso CC=clang LD=ld.lld
make install-disk-image CC=clang LD=ld.lld
```

Outputs are under `build/`:

- `build/chos.elf` — direct QEMU kernel (`ET_EXEC`).
- `build/uefi-kernel/chos.elf` — relocatable UEFI kernel (`ET_DYN`).
- `build/uefi/BOOTAA64.EFI` — ARM64 UEFI application.
- `build/chos-0.30.0.iso` — bootable ISO9660/EFI image.
- `build/chos-0.30.0-installed.img` — experimental 256 MiB disk image with a FAT32 EFI System Partition and CHFS data partition.

The installed image is generated as a file; the builder is not an installer wizard and has not been proven to boot in this build environment. Use a **disposable VM and virtual disk** while testing.

## QEMU workflow

For an interactive desktop test:

```sh
make run-gui-silent CC=clang LD=ld.lld
```

For a live, automated serial smoke test (requires `qemu-system-aarch64`):

```sh
make qemu-smoke CC=clang LD=ld.lld
```

The smoke test creates `build/qemu-smoke-disk.img`, not the persistent developer disk at the repository root. It expects the guest to report kernel entry, CHFS mount, desktop presentation, VirtIO input initialization, and VirtIO RNG initialization, and saves output to `build/qemu-smoke.log`. It is a live test only when run on a host with QEMU installed.

To run the UEFI ISO, supply an ARM64 UEFI firmware image:

```sh
make run-uefi UEFI_FIRMWARE=/path/to/AAVMF_CODE.fd CC=clang LD=ld.lld
```

`make run-installed-uefi UEFI_FIRMWARE=/path/to/AAVMF_CODE.fd CC=clang LD=ld.lld` is an experimental installed-image boot attempt; whether the firmware can boot the VirtIO disk is QEMU/EDK2-dependent. It is not a bootability guarantee.

## Host validation

```sh
make test-usb-hid CC=clang LD=ld.lld
make test CC=clang LD=ld.lld -j1
make test-sanitize CC=clang LD=ld.lld -j1
```

Host-side tests do not substitute for a live QEMU or VMware run. `make qemu-smoke` should be used on a system with a working AArch64 QEMU installation.

For repeatable HTTP browser fixtures, run `python3 tools/browser_test_server.py --bind 0.0.0.0 --port 8080` on the host and navigate to `http://10.0.2.2:8080/` in QEMU user-mode networking. The browser is a limited HTTP text preview. HTTPS URL handling is more complete, but HTTPS connections remain blocked until a reviewed TLS library, certificate-chain and hostname verification, CA roots, trustworthy clock policy, and live tests are added. JavaScript, complete CSS layout, real image rendering, forms and downloads are also not supported.

## Security and installer groundwork

See [TLS boundary](docs/HTTPS_TLS_BOUNDARY.md), [installer planner](docs/INSTALLER_PLAN.md), and [driver status](docs/DRIVER_STATUS.md) for exact implementation and verification status. A successful URL parse does not imply HTTPS security; the browser refuses HTTPS until TLS peer validation exists. The installer plan is non-destructive code only, not a write path.

## Important limitations

- **VMware USB input:** native PCI xHCI controller startup, USB enumeration/hub support, USB HID keyboard/mouse polling, and Fusion runtime tests are not implemented. The UEFI path still may display a desktop without working VMware keyboard/mouse input.
- **VMware networking:** native `vmxnet3` is not implemented; current VirtIO-net support targets the QEMU platform.
- **Graphical installer:** there is no interactive installer wizard, partition editor, on-device deployment/repair flow, or verified first boot from the generated image. `make install-disk-image` creates a prepared disk-image file only.
- **NVMe:** native PCI NVMe remains experimental and disabled by default. An explicitly enabled QEMU test build passes disposable namespace I/O, flush, reboot persistence and injected write-error recovery; physical controllers remain unvalidated. It is limited to namespace 1, plain 512-byte logical blocks, and no metadata; use only a disposable disk for testing.
- **Browser:** HTTP text preview only; the stack is not a modern browser platform.
- **Runtime evidence:** this source/build environment did not have AArch64 QEMU or ARM64 UEFI firmware, nor VMware Fusion. Do not interpret clean compilation or host tests as a successful guest boot.

For the next engineering phase, see the detailed [Codex handoff](docs/CODEX_HANDOFF_0.30.0.md).

See [0.30.0 release notes](docs/RELEASE_NOTES_0.30.0.md), [validation report](docs/VALIDATION_0.30.0.md), and [remaining work](docs/ROADMAP_0.30.0.md).

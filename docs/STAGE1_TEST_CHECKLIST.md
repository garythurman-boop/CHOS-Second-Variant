# Build expectations and Stage 1 test checklist

The source on GitHub main is a development kernel, desktop and driver test platform.
It is not ready for daily use, broad hardware compatibility or installation onto a
valuable disk. The supplied prebuilt/ directory is the original 0.30.0 baseline;
rebuild current main or use the final artifacts from the Stage 1 development package.
Do not infer which version an ISO contains solely from its unchanged 0.30.0 filename.

## What to expect

| Configuration | Demonstrated behavior | Current limits |
|---|---|---|
| QEMU direct ELF boot | Classic 800×600 desktop; VirtIO keyboard/relative mouse; wheel transport; window drag/maximize/restore; CHFS write/read/flush and persistence after a new guest; DHCP/ARP/ICMP; hardware RNG and bounded EL0 random syscall | QEMU virt, one CPU; general window resize, modern browser/TLS and an on-device installer are not provided |
| QEMU ARM64 UEFI ISO | EFI loader, ACPI discovery, GOP/RAMFB desktop, native xHCI keyboard/relative mouse/wheel, window operations and 20 mouse reconnects | Direct-attached HID only; hubs and absolute USB tablets unsupported; ACPI-only runtime has no working block/network/RNG attachment |
| QEMU prepared disk image | Firmware reads the EFI boot files from a disposable disk and reaches the desktop; native USB checks pass | Booting prepared files is not an installer test or proof of kernel filesystem persistence in this UEFI profile |
| VMware Fusion on Apple Silicon | ARM64 EFI loader/GOP/xHCI source exists and builds | Actual boot/input/network/storage support unverified; real USB topology, adapter and DMA/IOMMU inventory required |
| Pi 5 / ARM64 laptop | Platform requirements and missing driver families documented | Not a supported bootable daily-use configuration |

The desktop contains prototype Console, File Manager, Settings, Diagnostics and text
viewing paths. The automated checks do not establish that every control, application,
display mode or persistence setting works on each platform. HTTPS remains deliberately
blocked without TLS peer verification. NVMe automatic probing is disabled. Current
USB supports relative pointers rather than absolute USB tablets/touchscreens.

## What the engineering environment can implement next

These are open software tasks, not all hardware blockers:

1. Absolute USB HID pointer/tablet decoding, logical-range normalization, report-ID
   handling and live QEMU usb-tablet tests. This may improve Fusion input compatibility
   if its actual presented pointer uses that protocol; it is not a Fusion guarantee.
2. USB hub enumeration, power/reset/status-change handling, route strings and child
   teardown. Low/full-speed devices behind high-speed hubs also need the proper
   transaction-translator contexts. QEMU can exercise part of this topology.
3. Standards-discovered PCI VirtIO transport for block, network and RNG in the
   ACPI-only UEFI path, with DMA/cache ownership, feature negotiation, disposable
   persistence and negative tests. Preserve the working MMIO direct-boot backend.
4. Opt-in QEMU NVMe live read/write/flush and injected-error tests against disposable
   namespaces, followed by fixes before considering any default probe change.
5. Longer input/hotplug/storage/process stress runs, live long-document wheel tests,
   held-key disconnects, display-mode checks, reset/stall recovery and additional
   malformed-device fault injection. Automate the validated checks in CI.

IOMMU/SMMU ownership and additional platform drivers need architecture work plus
model-specific evidence. Implement vmxnet3 only after Fusion inventory shows that
adapter. QEMU testing of any new driver is useful but cannot close the Fusion/Pi/laptop
runtime gates. A TLS integration or graphical installer is separate from this Stage 1
kernel/platform reliability work.

## Manual acceptance checklist

Use a disposable VM and virtual disk. Save the exact Git commit, build checksum,
QEMU/Fusion version, host architecture, RAM, firmware and device configuration.
For each item, record PASS, FAIL, BLOCKED or NOT RUN; include a log or screenshot.

### Boot and recovery

- [ ] Cold-boot the direct QEMU kernel; verify serial entry, MMU self-test, filesystem,
  network, input and entropy initialization without unexpected faults.
- [ ] Boot the UEFI ISO using ARM64 firmware and a supported framebuffer. Verify that
  the loader reaches the desktop and records PCI/xHCI status rather than merely
  showing a firmware screen. A black screen is a failure to diagnose.
- [ ] Boot a fresh copy of the prepared image with the ISO detached. Record this as
  prepared-image boot; do not call it installation or UEFI filesystem persistence.
- [ ] Boot without optional RNG/network/input devices. The supported remaining
  workflow should stay responsive and identify unavailable devices.

### Keyboard, pointer and desktop

- [ ] Type in Console; test letters, numbers, punctuation, Shift/Ctrl/Alt, Caps Lock,
  Backspace, Enter, Tab, arrows and key releases. Check for stuck modifiers and repeats.
- [ ] Move the pointer to every edge/corner and reverse direction; test small and fast
  movement, left/right click, buttons and wheel up/down. Open a long document and
  confirm that visible scrolling follows the wheel where that viewer supports it.
- [ ] Drag windows and desktop icons; open/close menus and dialogs. Test focus,
  close/minimize/restore and maximize/restore. General free-form resize is not implemented.
- [ ] Unplug/reconnect mouse and keyboard independently, including while a key or
  button is held. Confirm state releases and that the other device continues working.
  Repeat at least 20 times; then leave the VM idle and resume input.
- [ ] Record pointer/window responsiveness and host CPU use during at least five
  minutes of desktop idle. No-response controllers require fault-injection testing;
  do not equate an untouched mouse with a broken controller.

### Storage, networking, entropy and processes

- [ ] In the direct profile, create/read a uniquely named file, flush, close the VM,
  start a new VM on the same disposable disk and verify the exact contents.
- [ ] Test a large file spanning multiple 4 KiB block requests and an out-of-space
  failure. Record failures without treating incomplete writes as durable success.
- [ ] In the direct profile, check DHCP configuration and ping 10.0.2.2; verify an
  actual reply. Test disconnected networking without freezing the desktop.
- [ ] Run rng-test and the automated EL0 RNG regression with RNG present and absent.
  Missing hardware must produce unavailable/EAGAIN, never fallback bytes. Do not
  post entropy output or interpret a successful read as a quality certification.
- [ ] Launch/exit several apps and test a deliberate user fault using the existing
  regression commands; verify that the desktop survives and resources are reaped.
- [ ] Use the host/sanitizer suite for malformed descriptors, ring transitions,
  timeouts, stalls, invalid pointers and allocation/process contracts.

### Fusion, Pi and laptop evidence

- [ ] For Fusion, record exact firmware, PCI segment:BDF, vendor/device/class, BARs,
  USB topology and whether the pointer is relative, absolute or behind a hub.
  Save the initialization log before testing input. If it fails, capture that log and
  the precise action/result instead of assuming xHCI or vmxnet3 is the right fix.
- [ ] For Pi/laptops, obtain the exact board/model and firmware/DT/ACPI/IORT inventory
  with a serial recovery method before attempting driver-specific tests. Do not infer
  support from an ARM64 build. Keep NVMe experimental and restricted to disposable media.

## Reproduce automated checks in this cloud environment

```
source /workspace/chos-tools/activate
cd /workspace/CHOS-Second-Variant
make CC=clang LD=ld.lld -j4 all uefi-iso install-disk-image build/qemu-smoke-disk.img build/userland/rng_test.chcx
make test CC=clang LD=ld.lld -j1
make test-sanitize CC=clang LD=ld.lld -j1
make qemu-smoke CC=clang LD=ld.lld
```

For a live direct run and a no-RNG run, use new output paths:

```
python3 tests/qemu_platform.py --out /workspace/chos-evidence/MY-DIRECT --qemu-data /workspace/chos-tools/root/usr/share/qemu
python3 tests/qemu_platform.py --no-rng --out /workspace/chos-evidence/MY-NO-RNG --qemu-data /workspace/chos-tools/root/usr/share/qemu
```

For native USB via UEFI and prepared-image boot:

```
python3 tests/qemu_platform.py --mode uefi --out /workspace/chos-evidence/MY-USB --firmware /workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd --qemu-data /workspace/chos-tools/root/usr/share/qemu
python3 tests/qemu_platform.py --mode installed --disk build/chos-0.30.0-installed.img --out /workspace/chos-evidence/MY-INSTALLED --firmware /workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd --qemu-data /workspace/chos-tools/root/usr/share/qemu
```

On another machine, use that machine's toolchain/firmware paths. scripts/setup-cloud.sh
is specific to the prepared Debian cloud environment. Read STAGE1_VALIDATION.md for
actual prior results and HARDWARE_SUPPORT.md for per-platform limits. The original
package's results remain evidence for its recorded build; documentation updates do
not imply that a new runtime test was performed.

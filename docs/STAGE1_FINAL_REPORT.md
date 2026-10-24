# Stage 1 final local validation and external gates

For subsequent user-reported Fusion desktop/input/clock results and their source
corrections, see [current Fusion fixes](FUSION_DRIVER_FIXES.md) and the
[driver acceptance guide](DRIVER_ACCEPTANCE.md). The local evidence below preserves
the earlier Stage 1 checkpoint and is not a claim that the later Fusion regression
passed on real hardware.

The implemented QEMU profiles pass local acceptance. Cross-platform Stage 1 remains
open until the corrected build is tested on Fusion and the other declared targets.
The user's previous Fusion screenshot proves a failing EFI/GOP/xHCI path, not a
successful desktop. No Fusion success is inferred from QEMU.

Read the [Fusion follow-up](STAGE1_FUSION_FOLLOWUP.md) for individual defects,
changes and preserved development failures; the [hardware matrix](HARDWARE_SUPPORT.md)
for exact support boundaries; and the [test checklist](STAGE1_TEST_CHECKLIST.md)
for commands and manual target steps. Source is pushed to GitHub main after each
completed iteration. Boot diagnostics identify the source revision and dirty state.

## Acceptance evidence

Logs are retained under `/workspace/chos-evidence/stage1b/`; delivery bundles include
the reports, test logs, result JSON and artifact hashes. Test disks and firmware
variable stores are disposable copies. Original baseline evidence remains outside
`build/`, so the final clean build does not erase it.

| Gate | Local outcome | Evidence |
|---|---|---|
| Clean Clang/LLD direct, UEFI ISO and prepared-image builds | PASS | `final-clean-build.log` |
| Applicable aggregate host regressions | PASS | `final-clean-host.log` |
| Aggregate ASan/UBSan checks | PASS | `final-clean-sanitize.log` |
| Direct serial smoke | PASS | `final-clean-smoke.log` |
| Original MMIO VirtIO input/GPU/block/net/RNG and EL0 random validation | PASS | `final-direct/result.json` |
| Native PCI xHCI → USB enumeration → HID → shared GUI | PASS for direct/nested full-speed hubs, keyboard, relative mouse and absolute tablet | `hub-relative-1`, `hub-aligned-live`, `final-pci-stress` |
| Hot-unplug/reconnect, held Shift/button release, ring wraps | PASS | 20 pointer reconnects and whole-hub removal in the live USB profiles |
| ACPI-only PCI VirtIO block/network/RNG | PASS | `pci-virtio-live2`, `pci-no-rng`, `final-pci-stress` |
| Prepared-image boot and actual kernel GPT/CHFS persistence | PASS | `final-prepared/result.json` |
| Missing controller / empty controller without splash freeze or busy loop | PASS | `final-no-usb`, `final-empty-usb` |
| Experimental NVMe read/write/flush/reboot and real write-error containment | PASS in explicitly enabled QEMU builds | `final-nvme`, `final-nvme-error`; normal probing stays disabled |
| Descriptor/ring/resource/GIC/block/net/NVMe failure bounds | PASS in host and sanitizer fault tests | Included in final aggregate logs |
| Fusion corrected-build boot/input/storage/network/entropy | BLOCKED: target VM retest and adapter inventory needed | New ISO supplied; prior screenshot and machine details recorded |
| Pi 5 and ARM64 laptop discovery/boot/device checks | BLOCKED: exact target hardware/firmware required | Matrix and manual closure steps |

## Failure and ownership contracts

* Native xHCI and PCI VirtIO/NVMe require validated firmware resources and identity
  DMA with MMU/cache off. Unknown or SMMU IORT topology fails closed. General IOMMU
  ownership remains unsupported; it requires target evidence and additional driver
  architecture before enabling those configurations.
* xHCI reset/command/control waits have counter deadlines and iteration caps. An
  absent port or normal interrupt-IN NAK does not wait for input. Failed transfers
  release held input and cancel the slot; failed controller/command state quarantines
  permanent DMA buffers until reboot. HS/SS hub live evidence remains pending.
* VirtIO block and experimental NVMe retain DMA storage on uncertain completion.
  Invalid completions or a timeout prevent reuse; NVMe flush failure also quarantines.
  CHFS uses a single storage owner/lock and never formats an unknown disk on boot.
* VirtIO net validates TX completion IDs/index advancement and RX overruns. Repeated
  successful initialization is idempotent; failure forbids unsafe rebinding. Input,
  RNG and block likewise preserve their owned queues; GPU/sound reject rebinding.
* RNG returns actual device bytes or unavailable/pending. No counter/timer fallback
  exists. Live EL0 tests cover invalid pointers, read-only/wrapping ranges, request
  bounds and output guards in the direct MMU-enabled process runtime.
* GICv3 explicitly controls EOI mode and both Group 1 views. UEFI proves timer tick
  progress before WFI. A deliberately unrouted timer keeps the desktop polling and
  produces a warning, at the cost of a full CPU. Efficient idle is not claimed for
  a platform with no working wakeup interrupt. Normal missing USB/RNG devices do
  not cause that degraded mode.

## Performance observations

These are short shared-cloud QEMU measurements, including firmware, screenshot/QMP
work and injected delays. They are not Fusion results or latency percentiles.
The baseline and historical comparison method is retained in STAGE1_VALIDATION.md.
Stronger final window assertions inspect actual ID, maximize state and restore/move
geometry; older screenshot-difference assertions alone are superseded.

| Measurement | Original direct baseline | Final direct | Final prepared UEFI |
|---|---:|---:|---:|
| Kernel/console from QEMU launch | 0.134 s | 0.159 s | 6.595 s |
| Desktop after minimum splash | 3.217 s | 3.258 s | 9.668 s |
| Keyboard to visible change | 55.62 ms | 53.08 ms | 105.70 ms |
| Pointer to visible change | 28.61 ms | 43.65 ms | 44.60 ms |
| Host CPU over two-second idle | 50.99% | 1.50% | 1.00% |

The final pointer method includes a fixed 40 ms injection delay, so its value cannot
be compared as raw pointer latency with the older method. Prepared-image keyboard
measurement overlapped other emulator/build activity. These observations do not
establish a product latency regression. Window maximize/move final observations are
about 171–223 ms including deliberate button/motion delays. Normal missing-controller
and empty-controller observations use 1.00% and 0.66% host CPU over three seconds.
A 120-cycle mixed keyboard/pointer and flushed-storage run adds a longer interleaving
check; all 120 cycles passed over 192.226 seconds, followed by a fresh guest boot
on the same copied kernel/ISO and successful persistence verification. Details are
in `final-pci-stress/result.json`.

## Closing external gates

Fusion retest configuration: Fusion 26H1u1, macOS 27, M1 MacBook Air, one CPU,
2 GiB RAM. Boot the current standard ISO. Verify splash completion and input, then
capture Console `pci` and `bootlog`, especially timer verification and USB VID/PID,
root/route/slot and error lines. Test modifiers/releases, pointer/buttons/wheel,
drag/maximize/restore, menus/dialogs, idle and hotplug. Actual NIC/storage IDs decide
whether vmxnet3 or another native backend is required. No vmxnet3 implementation is
selected without that inventory. Do not use an experimental NVMe build on valuable
media. Pi 5 needs the board/firmware/DT and RP1 ownership data; laptops need model,
ACPI/IORT inventory and recovery access. General UEFI EL0 processes, TLS/HTTPS and
an on-device installer remain outside the validated Stage 1 profile.

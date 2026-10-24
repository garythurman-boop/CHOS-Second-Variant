# CHOS driver status — Stage 1 development

This report supersedes the driver summary in the original 0.30.0 handoff. See
[hardware matrix](HARDWARE_SUPPORT.md) and [validation evidence](STAGE1_VALIDATION.md).
Builds and host mocks do not establish hardware support. Stage 1 is not declared complete.

| Subsystem | Implemented and verified | Remaining limits |
|---|---|---|
| Direct ARM64 boot / MMU | QEMU serial boot; translation, guard pages, dynamic aliases with 512 MiB RAM | Single CPU; QEMU platform addresses; no universal ARM64 boot claim |
| UEFI / GOP | AAVMF ISO and prepared-image boot with QEMU RAMFB | Generic UEFI EL0/process runtime and PCI VirtIO are not established |
| PCI discovery / resources | ACPI MCFG inventory; EFI root-bridge resource snapshot; sized assigned BARs; CPU/bus translation; RAM-overlap rejection | No PCI resource allocator; platform must assign BARs; no native IOMMU ownership |
| Native xHCI | Bounded ownership/halt/reset/start; no-op command validation; command/event/transfer rings; 32/64-byte contexts; scratchpads; control transfers; root-port HID enumeration; interrupt-IN reports | One controller, 8 devices, 32 root ports, 4 HID endpoints/device, 256-byte packets, 4096-byte descriptors; standard speed IDs only; no hubs, streams, high-bandwidth endpoints or absolute USB tablets |
| Native USB HID | Keyboard/modifiers/rollover/report IDs, signed relative pointer, buttons, wheel, disconnect key release; shared input path; live QEMU keyboard/mouse, drag, maximize/restore and 20 reconnects | USB LEDs/output reports absent; malformed reports quarantine after 8; endpoint stall disables slot until unplug; no physical keyboard/Fusion evidence |
| VirtIO GPU/input | Direct QEMU visible keyboard, relative pointer, window movement/maximize; software and host GUI regressions | Arbitrary free-form resize is not implemented; VirtIO tablet backend retained but not part of this stage's new relative-mouse live profile |
| VirtIO block / CHFS | Disposable disk write/flush/read and persistence across a fresh guest; permanent DMA bounce buffers; cache ownership; completion validation; host timeout/error mocks | 512-byte sectors; 4 KiB chunks; single serialized owner through storage lock; timeout quarantines until reboot |
| VirtIO net | QEMU DHCP lease, ARP reply and ICMP echo exchange | No VMware networking result; QEMU user networking only |
| VirtIO RNG | Guest receives real queue bytes; absent-device run returns unavailable; EL0 pointer/length/guard tests; host feature/bounds/reinitialization tests | No entropy quality/certification claim; no timer/counter fallback; unavailable without supported transport |
| NVMe | Parser/build coverage; automatic probing disabled | Experimental; no guest I/O/flush acceptance; opt-in source macro only after a separate disposable-controller test |
| VMware vmxnet3 | Not implemented | Actual Fusion PCI inventory is required before selecting/implementing an adapter |
| Audio | Existing bounded VirtIO sound implementation | Optional and untested here; silent profile |

## Ownership and failure contracts

* UEFI handoff ABI version 4 copies EFI PCI root-bridge resource windows **before**
  ExitBootServices. The kernel never invokes those firmware callbacks afterward.
  BAR sizing temporarily disables decode/bus mastering and restores the original
  BARs/command register. A window must translate the entire BAR unambiguously and
  cannot overlap RAM. SPCR/GIC addresses come from validated ACPI tables; omitted
  EFI device descriptors are permitted only with that independent declaration.
* Native xHCI currently requires identity DMA with MMU and data cache disabled.
  SMMU/unknown IORT DMA topology is rejected until an IOMMU driver exists. A controller
  without 64-bit addressing is rejected if its DMA buffers would exceed 4 GiB.
  This is a deliberately narrow contract, not a general DMA mapping API.
* xHCI DMA rings, contexts and buffers are permanent aligned BSS, reserved as part
  of the kernel image. Device removal issues Disable Slot before reusing its storage.
  Controller failures stop it with a bounded halt attempt and clear PCI bus mastering;
  uncertain controller state is quarantined until reboot. No late DMA buffer is freed.
* Controller/command/control waits have a one-second deadline and an independent
  iteration cap. Port reset is bounded to 250 ms. Empty interrupt-IN queues use
  normal NAK behavior: polling never waits for a user to press a key. Initialization
  retries require an unplug/reconnect, avoiding repeated probes on every GUI tick.
* VirtIO input uses separate cache lines for CPU-owned available and device-owned
  used queues. Oversized reports and used-ring overruns fail the device. RNG init is
  idempotent and a used-ring jump is rejected. Block completions validate the used
  index/id, use explicit cache maintenance and preserve static buffers on timeout.
* The storage IRQ-save lock serializes CHFS/device access; no scheduler yield or
  allocation occurs inside the block transport's waits. These contracts are single
  core. IRQ handlers do not call USB enumeration, storage or GUI routines.
* GUI/shell polling sleeps on the periodic timer when initialized. A platform without
  a working timer still has the original polling fallback; efficient idle on such an
  untested platform is not claimed.

No USB hub or USB absolute-tablet support is claimed. The declared live USB profile
uses directly attached QEMU `usb-kbd` and `usb-mouse`. Devices needing unsupported
features fail explicitly, without replacing the working VirtIO backend.

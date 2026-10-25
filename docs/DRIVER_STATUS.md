# CHOS driver status — Stage 1 development

This report supersedes the driver summary in the original 0.30.0 handoff. See
[hardware matrix](HARDWARE_SUPPORT.md), [validation evidence](STAGE1_VALIDATION.md),
and the current [Fusion follow-up](STAGE1_FUSION_FOLLOWUP.md).
Builds and host mocks do not establish hardware support. The user reports 816aa55 desktop/input success on Fusion and authorizes Stage 2. See [installer lifecycle and current limits](STAGE2_VALIDATION.md). Remaining Stage 1 platform gates stay tracked.
The published `2ed3e6d` ISO has a user-reported Fusion panic. See the
[symbolicated failure and MMU-off synchronization correction](FUSION_PANIC_2ED3E6D.md).
See [current Fusion fixes](FUSION_DRIVER_FIXES.md) for the recorded controller-version
refusal, clock defect and composite-HID changes based on the separate tested project.

| Subsystem | Implemented and verified | Remaining limits |
|---|---|---|
| Direct ARM64 boot / MMU | QEMU serial boot; translation, guard pages, dynamic aliases with 512 MiB RAM | Single CPU; QEMU platform addresses; no universal ARM64 boot claim |
| Wall clock | UEFI GetTime snapshot + architectural counter; FDT PL031 in direct boot; calendar/counter/absence host tests and visible QEMU minute rollover | Firmware snapshot accuracy/timezone; no runtime firmware calls or NTP |
| UEFI / GOP | AAVMF ISO and prepared-image boot with QEMU RAMFB | Kernel GUI runs with MMU/cache off; shared browser adapter, scaled desktop and installed-ESP boot-mode requests are implemented; UEFI EL0 remains unavailable |
| Driver acceptance / reports | PCI/FDT registry host/sanitizer tests; actual phased PCI VirtIO/xHCI binding; automatic input-free diagnostic pages | Current catalogue is PCI; new FDT adapters need enumeration glue; fixed capacity, one xHCI controller, no PCI hotplug |
| PCI discovery / resources | ACPI MCFG inventory; EFI root-bridge resource snapshot; sized assigned BARs; CPU/bus translation; RAM-overlap rejection | No PCI resource allocator; platform must assign BARs; no native IOMMU ownership |
| Native xHCI | Bounded ownership/halt/reset/start; no-op command validation; command/event/transfer rings; 32/64-byte contexts; scratchpads; control transfers; root-port HID enumeration; eight queued interrupt-IN reports/endpoint; strict input-context state | One controller, 16 devices, up to 255 root ports, 4 HID endpoints/device, 1024-byte packets, 4096-byte descriptors; standard speed IDs only; bounded hubs/depth; no streams or high-bandwidth burst endpoints |
| Native USB HID | Keyboard/modifiers/rollover/report IDs, signed relative pointer, absolute tablet coordinates, buttons, wheel, disconnect key release; shared input path; live QEMU keyboard/mouse, drag, maximize/restore and 20 reconnects | USB LEDs/output reports absent; malformed reports quarantine after 8; endpoint stall disables slot until unplug; no physical keyboard/Fusion evidence |
| VirtIO GPU/input | Direct QEMU visible keyboard, relative pointer, window movement/maximize; software and host GUI regressions | Arbitrary free-form resize is not implemented; VirtIO tablet backend retained but not part of this stage's new relative-mouse live profile |
| Modern PCI VirtIO transport | ACPI-only UEFI PCI capabilities and firmware BAR translation; block/net/RNG live; bounded host capability/notify tests | Identity DMA, MMU/cache off; no IOMMU mapping, legacy-only PCI or simultaneous mixed transports |
| VirtIO block / CHFS | MMIO and PCI disposable disk write/flush/read and persistence across a fresh guest; permanent DMA bounce buffers; cache ownership; completion validation; host timeout/error mocks | 512-byte sectors; 4 KiB chunks; single serialized owner through storage lock; timeout quarantines until reboot |
| VirtIO net | MMIO and PCI QEMU DHCP lease, ARP reply and ICMP echo exchange | No VMware networking result; QEMU user networking only |
| VirtIO RNG | MMIO and PCI guest receives real queue bytes; absent-device run returns unavailable; EL0 pointer/length/guard tests; host feature/bounds/reinitialization tests | No entropy quality/certification claim; no timer/counter fallback; unavailable without supported transport |
| NVMe | Opt-in QEMU disposable namespace read/write/flush/reboot; injected write error leaves GUI responsive; host/sanitizer reset/completion/allocation faults | Explicit read-only discovery / installed-boot discovery, confirmed graphical installer; identity DMA/MMU/cache off; exactly one controller, namespace 1, 512-byte sectors; new Fusion retest required |
| VMware VMXNET3 | Native revision-1/UPT1 polled Ethernet; QEMU live DHCP/ARP/ICMP, DNS and browser TCP/HTTP fixtures | PCI 15ad:07b0 only; one adapter; 32-entry rings; standard MTU; identity DMA/MMU/cache off; Fusion retest pending |
| Audio | Existing bounded VirtIO sound implementation | Optional and untested here; silent profile |

## Ownership and failure contracts

* UEFI handoff ABI version 5 copies EFI PCI root-bridge resource windows **before**
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
* Modern PCI capability traversal rejects loops, invalid lengths/alignment, out-of-BAR
  offsets and invalid notifications. Queue vectors are disabled for polled operation.
  Zero QueueReady writes are ignored on PCI, where a queue is reset through device
  reset rather than an MMIO-style zero queue-enable write. Driver handles are opaque;
  all device registers use shared accessors.
* VirtIO input uses separate cache lines for CPU-owned available and device-owned
  used queues. Oversized reports and used-ring overruns fail the device. RNG init is
  idempotent and a used-ring jump is rejected. Network initialization is idempotent; invalid TX completions and RX overruns
  quarantine the queue. GPU/sound initialization refuses unsafe rebinding after failure.
  Block completions validate the used
  index/id, use explicit cache maintenance and preserve static buffers on timeout.
* Experimental NVMe uses sized/translated firmware BARs and rejects unverified IORT
  translation or MMU/cache-on DMA. Reset/admin/I/O waits have counter deadlines and
  iteration caps. Completion IDs, queue IDs and heads are validated. All failures
  retain DMA pages; flush failure quarantines as read/write failure does. A failed
  probe cannot allocate/rebind again before reboot.
* The storage IRQ-save lock serializes CHFS/device access; no scheduler yield or
  allocation occurs inside the block transport's waits. These contracts are single
  core. IRQ handlers do not call USB enumeration, storage or GUI routines.
* GUI/shell polling sleeps on the periodic timer when initialized. A platform without
  a working timer still has the original polling fallback; efficient idle on such an
  untested platform is not claimed.

USB absolute-tablet and nested full-speed hub paths are implemented and tested in QEMU.
HS single-TT and SS hub handling remain build/host coverage pending live target evidence. The declared live USB profile
includes QEMU `usb-kbd`, `usb-mouse` and `usb-tablet`, directly attached or behind hubs. Devices needing unsupported
features fail explicitly, without replacing the working VirtIO backend.

Current installed-system work is described in [Fusion follow-up](FUSION_INSTALLED_FOLLOWUP.md).
The Ethernet stack now consumes `chos_net_device_ops_t`; VirtIO and VMXNET3 share
DHCP/DNS/ARP/ICMP/TCP/browser logic. Driver failure does not transfer DMA ownership
to a fallback. Backend errors become offline status. No native IOMMU, Wi-Fi or
unmatched Ethernet controller support is implied.

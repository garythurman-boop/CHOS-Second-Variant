# Hardware support matrix

Statuses describe the listed configuration only. `SUPPORTED AND RUNTIME-TESTED`
means exercised in the named runtime, not certified for production. User-reported
results are labelled and do not establish later-revision support. `BUILD-ONLY`
means source compiled without target runtime evidence. `BLOCKED` requires hardware
or an external environment. `PLANNED` has no complete native path. `UNSUPPORTED`
means an explicit current exclusion.

| Target | Boot path | Firmware | Input | Graphics | Storage | Network | Entropy | Runtime-tested status | Limitations |
|---|---|---|---|---|---|---|---|---|---|
| QEMU ARM64 direct | SUPPORTED AND RUNTIME-TESTED: ELF loader | SUPPORTED AND RUNTIME-TESTED: QEMU FDT | SUPPORTED AND RUNTIME-TESTED: VirtIO keyboard/relative mouse | SUPPORTED AND RUNTIME-TESTED: VirtIO 800×600, windows | SUPPORTED AND RUNTIME-TESTED: VirtIO/CHFS read/write/flush/reboot | SUPPORTED AND RUNTIME-TESTED: DHCP/ARP/ICMP | SUPPORTED AND RUNTIME-TESTED: VirtIO receive and missing-device failure | SUPPORTED AND RUNTIME-TESTED | QEMU virt, Cortex-A72, GICv2, one CPU, 512 MiB; no USB PCI attachment in direct boot |
| QEMU ARM64 UEFI ISO | SUPPORTED AND RUNTIME-TESTED: ISO EFI loader | SUPPORTED AND RUNTIME-TESTED: AAVMF, ACPI, GICv3 | SUPPORTED AND RUNTIME-TESTED: native PCI xHCI, direct/nested hubs, keyboard/mouse/tablet | SUPPORTED AND RUNTIME-TESTED: RAMFB GOP | SUPPORTED AND RUNTIME-TESTED: PCI VirtIO/CHFS persistence | SUPPORTED AND RUNTIME-TESTED: PCI VirtIO DHCP/ARP/ICMP | SUPPORTED AND RUNTIME-TESTED: PCI VirtIO receive/absence | SUPPORTED AND RUNTIME-TESTED | ACPI-only, modern PCI VirtIO; identity DMA, MMU/cache off; kernel GUI applications; no UEFI EL0 process runtime |
| QEMU prepared-image UEFI | SUPPORTED AND RUNTIME-TESTED: EFI files from VirtIO disk | SUPPORTED AND RUNTIME-TESTED: AAVMF | SUPPORTED AND RUNTIME-TESTED: native USB keyboard/mouse | SUPPORTED AND RUNTIME-TESTED: RAMFB GOP | SUPPORTED AND RUNTIME-TESTED: PCI VirtIO GPT/CHFS read/write/flush/reboot | SUPPORTED AND RUNTIME-TESTED: PCI VirtIO DHCP/ARP/ICMP | SUPPORTED AND RUNTIME-TESTED: PCI VirtIO receive | SUPPORTED AND RUNTIME-TESTED | Prepared-image builder is not an on-device installer; kernel GUI runtime with MMU/cache off |
| VMware Fusion / Apple Silicon | USER-REPORTED: c4672fb ISO installs and boots disk-only | User reports GOP entry; clock retest pending | BUILD-ONLY: recorded VMware version/EP0/composite fixes; user now operates keyboard/pointer in 816aa55; later changes need retest | USER-REPORTED: 816aa55 desktop visible | USER-REPORTED: c4672fb NVMe installation and disk-only boot | QEMU-TESTED: native VMXNET3 backend; actual Fusion adapter ID/network retest pending | PLANNED: supported entropy source required | USER-REPORTED desktop/input; later candidates require retest | [Exact panic diagnosis](FUSION_PANIC_2ED3E6D.md): exclusive access to Device-typed RAM; reference project input success remains separate evidence |
| Raspberry Pi 5 | PLANNED: Pi boot/firmware integration | BLOCKED: Pi 5/firmware needed | PLANNED: RP1/PCIe/USB topology and routing | PLANNED: display controller/firmware | PLANNED: SD/eMMC/PCIe storage as exposed | PLANNED: platform Ethernet | PLANNED: hardware RNG | BLOCKED | BCM2712/RP1 discovery, clocks, resets, IRQ routing, DMA/IOMMU/coherency and firmware ownership must be established |
| ARM64 UEFI laptops | BUILD-ONLY: EFI loader | BLOCKED: model-specific firmware | BUILD-ONLY for direct xHCI HID; PLANNED for I²C HID/touchpad | BUILD-ONLY: GOP; PLANNED: accelerated GPU | BUILD-ONLY: experimental NVMe is disabled | PLANNED: model-specific Ethernet/Wi-Fi | PLANNED: firmware/TRNG source | BLOCKED | ACPI/SPCR/GIC/MCFG/IORT ownership, power management, SMMU and DMA translations vary; secure boot signing is not provided |

## Closing hardware blockers

Use disposable virtual disks throughout. On Apple Silicon, create an isolated Fusion
ARM64 VM, record Fusion/version/firmware settings, boot the new ISO with serial/GOP
recovery, and capture PCI segment:BDF/class/BAR inventory and USB topology. Verify
whether the keyboard/pointer are direct HID, behind a hub, or absolute tablets.
All three paths have QEMU evidence; Fusion evidence remains separate. Test key release, modifiers,
rollover, relative movement, buttons, wheel, dragging, dialogs, disconnect/reconnect,
missing endpoints and controller errors. Do not enable NVMe on a valuable disk.

For Pi 5, supply the board, firmware/DT and the precise boot chain; confirm RP1 host
controller ownership, power/reset and PCIe DMA translation before enabling bus mastering.
For a laptop, supply the exact model plus ACPI/IORT/device inventory and a serial
recovery method. Controllers requiring SMMU translation must remain disabled until
CHOS owns that mapping. Native network drivers must follow the adapter actually exposed.

The user reports c4672fb installation and disk-only Fusion boot. In-system restart,
automatic write enablement, shutdown and VMXNET3 changes after that build require
new Fusion tests. See [installed-system follow-up](FUSION_INSTALLED_FOLLOWUP.md).
The provided PCI screenshot shows bridges (15ad:07a0), not the NIC. New PCI logging
includes all stored functions in compact form; `pci net` filters network classes.

## User-confirmed 107ef51 and Intel controller correction

The user confirms restart, power-off, automatic writable storage, hidden installed
Setup and Console history work in Fusion 107ef51. Networking remains offline. The
new screenshot identifies PCI 8086:10d3 class02:00: Intel 82574L/e1000e, not VMXNET3.
A new exact-ID backend is implemented with bounded reset/TX, legacy descriptor
rings, MAC/link discovery, checked PCI/DMA resources and permanent quarantine on
uncertain DMA. QEMU e1000e traffic passes DHCP, DNS, browser HTTP/relative redirect
and native USB/storage regressions (`e1000-http1`, `e1000-native1`). Host/ASan tests
cover reset, bounds, padding, ring wrap, receive errors, link loss and TX timeout.
The user now confirms Fusion Ethernet and example.com HTTP work after the
54b9731 delivery. This is user-reported target evidence; no image hash, packet
capture, reconnect or TLS validation was supplied. Arbitrary Intel NIC support
is not implied. The revised Stage 3 request is tracked in STAGE3_REVISED_PROMPT.md and
STAGE3_ARCHITECTURE.md, superseding the Word document's Stage 3 scope.

The selected first Wi-Fi device is now the Pi5 onboard CYW43455 family over
SDIO2, independently identified through RaspberryPiOS firmware mappings. This
selection is not driver/runtime support; Pi boot, SDIO2, firmware/authentication
and live wireless tests remain open. See PI5_WIRELESS.md. IPv6 control-plane
QEMU evidence is distinct from the user-confirmed Fusion IPv4/HTTP result.

Current source adds a bounded UEFI CPU/process isolation profile while retaining
identity DMA with data caching off. See [runtime contract and QEMU evidence](UEFI_PROCESS_RUNTIME.md).
This does not establish Fusion qualification for these new changes, native IOMMU
ownership, a modern browser, verified TLS or live wireless support. The binary
publication hold remains active until the complete revised Stage 3 passes.

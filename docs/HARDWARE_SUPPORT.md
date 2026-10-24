# Hardware support matrix

Statuses describe the listed configuration only. `SUPPORTED AND RUNTIME-TESTED`
means exercised in this cloud machine, not certified for production. `BUILD-ONLY`
means source compiled without target runtime evidence. `BLOCKED` requires hardware
or an external environment. `PLANNED` has no complete native path. `UNSUPPORTED`
means an explicit current exclusion.

| Target | Boot path | Firmware | Input | Graphics | Storage | Network | Entropy | Runtime-tested status | Limitations |
|---|---|---|---|---|---|---|---|---|---|
| QEMU ARM64 direct | SUPPORTED AND RUNTIME-TESTED: ELF loader | SUPPORTED AND RUNTIME-TESTED: QEMU FDT | SUPPORTED AND RUNTIME-TESTED: VirtIO keyboard/relative mouse | SUPPORTED AND RUNTIME-TESTED: VirtIO 800×600, windows | SUPPORTED AND RUNTIME-TESTED: VirtIO/CHFS read/write/flush/reboot | SUPPORTED AND RUNTIME-TESTED: DHCP/ARP/ICMP | SUPPORTED AND RUNTIME-TESTED: VirtIO receive and missing-device failure | SUPPORTED AND RUNTIME-TESTED | QEMU virt, Cortex-A72, GICv2, one CPU, 512 MiB; no USB PCI attachment in direct boot |
| QEMU ARM64 UEFI ISO | SUPPORTED AND RUNTIME-TESTED: ISO EFI loader | SUPPORTED AND RUNTIME-TESTED: AAVMF, ACPI, GICv3 | SUPPORTED AND RUNTIME-TESTED: native PCI xHCI + direct USB keyboard/mouse | SUPPORTED AND RUNTIME-TESTED: RAMFB GOP | UNSUPPORTED in this ACPI-only kernel runtime profile | UNSUPPORTED in this profile | UNSUPPORTED in this profile | SUPPORTED AND RUNTIME-TESTED | Firmware supplies no FDT; PCI VirtIO runtime drivers absent; serial/GOP diagnostics and polled desktop only |
| QEMU prepared-image UEFI | SUPPORTED AND RUNTIME-TESTED: EFI files from VirtIO disk | SUPPORTED AND RUNTIME-TESTED: AAVMF | SUPPORTED AND RUNTIME-TESTED: native USB keyboard/mouse | SUPPORTED AND RUNTIME-TESTED: RAMFB GOP | BUILD-ONLY for kernel filesystem attachment; firmware successfully read boot files | UNSUPPORTED in this profile | UNSUPPORTED in this profile | SUPPORTED AND RUNTIME-TESTED for boot/desktop | Prepared-image builder is not an on-device installer; kernel filesystem persistence uses the direct profile |
| VMware Fusion / Apple Silicon | BUILD-ONLY: ARM64 EFI | BLOCKED: actual Fusion VM needed | BUILD-ONLY: native xHCI path; actual topology unknown | BUILD-ONLY: GOP | BLOCKED: actual adapter inventory/I/O | PLANNED: vmxnet3 only if actual adapter requires it | PLANNED: supported entropy source required | BLOCKED | Need PCI BAR/class, USB topology, firmware map, IORT/SMMU data and live keyboard/mouse test; no substitution with QEMU results |
| Raspberry Pi 5 | PLANNED: Pi boot/firmware integration | BLOCKED: Pi 5/firmware needed | PLANNED: RP1/PCIe/USB topology and routing | PLANNED: display controller/firmware | PLANNED: SD/eMMC/PCIe storage as exposed | PLANNED: platform Ethernet | PLANNED: hardware RNG | BLOCKED | BCM2712/RP1 discovery, clocks, resets, IRQ routing, DMA/IOMMU/coherency and firmware ownership must be established |
| ARM64 UEFI laptops | BUILD-ONLY: EFI loader | BLOCKED: model-specific firmware | BUILD-ONLY for direct xHCI HID; PLANNED for I²C HID/touchpad | BUILD-ONLY: GOP; PLANNED: accelerated GPU | BUILD-ONLY: experimental NVMe is disabled | PLANNED: model-specific Ethernet/Wi-Fi | PLANNED: firmware/TRNG source | BLOCKED | ACPI/SPCR/GIC/MCFG/IORT ownership, power management, SMMU and DMA translations vary; secure boot signing is not provided |

## Closing hardware blockers

Use disposable virtual disks throughout. On Apple Silicon, create an isolated Fusion
ARM64 VM, record Fusion/version/firmware settings, boot the new ISO with serial/GOP
recovery, and capture PCI segment:BDF/class/BAR inventory and USB topology. Verify
whether the keyboard/pointer are direct HID, behind a hub, or absolute tablets; the
last two paths currently require additional implementation. Test key release, modifiers,
rollover, relative movement, buttons, wheel, dragging, dialogs, disconnect/reconnect,
missing endpoints and controller errors. Do not enable NVMe on a valuable disk.

For Pi 5, supply the board, firmware/DT and the precise boot chain; confirm RP1 host
controller ownership, power/reset and PCIe DMA translation before enabling bus mastering.
For a laptop, supply the exact model plus ACPI/IORT/device inventory and a serial
recovery method. Controllers requiring SMMU translation must remain disabled until
CHOS owns that mapping. Native network drivers must follow the adapter actually exposed.

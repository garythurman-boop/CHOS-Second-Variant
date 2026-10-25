# Revised Stage 3: internet platform architecture

The user-provided [revised specification](STAGE3_REVISED_PROMPT.md) supersedes the
Stage 3 section of the earlier Word document. Full modern web rendering and real
wireless support remain completion requirements. This document does not certify
an engine port, Wi-Fi or TLS on CHOS.

## Existing system to preserve

Direct boot has an MMU, bounded CHCX v2 EL0 applications and a single-core task
scheduler. UEFI uses a relocated PIE kernel, native PCI USB/NVMe/GOP and a polled
GUI with MMU/cache disabled for identity DMA. UEFI EL0 scheduling is unavailable.
Current CHCX applications have a 64 KiB image limit, 16 KiB maximum stack, bounded
argument passing, no dynamic loader, no complete libc, no POSIX threads/futexes,
no browser-sized user heap or mmap, no general IPC/shared-memory service and no
modern-engine sandbox. These are implementation dependencies, not external-hardware
blockers. A Linux ARM64 browser binary cannot run on this ABI.

Preserve the validated direct/UEFI boot, installed NVMe/CHFS, genuine installer,
power lifecycle, native USB, GOP sizing and existing VirtIO paths. Networking has
DHCPv4, ARP, ICMP, bounded DNS A and four outbound TCP sockets, with single selected
VirtIO or VMXNET3 backend. The HTTP preview browser has URL/redirect/parser tests
and now correct nonblocking UEFI adapters. It remains a recovery/diagnostic app;
it is not the revised modern browser deliverable. No TLS/certificate validation,
IPv6, wireless driver/authentication or Network Manager is implemented initially.

## Immediate implementation order

1. Close Fusion's identified controller gap: PCI 8086:10d3 (82574L/e1000e).
   Keep device-specific ring/resource handling behind the common Ethernet API.
   Validate queues, link, DHCP, DNS and HTTP in an actual AArch64 QEMU e1000e guest;
   distinguish that evidence from the unavailable Fusion host.
2. Introduce an OS-owned network configuration/status service. Centralize link
   transitions, static/DHCP validation, route/resolver state and reconnection.
   The privileged shell configures it; browsers only consume sockets/status.
   Add persistent checked profiles and a functional graphical Network Manager.
3. Evaluate maintained browser engines and select a port architecture from actual
   upstream requirements. Implement the necessary isolated user runtime and I/O
   services; do not execute a modern engine or untrusted JavaScript in the UEFI
   kernel GUI loop. W^X, content isolation, bounded memory and preemption need real
   tests before hostile content is admitted. Do not replace the modern-engine goal
   with a text parser, host renderer or remote rendering service.
4. Integrate maintained, pinned TLS with verified entropy/time/trust roots, failure
   fixtures and update policy. VirtIO RNG has live QEMU evidence but Fusion entropy
   remains unavailable. Firmware-clock snapshots are not an authenticated network
   time service. HTTPS remains blocked until all verification prerequisites work.
5. Add explicitly selected wireless hardware through real bus/firmware/802.11 and
   WPA services. No device scan/profile UI may invent hardware or networks. Pi 5
   and laptop controller selection needs actual inventory/firmware evidence.
6. Expand IPv6, multiple interfaces/routes, browser APIs/conformance, downloads,
   persistent web storage and resilience; benchmark real guests and keep all
   unfinished requirements visible.

## Constraints and test matrix

The current target remains one CPU, 2 GiB RAM, identity DMA without native IOMMU.
Modern engines need larger address spaces, multiple isolated processes/threads,
substantial heap/stack growth, filesystem and IPC primitives and software-rendering
or GPU backend integration. Raising an image-size constant alone does not supply
those capabilities. Resource bounds and allocation-failure handling must accompany
runtime expansion. No modern-engine performance numbers exist yet.

| Platform | Available evidence/environment | Required independent gates |
|---|---|---|
| QEMU AArch64 direct | Live MMU/EL0, VirtIO Ethernet/RNG, CHFS, GUI | Retain regressions; add service/TLS/engine/IPv6 fixtures |
| QEMU AArch64 UEFI | Live native USB/NVMe/GOP, VirtIO/VMXNET3 | e1000e traffic, reconnect, manager, installer/power regression; isolated runtime |
| Fusion on M1 | User confirms 107ef51 fixes except network; NIC 8086:10d3 | New e1000e driver traffic/reconnect; secure entropy; modern runtime/browser |
| Raspberry Pi 5 | Board unavailable | Real boot/IOMMU/DMA, Ethernet and selected Wi-Fi chipset/firmware/authentication |
| ARM64 laptop | Model/inventory unavailable | Explicit controller/firmware, Wi-Fi, ACPI/DMA, real browser/isolation |

Every completion requirement remains open until backed by working implementation
and appropriate runtime evidence. Host tests and QEMU Ethernet do not certify
Fusion, physical wireless or browser standards compliance.

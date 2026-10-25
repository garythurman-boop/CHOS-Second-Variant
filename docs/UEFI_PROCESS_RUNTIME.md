# Isolated UEFI application runtime

The UEFI kernel now has a CPU translation and scheduled EL0 application profile.
Actual QEMU AAVMF tests run CHOS-native ELF RX/R/RW segments, BSS, sparse holes,
arguments, deliberate RO/XN/translation faults and denied reads of real kernel
text. Repeated normal and faulted exits return to equal free physical-page counts.
This is a runtime prerequisite for Stage 3, not a completed browser sandbox or
modern browser. Fusion must independently validate these source changes; earlier
user reports establish the preceding Fusion build's Ethernet and desktop only.

## Mapping and DMA policy

The new builder uses four levels, 4 KiB pages and 2 MiB blocks over a bounded
40-bit physical identity profile. Four-level translation is required because
QEMU assigns native USB BARs at 0x800000c000, above the old 39-bit VA ceiling.
It has fixed pools of 64 L2 and 192 L3 tables; exhaustion rejects the candidate.
A failed candidate is never activated. This builder is not transactional: a
failed map can leave partial entries in the discarded candidate.

Validated EFI RAM descriptors retain privileged Normal RW/XN mappings. Exact
relocated kernel text is RO/executable, rodata is RO/XN, and boot/task-stack guard
pages stay unmapped. Disjoint bounded descriptors, retained boot/map/payload
buffers, the complete framebuffer range and the empty private user arena are
checked. Devices use validated GIC/SPCR/MCFG and supported assigned BAR ranges;
broad PCI bridge apertures are not indiscriminately mapped across user space.
RAMFB retains its RAM attributes. Unknown/unsupported layout falls back to the
existing MMU-off recovery desktop, with an explicit diagnostic.

**Data caching stays disabled.** Native xHCI, NVMe, Intel, VMXNET3 and PCI VirtIO
accept MMU-on DMA only in the explicit CHOS-owned identity profile with data
cache off. Unknown translations and cache-on operation remain refused, as do
unverified IORT/SMMU topologies. This does not implement an IOMMU, device isolation,
SMP, arbitrary DMA mappings or a cache-coherent DMA API. Instruction caching and
CPU W^X enforcement are enabled. CPU 4 KiB/40-bit translation support is checked.

Process roots inherit all privileged identity RAM/device mappings, including RAM
above the direct-boot alias candidate window. UEFI roots privately clone the low
L1 and two low L2 tables for private EL0 leaves while sharing remaining privileged
tables. Direct boot retains its three-level hierarchy. Template copies and root
switches refuse live dynamic aliases; UEFI dynamic-alias APIs remain unavailable.
Owner references, private frame allocation, user-copy checks and deferred cleanup
retain their existing rules. The extra private UEFI L1 page is reaped too.

The scheduled kernel desktop polls native input, network services and GUI, and
services actual process spawn/reap requests. Its serial shell is available when a
UART is described. Timer delivery must be demonstrated before starting this
runtime; absent/broken timer paths keep recovery behavior. The existing QEMU-fixed
VMM self-test is not used as UEFI acceptance evidence.

## Reproducible local tests

```sh
source /workspace/chos-tools/activate
make CC=clang LD=ld.lld all uefi-iso build/userland/native_test.elf
make test-uefi-vmm test-uefi-vmm-sanitize
python3 tests/qemu_native_elf.py --mode uefi --out /workspace/chos-evidence/NEW-UEFI-ELF
python3 tests/make_uefi_high_iso.py --out /workspace/chos-evidence/NEW-HIGH-FIXTURE
python3 tests/qemu_native_elf.py --mode uefi \
  --iso /workspace/chos-evidence/NEW-HIGH-FIXTURE/relocation-test.iso \
  --out /workspace/chos-evidence/NEW-HIGH-ELF
```

The relocation fixture copies the real EFI loader source and changes only its
minimum allocation address to 0x80000000. Production loader policy is unchanged;
source/binary hashes and exact commands are retained. This local test ISO is not
a distribution. The observed kernel placement was 0xbab31000 with native Intel,
high-BAR xHCI and isolated user execution. Host/sanitizer tests cover high-address
blocks/pages, boundaries, W^X refinement, guards, conflicting overlap, overflow,
idempotent remaps, table exhaustion and the fail-closed identity-DMA policy.

Early input/network fixtures assumed the old kernel singleton applications.
Once actual EL0 applications became reachable, their data and windows required
owner-aware test probes. Such failed runs remain failed; corrected fixtures must
still verify real input, window ownership, wire traffic and browser response.
Native text-editing, held keys, DHCP/HTTP, installation/power and final source
qualification are tracked separately. No source iteration closes the full Stage 3
or release gate merely because these runtime components pass.

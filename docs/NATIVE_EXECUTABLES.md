# Bounded native AArch64 executables

The direct-boot EL0 runtime now accepts native ELF64 static executables in addition
to existing CHCX v1/v2 images. This advances the runtime needed for ported libraries;
it is not a WebKit port, Linux ABI compatibility, complete C/C++ runtime, dynamic
linker or full browser isolation. The additional UEFI CPU/process profile is documented
in UEFI_PROCESS_RUNTIME.md and has QEMU evidence; Fusion validation remains open. The revised Stage 3 remains unfinished.

## Supported profile

* AArch64, ELF64 little-endian, static ET_EXEC at the existing 0x20000000 user arena.
* An explicit CHOS ABI v1 PT_NOTE: type 0x43484f53, name `CHOS` including NUL,
  four-byte little-endian version 1. This identifies ABI intent, not authenticity.
  A plain Linux/System V executable without the note is rejected.
* At most eight program headers, four MiB virtual span and four MiB plus 4 KiB file.
  LOAD offsets/virtual addresses are page-aligned with checked alignment/congruence.
  File/memory bounds, arithmetic overflow, header sizes, version, machine and entry
  alignment are checked before allocating process pages.
* Page-separated RX, read-only/XN and RW/XN segments. Write-plus-execute is refused.
  Entry must name four initialized bytes in an executable segment. Overlapping
  file or virtual-page ranges are rejected. Unmapped holes stay unmapped; BSS and
  page tails use the owner's zero-filled PMM pages.
* Non-executable stack with the existing 32 KiB maximum; argc/argv use the existing
  CHOS startup convention. Interpreter, dynamic, TLS, executable-stack and
  unsupported RELRO requests are rejected. Section headers are not a loader API.

The owned-frame quota is eight MiB; the four-process limit remains. Native image span is
larger than CHCX's preserved 64 KiB bound, but it is still insufficient for WebKit.
Larger heaps/images, threads/futexes, IPC, events, complete libc/C++ and native IOMMU/device
isolation remain separate requirements. See NATIVE_C_RUNTIME.md for allocation
and string primitives already implemented. No constant increase is represented
as a working browser runtime.

The process reaper uses a bounded image-page mapping ledger rather than a 32-bit mask.
It unmaps only pages actually loaded, then destroys the owner and zeroes/frees its
frames. This also handles images spanning holes and high page indexes. Mapping
counts are checked against actual loaded pages rather than the sparse address
span. Failed loads and normal/faulted/scheduled exit retain the existing deferred
cleanup rules and canonical-kernel-root requirement. Disk-backed executable reads
are size-bounded before allocating the file buffer; CHFS CRC validation remains.

## Build and evidence

`userland/native.ld`, `native_abi.S` and `examples/native_test.c` supply a reproducible
CHOS-native example with sparse RX/R/RW segments and a 256 KiB BSS. Build locally:

```sh
source /workspace/chos-tools/activate
make CC=clang LD=ld.lld all build/userland/native_test.elf build/qemu-smoke-disk.img
make test-elf-image test-elf-image-sanitize
python3 tests/qemu_native_elf.py --out /workspace/chos-evidence/NEW-NATIVE-TEST
```

The runner injects the app only into a disposable disk. Live execution checks
arguments, initialized data, BSS zeroing/writes and return through the actual EL0
entry/SVC path. Deliberate RO writes, execution of RW data and writes into a sparse
gap fault only the user process. Repeated normal/faulted/scheduled launches and
slot reuse must quiesce and return to equal free physical-page counts. Host/
sanitizer tests cover malformed headers/notes, missing/wrong ABI, overflow, overlap,
wrong machine/type/endianness, permissions, entry bounds and truncated input.

An early live load failed because its final mapping-count check still assumed
every page in an image span was mapped. The check now counts actual mapped pages;
the retained failing run is not accepted evidence. Later note-bearing live tests
pass with equal free-page counts and contain all three deliberate faults.

All builds here are local test inputs. Publication remains held until every revised
Stage 3 acceptance gate is satisfied and thoroughly tested. No new ISO/build tag
is published for this prerequisite.

The subsequent [larger native profile and generated ICU resource execution](NATIVE_LARGE_RESOURCES.md)
qualifies1MiB allocations,4MiB images and8MiB owned frames in both QEMU boot
modes. C++23 byteswap is now supplied and tested. The historical2MiB/4MiB and
missing-byteswap limits above describe earlier snapshots. The full32MiB embedded
data package remains over budget; JavaScriptCore still requires real virtual
memory APIs and further platform work. This is not Stage3 completion.

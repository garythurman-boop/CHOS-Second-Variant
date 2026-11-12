# Engine contiguous allocations

The native engine profile can now back ordinary malloc/calloc/realloc allocations
with contiguous virtual regions up to 32 MiB. The allocator's 32-byte header means
the largest ordinary aligned payload is **32 MiB minus 32 bytes**. Additional
alignment overhead can reduce that maximum. Legacy applications retain the
existing 1 MiB backing-region limit.

This closes an allocator prerequisite for original WebCore's contiguous image
vectors and encoded shared buffers. It does not implement those engine adapters
or complete WebCore/browser integration. Stage 3 remains incomplete at 0.30.0.

## Implementation

The heap core has an explicit per-heap mapping limit. Its default remains 1 MiB;
only an empty heap can change the limit. The native wrapper enables the larger
limit only for its engine profile. Kernel resource admission still validates the
executable profile independently of that user-runtime hint.

Backing regions at or below 1 MiB use the existing eager mapping path. Larger
regions reserve contiguous virtual space through the existing engine reservation
service, then commit physical pages in operations of at most 1 MiB. Only a fully
committed region reaches heap metadata initialization and client publication.
Release also proceeds in bounded prefix operations. The kernel mapping/syscall
contracts, 96 MiB engine mapping aperture, physical frame quotas and non-executable
page policy remain unchanged.

Failed commits unwind the reservation. If rollback becomes uncertain, the wrapper
retains the remaining range in a separate quarantine ledger; it is never reused
or retried. The original heap quarantine covers partial release failures of a
previously published region. Those regions may have unmapped metadata prefixes,
so subsequent operations skip their metadata entirely. Address-space reaping
retains authoritative ownership and performs terminal cleanup. Heap diagnostics
report the quarantine and conservatively retained virtual spans, not a claim of
exact physical residency after an uncertain operation.

The new limit field changes the static native heap-core structure layout. Rebuild
native clients against a freshly exported matching SDK. Previously distributed
static executables retain their own runtime; no dynamic-library ABI substitution
is performed.

## Qualification

[The validation report](validation/stage3-native-large-heap.json) records source,
SDK and executable hashes and actual guest results.

Host tests compile the actual allocator core and native wrapper with syscall
fault models. They check the 1 MiB transition, 8/16 MiB payloads, the 32 MiB
backing boundary, alignment, complete byte patterns, calloc zeroing, overflow
refusal, realloc preservation, reservation failure and every commit boundary of
an 8 MiB request. Failed rollback and partial release must retain quarantine
records and never retry unknown mappings. The original 12,000-operation mixed
allocator test and external metadata-capacity checks still pass. Address,
undefined-behavior and leak sanitizers pass for both the core and wrapper tests.

Direct and UEFI QEMU each pass three full cycles using a fresh SDK and disposable
CHFS application images. Every cycle runs the engine fixture, a legacy fixture,
and two deliberately faulted engine processes that write the real low/high guard
pages. The engine checks all bytes of the selected large allocations and uses
three actual pthread workers. Normal exits have zero live heap allocations;
every normal or faulted lifecycle restores the exact cold physical-page baseline,
engine ownership, committed-frame accounting and futex state.

The memory-failure cases are host fault injection. They do not represent a claim
that native hardware page-table/free failures were physically induced. QEMU tests
establish the listed allocation, guard and cleanup behavior only; new Fusion,
physical Pi 5 and laptop execution remain open.

## Reproduce

The focused host targets are part of normal `make test` and `make test-sanitize`:

```sh
make test-user-allocator CC=clang LD=ld.lld
make test-user-allocator-sanitize CC=clang LD=ld.lld
```

Export a fresh native SDK, then build `tests/native_large_heap` through
`ports/native/chos-toolchain.cmake` separately with
`CHOS_APPLICATION_PROFILE=engine` and `legacy`. Run the genuine guest runner with
both executable paths and frozen `chos.elf`, `uefi-kernel.elf` and `chos.iso`:

```sh
python3 tests/qemu_native_large_heap.py --mode direct --application /path/to/engine/native_large_heap --legacy /path/to/legacy/native_large_heap --image-root /path/to/frozen-images --out /path/to/fresh-direct-result
python3 tests/qemu_native_large_heap.py --mode uefi --application /path/to/engine/native_large_heap --legacy /path/to/legacy/native_large_heap --image-root /path/to/frozen-images --out /path/to/fresh-uefi-result
```

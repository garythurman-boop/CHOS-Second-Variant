# Native engine reservations and first VM execution

The original pinned JavaScriptCore VM now creates a context, executes JavaScript
iteration and a JSON round trip, formats a date through full original ICU data,
collects garbage and releases the context in direct and UEFI QEMU guests. These
are initial execution tests, not ECMAScript, Wasm, GC or browser conformance.
Stage 3 remains incomplete and the development version remains 0.30.0.

Two concrete dependencies caused the earlier failures. The original Structure
allocator's last fallback requests a 32 MiB aligned reservation. The generic
POSIX path requested another 32 MiB for alignment, while native eager mappings
are bounded to 1 MiB. After implementing real reservations, VM creation worked,
but evaluation reported stack overflow: the 128 KiB main stack was smaller than
upstream's 128 KiB soft reserve after ordinary call frames. A hardware breakpoint
at `JSC::throwStackOverflowError` captured SP `0x2ffff790`: only 128,912 bytes
remained, below the 131,072-byte reserve. The exception-string conversion had the
same problem. Collection and context release nevertheless completed.

The engine main stack is now **1 MiB** with an independently tracked 256-page
mapping ledger. Engine default and maximum secondary-worker stacks remain
128 KiB. Legacy main/default-worker stacks remain 32 KiB; legacy maximum workers
remain 128 KiB. Snapshot validation accepts the larger independently registered
main stack; this grants neither mappings nor larger secondary-thread stacks.
An initial shared snapshot check still enforcing the worker limit was reproduced
with a failing host test and fixed. Failure logs and debugger evidence are retained.

Repeated VM execution also exposed delayed terminal page scrubbing: a four-second
diagnostic deadline expired after JavaScript, Intl and collection had completed.
A retained diagnostic run allowed all three unchanged lifecycles to finish with
exact cold memory baselines; page teardown alone took 216, 445 and 474 timer ticks
at 100 Hz. The captured PC was the byte-store page-clear loop, not a deadlock.
Owned normal pages now use aligned, alias-safe, volatile 64-bit integer stores
without SIMD or cache-zero instructions. Every freed page is still fully scrubbed.
Boundary/canary and sanitizer tests validate the exact 4096-byte extent and
misaligned refusals. Repeated guest tests retain the four-second diagnostic limit
and additionally require actual HID desktop response during VM exit within two
seconds; measured results belong to the recorded QEMU configuration, not a general
latency guarantee.

## Kernel memory contract

Only an executable admitted by a validated engine ABI note can reserve virtual
memory. `chos_reserve_memory(bytes, alignment)` uses map-memory syscall 35 with
flag `CHOS_MEMORY_MAP_RESERVE` and a power-of-two alignment from 4 KiB to 32 MiB.
One reservation is at most 32 MiB, inside the existing guarded 96 MiB private
anonymous window and 512-record metadata limit. Reservation observes descriptors;
it allocates and zeroes no physical data pages. It adds no resident/mapped charge.

The existing memory-control syscall 64 commits zero-filled pages when a reserved
range becomes readable or writable. `CHOS_MEMORY_DECOMMIT` actually unmaps and
frees physical pages while retaining the reservation. Recommit returns zeroes.
Protect-none and advice on holes allocate nothing; discard zeroes existing pages
without changing permissions. Range release preserves the reservation type when
splitting. Engine frame quotas, the exclusive engine lease, system memory floor,
read-only data and execute-never anonymous memory still apply.

Every physical commit, decommit, protection, discard or release transaction is
bounded to 1 MiB. Userland iterates larger requests between syscalls. A failed
commit rolls back only its newly allocated pages, preserving existing contents
and permissions; unexpected descriptor, rollback or free failures quarantine the
region until terminal cleanup. There are no operation pins left across userland
chunks. Process termination can reap an abandoned reservation or a live worker's
remaining mappings. The metadata and physical operations are serialized with
interrupts masked on the current single CPU. No SMP or maximum IRQ-latency claim
is made.

## Actual upstream adapter

The pinned `OSAllocatorPOSIX.cpp` selects native reservations only for an engine
executable. Its actual public reserve, commit, protect, decommit, release,
zero-fill and advice methods use the bounded services. Guarded alignment can
overreserve virtual space and trim unused ends before publishing the aligned
data address. Both requested guard ranges remain uncommitted. Physical pages
are never allocated merely for alignment slack. Unsupported executable/JIT-cage
allocations and address hints refuse explicitly.

The original Structure allocator, preferred size, fallback loop and GC/pointer
assertions remain unchanged. JIT, DFG, FTL and sampling remain disabled; checked
Wasm remains configured on, which alone proves no Wasm execution. The full VM
links all 141 JSC archive members, 53 original interpreter/bytecode subtarget
objects, 173 WTF members and 168 bmalloc members, with original full ICU data
and no unresolved symbols. The strict source manifest checks all 40 patched
postimages, seven native port files and the combined patch. Replaying the patch
against all original files must reproduce every postimage hash.

## Reproduce and qualification boundary

Build and verify the SDK before building applications. Use the recorded native
toolchain, pinned source and engine configuration; do not rebuild them while
guest matrices run. `tools/build_engine_reserve.py` builds engine and legacy
refusal variants. `tools/build_wtf_allocator.py` builds the actual public WTF
allocator fixture. `tools/probe_jsc_vm_link.py --application-profile engine`
links the full original-ICU C API fixture. Stage it with
`tools/prepare_native_engine.py`, then build `make engine-test-images`.
The separate diagnostic ISO has no installer payload.

Run `tests/qemu_engine_reserve.py` with both application variants,
`tests/qemu_wtf_allocator.py` with its actual WTF executable, and
`tests/qemu_jsc_vm.py --runs 3`, each in direct and UEFI mode with disposable
media. Positive tests require normal process exit and exact cold physical-page,
commit, pool and futex baselines. Deliberate guard, read-only, decommitted and
execute-never faults must stay inside their process. Legacy memory limits and
engine admission under pressure require separate regressions.

The final qualification comprises **41 passing guest profiles** plus the complete
host and ASan/UBSan suites. The VM passes three lifecycles in each boot mode;
teardown takes 43–58 ticks at 100 Hz and the observed HID response during exit
is 0.002–0.032 seconds. Actual WTF allocator tests separately pass three normal
lifecycles and five contained faults in each mode. Every lifecycle returns to
its exact cold physical-page, commitment, engine-lease and futex baseline.
Legacy memory/threading, native C++/ICU/WTF/JSC components, TLS over IPv4/IPv6,
typing, e1000e packet exchange and USB/storage regressions also pass.

The original installation observation exceeded its fixed 90-second deadline
while still writing the ESP at sector 120,704 of 131,072 with no NVMe error.
The retained retry uses a 180-second total watchdog and a separate 15-second
job/sector stall watchdog. It completes installation in 102.811 seconds,
then passes disk-only boot, persistent writes, actual GOP mode change, automatic
writable storage, hidden installed Setup, real firmware restart and a 5.235-second
shutdown window followed by guest power-off. Injected NVMe write failure separately
passes failure reporting, no false completion, unchanged target and responsive GUI.
The timing difference from the earlier 76.742-second checkpoint is retained;
its exact host/device timing cause is not established. Native installer I/O was
unchanged. The observer now records actual progress and still fails on a stall.

An environment restart interrupted one firmware boot. That attempt is retained
as interrupted, receives no qualification credit, and passes on an unchanged-input
retry. All guest matrices use frozen input hashes with no kernel/SDK/engine builds
during execution. Closed guest media and obsolete frozen input copies are reclaimed
only after complete member-verified archives and matching removal ledgers exist.

See [the qualification manifest](validation/stage3-engine-reservations.json)
for exact build hashes, executed profiles, retained failures and archive hashes.
These initial VM and allocator results do not provide WebCore rendering, a modern
browser, native Pi 5 wireless, production browser security or independent new
Fusion/Pi 5/laptop qualification. Those remain required by the revised Stage 3.

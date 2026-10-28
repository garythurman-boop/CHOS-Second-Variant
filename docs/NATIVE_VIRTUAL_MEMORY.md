# Native anonymous memory operations and upstream VM integration

The bounded native runtime now implements private anonymous mmap/munmap,
mprotect and madvise, plus sysconf/getpagesize/getpid and process-owned
read/lseek/close. write sends bounded partial output only to the native Console
stdout/stderr descriptor convention. This is a concrete subset, not complete
POSIX, an engine-sized runtime or Stage3 completion.

## Ownership, permissions and failure behavior

MAP35 and exact-region UNMAP36 keep their ABI. MEMORY_CONTROL64 shares MAP35's
existing capability class; UNMAP_RANGE65 shares UNMAP36's. No sign-bit permission
or mask widening is introduced. The dispatcher rejects denied operations before
range processing. Control operations apply only inside one live anonymous region
owned by the current address-space generation. Code, main stacks, device memory,
foreign owners, holes, pinned worker stacks and invalid/overflowing ranges fail.

Protection supports NONE, read-only/XN and read-write/XN. Executable anonymous
memory is refused. NONE retains owned backing behind non-global AP=00, PXN/UXN
PTEs; neither EL0 loads nor syscall user-buffer copies may access it. Permission
changes validate the entire bounded range first, then perform break-before-make,
cache maintenance and architected TLB barriers with local IRQs masked. Frame and
mapping ownership counts remain unchanged. This preserves the single-CPU model
and current ASID0 root-switch rules; it does not change DMA/cache/IRQ ownership.

Discard validates owned normal pages, zeroes them through their kernel physical
aliases and preserves protection. Backing frames remain reserved to the owner;
this eager profile does not implement demand paging, swapping or release physical
commit on advisory discard. NORMAL/RANDOM/SEQUENTIAL/WILLNEED are validated hints
with no extra paging behavior. Unknown advice fails rather than inventing success.
Each control operation covers at most256 pages, under the existing1MiB mapping
cap and2MiB aperture. The image/frame budgets remain4MiB/8MiB.

Partial unmap supports prefix/suffix trimming and a middle split. A split reserves
one of16 region records before touching pages; metadata exhaustion changes no
mapping or data. Pinned regions fail. Unexpected unmap/free failures quarantine
the original region until reaping. Guard gaps remain inaccessible. Requests that
span holes or multiple regions fail; the implementation does not advertise full
POSIX range semantics. Fixed/file/shared/executable mappings remain unsupported.
MAP_NORESERVE is accepted as an advisory flag without changing eager allocation.

## Real engine integration

The checked WebKit platform patch now identifies CHOS in CMake and bmalloc and
uses the native discard API for VM zero-and-purge, avoiding unsupported MAP_FIXED
replacement. Original allocator algorithms are retained. The native SDK defines
__CHOS__ explicitly without inheriting Linux/UNIX conformance claims. Original
source/post-image/patch hashes are recorded in the dependency manifest; the older
identity-only patch remains available for historical evidence.

A genuine C++23 executable includes the pinned upstream VMAllocate helpers and
executes aligned allocation with prefix/suffix trimming, purge, access revocation,
restoration and native worker lifecycles. Original physical-page advice helpers
run against the documented eager profile; their names do not imply released PMM
frames. Full bmalloc, WTF, JavaScriptCore, WebCore and browser rendering are not
established by this component fixture. The next actual JavaScriptCore build gets
past byteswap and memory headers, then fails on fcntl.h and stderr stream APIs.
These are real remaining dependencies, not a successful engine build.

## Qualification

Qualification also found that freed executable/resource buffers left entire
kernel heap arenas reserved. The allocator now returns fully empty arenas,
retaining at most one completely free minimum-size arena. A PMM ownership refusal
quarantines its arena; uncertain storage is excluded from allocations and free
capacity diagnostics. Host tests cover that fault path and concurrent allocation.
UEFI ICU diagnostics show the same4 backing pages and240 live payload bytes
before/after the workload, instead of retaining a596-page empty file arena.
A retained failed profile exposed an additional36-page high-water allocation.
The fix keeps the strict PMM equality test; it does not weaken it into an allowed
leak or an arbitrary extra warm-up. A separate retained test-timing failure parsed
a partial UART fault record; the runner now waits for the full ESR/FAR record.

The native mapping fixture tests maximum1MiB operations, protection preservation,
zeroed bytes, partial split/trim, quota failure, actual foreign-process attempts,
reduced permission masks, code-promotion refusal and three concurrent workers.
Intentional read-NONE, write-RO, execute-XN and unmapped-hole accesses fault only
the application at the exact announced address. Later applications execute and
warm free-page counts remain stable. Separate thread tests verify that both new
syscalls refuse live pinned worker stacks. Existing exact UNMAP behavior remains.

Host and sanitizer checks cover descriptors, registry ownership, malformed
operations, rollback/quarantine and split capacity. Native SDK executable probes
link the actual APIs; negative missing-import/library and full-data-budget checks
remain failures as required. The full host/sanitizer suites and integration
matrix are recorded in [the evidence](validation/stage3-virtual-memory.json).
Timing records are observations from shared-cloud QEMU, not browser benchmarks
or physical-hardware latency guarantees.

Reproduce with make test/test-sanitize and resynchronized all/uefi-iso media.
Run tests/qemu_virtual_memory.py --mode direct|uefi --out FRESH_OUTPUT; use
--application for the SDK-built sdk_vm executable. Build the upstream fixture
with tools/build_bmalloc_vm.py --source PINNED_CHECKOUT --native-sdk build/native-sdk
--out FRESH_BUILD and run tests/qemu_bmalloc_vm.py --application BUILT_EXECUTABLE
--mode direct|uefi --out FRESH_OUTPUT. Exact commands/hashes live in the reports.

New Fusion/Pi/laptop validation remains NOT RUN. Modern JavaScript/rendering,
engine-sized memory, remaining OS APIs, production browser security and real
Pi5 SDIO host/firmware/authentication still block Stage3/version0.50.0. This is a
source-only milestone; the cc29ea8 test ISO remains unchanged.

# Larger bounded native images and genuine ICU resources

The native profile now allows a4MiB static ELF image,8MiB owned physical frames
per address space and a1MiB individual anonymous mapping. The separate2MiB
anonymous aperture,16 regions, unmapped guards, four processes,32KiB main stacks
and three secondary-thread records remain. Kernel and allocator share one mapping
limit header. CHCX retains its64KiB image limit. No syscall numbers, DMA/cache
policy, interrupt contract, executable heap permission or device authority changes.
This is a measured prerequisite, not an engine-sized or complete POSIX runtime.

## Why the expansion is necessary

Actual upstream ICU resources include143KiB locale and193KiB currency pools and
235KiB supplemental metadata. The old128KiB allocation cap cannot load these.
The generated-resource application links original common+i18n code and explicitly
opens the large files, then runs en/de/fr decimal formatting/parsing, strict
Windows1252 and ShiftJIS conversion, word breaks and extended grapheme boundaries.
Three native workers repeat the APIs through three init/cleanup cycles. Missing
data, an unknown converter, unmappable text and malformed numeric text fail.
Every completed cycle releases the ICU runtime's heap allocations.

The fixture extracts38 selected, individually verified original resources from
the real4,321-resource ICU79.1 package onto a disposable CHFS disk. It includes
the base converter needed by the extension-only ShiftJIS file. This is deployment
of real generated data, not fabricated resources or the empty bootstrap payload.
It does not execute the whole32MiB embedded archive or establish full Intl/CLDR/
ICU conformance. The full-data SDK link still must fail with the specific4MiB
budget assertion, rather than an undefined symbol or a silently reduced package.

A separate fixture actually maps a3MiB read-only payload and a1MiB private heap,
exceeding both the old2MiB image span and old4MiB total frame budget. It verifies
all payload/zeroed bytes, initialized writable data and BSS. Writes and indirect
execution above the old boundary fault at the exact expected address and the
correct architectural class. Repeated normal, abandoned and faulted exits return
to stable warm free-page counts; subsequent applications still execute.
Host allocation/map failure injection covers pages1,32,33 and256 with complete
rollback, alongside owner identity, quarantine, guard and pin rules.

## Original C++ bit support and engine build

The exact LLVM19.1.7 byteswap header is added to the771-file verified dependency
closure. The importer verifies the pinned official archive and existing files
before extending public/private transitive includes. Original bytes remain intact.
Native C++20/23 fixtures model-check bit counts/rotations/width/floor/ceil and
C++23 signed/unsigned byteswap through128-bit values, including worker lifecycle
cleanup. The SDK performs a genuine C++23 executable link before guest execution.

Actual pinned JavaScriptCore CMake configuration still succeeds against the
native SDK. The next real build no longer reports missing byteswap; it stops at
sys/mman.h. No fake memory APIs, host libc substitution, source-level algorithm
replacement or executable-only compile probe is accepted as engine execution.

## Persistent UEFI serial recovery

Testing the mapping lifecycle in UEFI exposed a serial-console issue: runtime-exit
left the persistent desktop task's serial loop although there was no boot console
to return to. The desktop polling loop continued, but UART recovery was lost.
That command now explains the persistent session and keeps serial recovery alive.
The direct profile still returns from its nested runtime to its real boot console.
Both behaviors have live checks after deliberately faulted native applications.

## Reproducible checks and limits

Use make test and make test-sanitize, then resynchronize all/uefi-iso/install-disk-image.
Run tests/qemu_user_memory.py, qemu_large_image.py, qemu_bit.py,
qemu_icu_resources.py and qemu_user_allocator.py with --mode direct and --mode uefi
and a fresh --out directory. The UEFI bit/resource runs use the real SDK-built
C++23 applications via --application. Native ELF/thread, typing and e1000e
platform regressions qualify integration separately.

Exact hashes, outcomes and retained failures are in
[the milestone evidence](validation/stage3-large-resources.json). QEMU results
are not new Fusion, Pi5 or laptop certification. This source milestone publishes
no new ISO; cc29ea8 remains the selected test build and version stays0.30.0.
Modern WebKit rendering/JavaScript, engine-sized memory and missing OS interfaces,
production TLS policy/sandbox integration, real Pi5 SDIO host/firmware/authentication
and independent target tests remain required for Stage3/version0.50.0.

The subsequent [native virtual-memory component](NATIVE_VIRTUAL_MEMORY.md) now
supplies owned anonymous protection/discard/partial unmap and bounded native
unistd operations. Original WebKit VM helpers execute against the SDK with a
checked CHOS discard adaptation. Actual JavaScriptCore compilation moves past
the missing memory headers to fcntl.h and stderr stream APIs; no complete engine,
modern browser or Stage3 completion is claimed.

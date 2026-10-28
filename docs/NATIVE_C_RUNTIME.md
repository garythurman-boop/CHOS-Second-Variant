# Native C allocation and string runtime

The CHOS-native ELF runtime now has a small freestanding allocation/string
library. Its standard C symbols are compiled for AArch64 and linked into an
actual isolated EL0 test executable. This is a concrete porting prerequisite,
not a complete libc, libstdc++, POSIX implementation or browser engine.

`userland/libc` implements `malloc`, `free`, `calloc`, `realloc`, `aligned_alloc`
and `posix_memalign`. A process-private allocator requests anonymous RW/XN pages
through CHOS MAP/UNMAP syscalls; it never allocates kernel or physical addresses.
Each allocation has at least 16-byte alignment, checked against target
`max_align_t`. Alignment/size overflow, multiplication overflow, mapping failure
and the 16-region limit return errors without discarding a live realloc input.
Zero-size malloc returns a freeable minimum allocation; realloc of a non-NULL
pointer to size zero frees it and returns NULL. `posix_memalign` leaves its output
and errno unchanged on failure. Power-of-two alignments that cannot fit a backing
region fail with ENOMEM.

The current kernel caps one mapping at 1 MiB and all mappings at 16 regions
within a guarded two-MiB virtual arena. The allocator usually maps 64 KiB and
splits/coalesces aligned in-mapping blocks. It checks all block ranges before
following metadata, recognizes only exact live payload pointers, and releases
empty regions. A corrupt region or failed unmap is quarantined rather than reused
or blindly unmapped again. The process reaper remains responsible for abandoned
and quarantined frames. Metadata stamps catch accidental corruption; they are
not a cryptographic protection against the process that owns the memory.

The library also provides memcpy/memmove/memset/memcmp/memchr, string length and
bounded length, comparison, copy, concatenation, character search and substring
search. Overlap-safe memmove is tested in both directions. Other functions keep
their normal C preconditions, including destination capacity and memcpy's
non-overlap requirement. These are ASCII/byte operations, not Unicode text shaping.
The allocation/string implementation is CHOS source. The subsequent compiler TLS
runtime has its own pinned LLVM dependency described in COMPILER_TLS_RUNTIME.md.

The [native pthread runtime](NATIVE_PTHREAD_RUNTIME.md) now protects heap
transactions and supplies per-thread errno, join and synchronization. Compiler
TLS has the separate explicit build profile in COMPILER_TLS_RUNTIME.md. Complete
stdio, locale, signal, dynamic-loader, filesystem-descriptor and C++ runtime
interfaces remain open. C-linkage headers
are checked from freestanding C++17, and an actual C++ object calling these C
symbols is linked and executed by the guest test. That does not imply exceptions,
RTTI, operator new/delete, C++ standard-library containers or WebKit support.

## Reproduction and evidence

```sh
source /workspace/chos-tools/activate
make CC=clang LD=ld.lld test-user-allocator test-user-allocator-sanitize \
  build/userland/runtime_test.elf build/qemu-smoke-disk.img all
python3 tests/qemu_user_allocator.py --out /workspace/chos-evidence/NEW-LIBC-RUN
python3 tests/qemu_user_allocator.py --mode uefi --out /workspace/chos-evidence/NEW-UEFI-LIBC-RUN
```

Host tests exercise the same allocator core with controlled map/unmap failures,
16-region exhaustion, exact and high alignment, splitting/coalescing, realloc
preservation, overflow, pointer rejection and quarantine. A deterministic 12,000
mixed-operation test checks all live allocation contents, non-overlap and
accounting. The string tests compare byte operations and thousands of overlapping
memmove ranges against the host implementation. Both suites pass ASan and UBSan.

The live runner injects the native ELF only into a disposable CHFS disk. It calls
standard allocation/string symbols from C and C++, exhausts all 16 real mappings,
checks the next request fails ENOMEM while existing contents survive, then frees
everything. Repeated exec/spawn launches and deliberately abandoned heaps must
quiesce and return equal physical free-page counts. Both profiles execute through
the real scheduled runtime; the direct profile also returns to the boot shell and
executes synchronously. UEFI stays in the scheduled runtime. The runner records
the exact app digest and kernel snapshot in its output. The accepted direct run
returned 117699 free pages before/after; the matched UEFI run returned 500560
before/after. Both used app SHA256
`e1fbc40e262e055b30d097d05145c787a9964cf489de0c79aef2b2a4546f05e7`.
The UEFI kernel/ISO embedded-kernel hashes matched, rather than probing symbols
from a different compile. See `validation/stage3-native-c-runtime.json`.
QEMU results do not prove Fusion, Pi 5, browser-engine or full revised Stage 3
acceptance. All binary
publication remains held under the user's Stage 3 completion instruction.

See [native clocks](NATIVE_CLOCK_API.md) for the subsequent read-only clock API
and limited C/C++ time conversion implementation. Full POSIX/timezone/thread
support remains separate.

The larger bounded profile is qualified in [NATIVE_LARGE_RESOURCES.md](NATIVE_LARGE_RESOURCES.md).
It retains the2MiB aperture and guards while testing1MiB allocations,4MiB images
and8MiB owned frames; these remain below the complete modern engine footprint.

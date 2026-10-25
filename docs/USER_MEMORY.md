# Bounded EL0 anonymous memory: revised Stage 3 runtime groundwork

This implementation supplies real zero-filled private memory to direct-boot EL0
applications through CHOS syscalls 35/36. It is not a full POSIX mmap/malloc layer,
an engine port, shared-memory IPC, or a working isolated UEFI runtime.

`chos_map_memory(bytes)` accepts 1..128 KiB and returns a process-private base or
negative errno. Nonzero flags, including high 64-bit flag bits, are rejected.
Mappings round to 4 KiB pages, are read/write and execute-never, and have unmapped
guards. There are at most 16 tracked regions in a bounded two-MiB arena per address
space; the existing four-MiB owned-frame limit remains. The caller cannot select a
physical address, overwrite its image/stack or request executable heap memory.

`chos_unmap_memory(base, bytes)` requires the exact region base and matching rounded
length. Misaligned, wrong-size, foreign-space, image/stack and double-unmap requests
are rejected. A stale address-space handle fails generation/identity validation.
AS-owned pages are zeroed by the existing allocator before exposure. W^X is retained.

Allocation/mapping failures roll back completed pages. Partial unmap/free failures
quarantine the region; uncertain pages are never reused as someone else's storage.
The process reaper unmaps all abandoned/quarantined regions before destroying the
address space, where the existing owner allocator zeroes/frees remaining frames.
The lock order is user-memory metadata → address-space → PMM, with IRQs masked and
no allocation-time yields. Each request handles at most 32 pages, rather than a
browser-sized unbounded mapping or full RAM scan. This is single-core support.

## Evidence

Host/ASan/UBSan exercise size/flag bounds, owner separation, quotas, partial
allocation/map rollback, unsafe unmap attempts and quarantine. The live
`tests/qemu_user_memory.py` loads an actual CHCX app into a disposable direct guest
and checks zeroing, writable data, invalid sizes/high flags, exact unmap, code-page
protection and double-unmap. It deliberately faults a guard write and an indirect
call into RW/XN memory; both faults are contained to the EL0 process and normal
program execution continues afterward. Tests exercise the default scheduled shell
and a return to the synchronous boot shell. Abandoned mappings are reaped and PMM
free-page counts match after quiescence.

The accepted live run returned 118,023 free physical pages before and after the
normal, faulted and scheduled application sequence. The application input SHA256
was `f93d600b081714ad56eb07ee4cdb6f32ff7e23682d8d1fa537d9b1d6853a9247`.
The full host and sanitizer suites passed. Separate live regressions passed for
EL0 network-status/profile protection, UEFI Intel e1000e DNS/HTTP, the direct-boot
platform baseline, and graphical install/persistence/restart/shutdown. These are
QEMU results; they do not establish Fusion or modern-browser acceptance.

To repeat the focused checks in the prepared environment:

```sh
source /workspace/chos-tools/activate
make CC=clang LD=ld.lld test-user-memory test-user-memory-sanitize build/userland/memory_test.chcx build/qemu-smoke-disk.img all
python3 tests/qemu_user_memory.py --out /workspace/chos-evidence/user-memory-repeat
```

Use a fresh output path. The runner copies the prepared smoke disk and injects
the test app only into that disposable copy.

The first expanded runner compared a scheduled-shell snapshot with pending cleanup
against a fully reaped boot-shell snapshot. It found 42 *more* free pages, not a
leak. The corrected runner waits for actual `procinfo` active/pending-cleanup zero
before comparing counts. The retained earlier failure is not used as acceptance.

The previous 54b9731 ISO remains the published network milestone; this runtime
follow-up follows the next source-only iteration. It does not change the stated
modern-engine, TLS, Wi-Fi, IPv6 or Fusion-validation gates. Larger heaps/images,
robust multithreading/futex/event APIs, read-only/file/shared mappings and UEFI MMU/
DMA integration still require separate implementation and security tests.

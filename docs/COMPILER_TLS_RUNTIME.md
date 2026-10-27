# Compiler-emulated C/C++ thread-local storage

CHOS now links LLVM compiler-rt's emulated TLS implementation into the native C
runtime. Real Clang-generated C11 `_Thread_local` and C++17 `thread_local` code
runs in isolated EL0 processes over native pthreads. This is an explicit static
build profile, not ELF PT_TLS loading, a dynamic linker, libc++ or an integrated
browser. The complete revised Stage3 and its0.50.0 release remain open.

## Build and ABI

Compile every translation unit using compiler TLS with `-femulated-tls`, then
link the native libc objects. The fixture rules in userland/libc/Makefile.inc
show the exact C/C++ commands. Clang emits ordinary RW emutls control records,
constant initialization templates and calls to `__emutls_get_address`; the loader
does not need to interpret TLS relocations or install a different TPIDR layout.
The compiler runtime uses native pthread-specific storage, while the lowest-level
allocator, errno and pthread implementation retain their allocation-free native
TCB bootstrap. They must not acquire a dependency on compiler TLS themselves.

The pinned source matches the installed Clang19.1.7 toolchain. Its original
emutls source and five-file header/license closure are retained without source
patches under third_party/compiler-rt. The license is Apache-2.0 WITH
LLVM-exception; its complete text is included. The immutable upstream commit,
all six SHA-256 values and scope are in dependencies/compiler-rt-emutls.json.
`python3 tools/verify_compiler_rt.py` checks the exact offline closure. The GitHub
API lookup was unavailable through the network proxy; `git ls-remote` resolved
the annotated llvmorg-19.1.7 tag and peeled immutable commit, then original bytes
were fetched from that commit through verified HTTPS. No TLS verification bypass
was used.

Build definitions select native posix_memalign and checked CHOS adapters for
pthread once/lock/unlock/setspecific. A synchronization failure aborts the process
instead of letting upstream continue with an unprotected index registry. Its key
creation hook selects an internal late-cleanup key; this consumes one of the64
real key slots when compiler TLS is first used. Key exhaustion, failed allocation
or invalid alignment fail closed through abort134 and OS-owned process cleanup.
No successful fake TLS object or fallback shared value is returned.

## Lifetime and C++ cleanup

Each worker owns its value/address array. Upstream grows that array as more
compiler controls acquire indices, copies nonzero initialization templates,
zero-initializes other values and honors power-of-two alignment. Index publication
is acquire/release and native mutex protected; different workers never share a
value just because they reference the same compiler control.

The static-link Itanium entry points `__cxa_thread_atexit` and
`__cxa_thread_atexit_impl` register real destructor callbacks on the current TCB.
Thread exit pops them in reverse registration order, releasing list nodes before
invoking user code. Callbacks may register additional destructors. There are at
most64 pending callbacks per thread and256 invocations per cleanup; exhaustion
or a self-rearming loop aborts the process with bounded cleanup. This supplies
thread-local C++ constructors/destructors emitted by the tested Clang profile,
not global init arrays, process `atexit`, exception unwinding or DSO unloading.
The DSO parameter is opaque in this static-link profile.

C++ thread-local destructors run before ordinary pthread key destructors. The
compiler address array survives all four ordinary key-destructor passes, including
callbacks that reset a key and access compiler TLS again. Only then does its late
destructor free the values and array. This avoids a final-pass recreation leak and
keeps compiler TLS alive while cleanup code still needs it. Registry locks are
released before callbacks, and the allocator serializes its own transactions.

Normal main return now calls a weak native-libc finalization hook before process
EXIT. It runs main C++ thread-local destructors while their values and main stack
remain valid. Raw SDK and CHCX binaries use its local weak return-function
fallback and continue through their existing EXIT path. Normal process exit does not run
ordinary pthread key destructors; explicit main pthread_exit does, preserves live
workers and allows its result to be joined. Other remaining process storage is
owned by the existing process reaper.

## Verification and limits

Host tests exercise the unmodified upstream implementation with real host pthreads:
64 controls, address-array growth, initialized values,16/4096-byte alignment and
repeated worker destruction under ASan/UBSan and leak detection. A separately
prefixed native C++ cleanup test checks per-TCB lists,64 reverse callbacks,
registration during cleanup and the normal-main hook. These host tests do not
establish CHOS hardware support.

`tests/qemu_emutls.py --mode direct|uefi --out FRESH` runs the real C/C++ compiler
output and runtime. The fixture has48 compiler controls and rejects any PT_TLS
program header or unresolved runtime import. Three workers preserve initialized,
zeroed, aligned and independently modified values across scheduling. C++ objects
construct once, destruct in reverse order, then ordinary C key callbacks access
TLS through four passes. After join the user heap returns to its exact main-thread
baseline. Three lifecycles per process and repeated processes must preserve warm
physical pages; main normal-return and pthread_exit cases validate cleanup and
surviving workers. Separate key/callback-quota and destructor-loop processes must
exit134, reclaim pages and leave no kernel futex records.

See validation/stage3-compiler-tls.json for exact evidence, plus legacy pthread,
TLS, typing and raw startup regressions. New Fusion/Pi/laptop qualification is
NOT RUN. Native heap/stack/key/thread quotas remain; this does not make the current
small runtime engine-sized. Untested GCC common-TLS registration, ELF native TLS,
dynamic libraries, global C++ initialization, the full C++ ABI/standard library,
modern engine and remaining network/wireless requirements stay open. Before any
production browser dependency release, review upstream compiler-rt advisories and
changes, update the pin/license/checksums deliberately, and rerun both compiler ABI
and guest lifetime suites. No binary or build tag is published for this milestone.

The first startup hook used ADRP/ADD to test an undefined weak function against
zero. ARM64 linker semantics instead produced the current entry page in the raw
ELF, so a returning raw application could re-enter _start. The raw-thread regression
failed; its extracted failing executable and disassembly are retained. The local
weak return function fixes this while libc's strong function still overrides it.
Only subsequent runs with that correction are accepted for final qualification.

A parallel typing regression initially sampled the document at a fixed delay
after asynchronous USB removal. Its final diagnostic already had repeat_key0
and empty queues, consistent with a final previously queued event completing
after the snapshot. The oracle now requires actual kernel key release, queue
drain, eight further guest dispatches and a matching presented caret before its
stable-text check. It still fails if release/drain misses bounded deadlines or
new repetition appears. The original failure is retained; no driver behavior was
changed to discard legitimate queued input.

The subsequent [native static CRT](NATIVE_STATIC_CRT.md) adds actual global
constructors,process atexit/selective finalization,last-pthread process cleanup
and post-key C++ TLS destruction with a shared per-TCB cleanup budget.
[Current evidence](validation/stage3-static-crt.json) supersedes earlier missing-
global-startup/atexit status for the bounded dense static profile. Full C++/POSIX,
ICU/modern engine,production security policy and wireless remain open; binaries
stay held.

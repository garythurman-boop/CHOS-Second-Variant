# Native C++ synchronization and ICU threading component

This is a working prerequisite for ICU/JavaScriptCore, not a modern browser or
completion of revised Stage 3. Version 0.50.0 and new binaries remain held.

The native libc++ archive now builds LLVM 19.1.7's original `mutex.cpp`, mutex
and condition-variable destructors, `condition_variable.cpp` and `memory.cpp`.
The expanded unmodified dependency closure contains 716 files, pinned to the same
LLVM release/archive/commit as the existing container profile. The original-byte
manifest and offline verifier cover every imported file. Merely importing a
header does not qualify all the facilities it declares.

CHOS supplies the external threading bindings over its own pthread/futex runtime.
The bounded no-exceptions/no-RTTI profile supports mutex, recursive mutex, timed
mutex, recursive timed mutex, lock_guard, unique_lock, scoped_lock, condition
variables, condition_variable_any and concurrent shared_ptr/weak_ptr ownership.
Native system_clock and steady_clock read the checked clock syscall. Failed
clock reads terminate rather than invent a timestamp; availability of wall time
is still not authenticated time for production TLS policy.

`pthread_cond_clockwait` selects CLOCK_MONOTONIC or CLOCK_REALTIME per call without
mutating the condition's configured clock. The generated libc++ configuration
explicitly selects this implemented API, so relative/steady waits use monotonic
deadlines. This does not identify CHOS as Linux or claim full POSIX timers. Native
waits round up to 100-Hz scheduling ticks, reacquire the mutex on return and retain
the existing predicate/spurious-wakeup contract. C pthread timed-lock functions
are still absent; C++ timed locks use the original upstream mutex/CV algorithms.

A native call_once implementation preserves libc++'s actual AArch64 64-bit flag
ABI and all-bits-set completion value, with full-width acquire/release publication.
It does not reinterpret the flag as the different 32-bit pthread_once ABI. A low
32-bit futex wait is valid only on the explicitly checked little-endian target.
Up to 16 nested active initializers are recorded in each native TCB. pthread_exit
resets unfinished flags and wakes contenders, including an initializer entered
from a key destructor during thread cleanup. Recursive ownership and depth17 fail
closed. Exceptions, cancellation and unwinding remain unsupported. In this profile
C++ system-error paths terminate with status134; error_category/exception objects
are not supplied by the terminating helper.

The C runtime now supplies the real assert/NDEBUG contract and reports expression,
file, line and function before aborting the process. The added POSIX errc constants
supply the standard header vocabulary; they do not implement corresponding APIs.

## Original ICU code in the guest

The separate `third_party/icu-threading` closure contains 24 original ICU79.1 files
from commit `ddc6b66926bfac6036144ca9902992a0d6d50af9`, including its Unicode License
V3. `tools/verify_icu_threading.py` verifies these bytes offline. This component
builds the actual `umutex.cpp` with U_DEBUG=1 and enabled assertions against the
CHOS C++23 SDK. No thread-disabled variant or fake condition-variable header is
used. Normal builds need no upstream reference checkout or network.

The integration fixture provides one explicitly test-only cleanup recorder. It
accepts only UCLN_COMMON_MUTEX, saves the real callback registered by umutex.cpp,
and invokes that callback after all workers have joined. This tests the original
mutex cleanup and placement reconstruction of once_flag. It is not a production
replacement for ICU's full cleanup registry or u_cleanup, and no ICU uc/i18n/data
libraries are exported or installed. Full ICU, Unicode conformance and production
release/security qualification remain open.

## Validation

`tests/qemu_cxx_sync.py --component cxx|icu --mode direct|uefi --out FRESH` installs
the exact native ELF on disposable CHFS media and snapshots its boot inputs.
The C++ profile checks three contending workers, correct predicate/data publication,
all four mutex families, timeout and lock reacquisition, scoped locks, shared/weak
ownership and final destruction, nested once, owner-exit retries and process-local
negative cases. The ICU profile checks eight lazy mutexes and the global mutex,
UInitOnce success and shared failure status, four cleanup/reinitialization cycles,
and a native debug assertion abort. Both require successful process exits, clean
relaunch after refusal, stable warm PMM pages and zero queued futex wait records.
See [exact evidence](validation/stage3-cxx-synchronization.json).

Host normal and ASan/UBSan synchronization checks exercise full-width once state,
malformed flags, recursion/depth, abandoned ownership, futex failure, explicit
clock selection, native clock failure and lock reacquisition. They do not replace
guest scheduling tests. The first C++ direct run predates shared/weak ownership and
the final ICU direct run supersedes its initial fixture; these remain intermediate
results rather than final evidence. Initial compile/build evidence includes missing C++20 transitive system_error
and a fixture reference to ICU's separate Mutex helper corrected to
std::lock_guard<UMutex>. The initial debug assert dependency failure is recorded
in icu79-sync-debug-initial-diagnosis.txt; its original stdout log was reused by
the successful compile, so that text is explicitly a diagnosis, not raw evidence.

std::thread/futures/notify_all_at_thread_exit runtime, atomic wait/notify,
exceptions/RTTI, complete locale/stdio/POSIX, engine-sized memory, full target ICU,
JavaScriptCore/WebCore, production TLS policy and real Pi5 wireless remain open.
New Fusion/Pi/laptop qualification is NOT RUN. Passing QEMU tests does not establish
those platform results.

Accepted final evidence under `/workspace/chos-evidence` is
cxx-sync-final-direct/uefi and icu-sync-final-direct/uefi (26 process cases), plus
cxx-sync-crt-uefi (18 lifecycle cases), cxx-sync-pthread-direct,
cxx-sync-tls6-uefi (certificate-verified TLS1.3 AES256-GCM over Intel Ethernet/IPv6),
cxx-sync-typing-uefi, cxx-sync-emutls-uefi and cxx-sync-cxx20-uefi. Aggregate host,
ASan/UBSan, local ISO/prepared-image builds and direct smoke checks also pass.
Completed firmware-variable copies are losslessly gzip-archived with original
SHA256/mode/restoration entries in completed-runtime-vars-archives.json; logs,
disposable disks and boot snapshots remain available. No binary was published.

The subsequent [native thread iteration](NATIVE_CXX_THREADS.md) qualifies a bounded
std::thread/notification/termination profile and extends the original closure
to720 files. Its scope and evidence supersede the earlier absent-thread-runtime
status here. Public futures, exceptions and the other full-stage gates remain open.

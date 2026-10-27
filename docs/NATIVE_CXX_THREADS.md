# Native C++ threads and ordered exit notification

This supplies a bounded native runtime prerequisite for ICU/JavaScriptCore. It
is not full libc++, a modern browser, or completion of revised Stage 3. New
binaries remain held and version0.50.0 remains reserved for full acceptance.

LLVM libc++19.1.7's original thread.cpp now runs over CHOS pthreads. Its public
constructor/move/swap/id/join/destructor code and thread-exit notification list
are used without modifying the upstream bytes. The original dependency closure
now contains720 files, including future.cpp for the private readiness callback
referenced by the thread-exit list. The same release/archive/commit and offline
hash verifier apply. A real promise<int>/get_future/set_value/get link probe fails on missing
exception_ptr/error-category/logic-error ABI (cxx-threads-future-probe/build.log).
Public promises/futures/exception_ptr and exception objects are not supplied or qualified; importing future.cpp is not a full futures port.

The native static C++20/23 profile is compiled at -O2 without exceptions or RTTI.
It supports three live worker/join records per process, with the existing native
32KiB default worker stacks and allocator/mapping limits. It does not increase
those limits to engine size. Workers inherit the creating thread's floating-point
environment through the existing pthread runtime. Move-only callables/arguments,
thread IDs, move/swap/join, native yield and the selected sleep cases execute in
the actual guest. hardware_concurrency returns the standard's allowed unknown
hint0; the original warning is retained, with only that file's warning promoted
back from -Werror. No invented CPU-count syscall or foreign OS identity is used.

## Cleanup and failure contract

A small native start wrapper marks the C++ callable while upstream's proxy owns
its tuple/functor allocations. Calling pthread_exit from that callable would skip
C++ automatic destruction because this runtime has no forced unwind. It therefore
aborts the process with134 instead of silently stranding those resources. Normal
return destroys the callable state, clears the marker and enters the ordinary
pthread exit path. Raw pthread initializer-exit/once retry behavior is preserved;
POSIX cancellation and general C++ stack unwinding remain absent.

The standard-library thread-specific key uses a private final-notification phase:

1. Abandon unfinished native once/static guards; run C++ TLS destructors.
2. Run ordinary pthread-key destructor passes, then drain C++ TLS created there.
3. Destroy libc++ thread-exit state, executing its original deferred notification
   and mutex unlock, while compiler TLS storage is still retained.
4. Perform last-C-thread/process finalization as needed; release compiler TLS,
   publish the join result and let the kernel collect the stopped worker.

This final phase does not run arbitrary ordinary keys a second time. Private
notification callbacks and termination handlers cannot escape it via pthread_exit.
The test verifies both normal TLS destruction and a new nontrivial TLS object
created by an ordinary key callback in a separate translation unit. Its destructor
checks that the exit mutex is still held, independently of scheduling order.
The runtime keeps compiler TLS available through notification and existing global
teardown. It does not promise pthread-key notifications on process exit in place
of orderly joined worker completion.

std::set_terminate/get_terminate use atomic handler publication. A default handler
aborts; a returning handler also aborts. Recursive termination and handler
pthread_exit abort without stranding a thread. An explicit handler exit can
terminate the process with its requested status. These termination functions do
not claim to implement exception_ptr, C++ exceptions or foreign unwinding ABIs.

Detached threads remain unsupported: the native binding returns ENOTSUP and the
standard no-exception wrapper aborts. Empty/self join, exhausted worker capacity,
destroying a joinable thread and move-assigning over a joinable thread also fail
locally. The kernel and later applications remain usable after these refusals.

## Evidence and remaining scope

`tests/qemu_cxx_threads.py --standard 20|23 --mode direct|uefi --out FRESH` installs
the exact native ELF on disposable CHFS media. It checks repeated three-worker
lifecycles, heap reclamation, TLS/notification order, move-only argument cleanup,
IDs/moves/swaps/joins, default and custom termination handlers, deliberate refusal
processes and successful relaunch. It requires stable warm physical pages and no
queued futexes. Nanosecond, runtime integer-millisecond, runtime floating-second,
negative/zero sleep and steady-clock sleep_until cases use the real monotonic API.
Arbitrary chrono durations, unoptimized binary128 compiler lowering and full
chrono conformance remain unqualified.

The first direct run predates the final sleep/ordering fixture. A deliberately
misordered binding initially passed because checking destruction only after a
waiting thread acquired the mutex depended on scheduling. That run is not proof
of correct order. The stronger destructor-time lock invariant rejects the matched
binding that registers libc++ cleanup as an ordinary key. Its source, binaries,
passing intermediate and rejected run remain under
`/workspace/chos-evidence/cxx-thread-order-*`. Production sources were not rolled
back. See [exact final results](validation/stage3-cxx-threads.json).

An exploratory jthread/stop_token compile failed because LLVM19 keeps that feature
behind its experimental configuration. It was not enabled to bypass the failure;
it is outside this qualified std::thread profile. A preliminary comment anticipating
missing C++20 transitive headers was corrected after the actual upstream C++20
compile passed without any extra imports. Full ICU common/i18n/data, JavaScriptCore,
WebCore/browser integration, engine-sized runtime/isolation, production trust/time/
reseed policy and real Pi5 radio support remain open. New Fusion/Pi/laptop execution
is NOT RUN, and QEMU evidence must not be treated as that qualification.

Accepted final evidence under `/workspace/chos-evidence` comprises
cxx-threads-final-direct20/direct23/uefi20/uefi23 (60 process cases), plus
cxx-threads-crt-uefi (18 lifecycle cases), cxx-threads-sync-direct,
cxx-threads-emutls-uefi, cxx-threads-icu-direct, cxx-threads-tls6-uefi,
cxx-threads-typing-uefi and cxx-threads-math-uefi. Aggregate host and ASan/UBSan,
local media builds and direct smoke checks also pass. The matched final controlled
counterexample is cxx-thread-order-final-before with before-final.elf; it is
expected to fail the destructor-time lock invariant, not counted as a passing
profile. Completed firmware-variable copies are losslessly archived with original
SHA256/mode/restoration metadata in completed-runtime-vars-archives.json. Raw logs,
disposable disks and immutable boot snapshots remain retained. No binary is published.

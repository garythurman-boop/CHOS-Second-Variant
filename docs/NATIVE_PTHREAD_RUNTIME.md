# Native C thread runtime

The freestanding user C library now supplies a bounded pthread subset over actual
EL0 threads and private futex waits. This is a prerequisite for the browser port,
not a modern browser or a complete POSIX/C++ runtime. Development remains 0.30.0;
the Stage3 binary publication hold stays active.

## Implemented contract

`pthread_create` maps a dedicated RW/XN stack and passes an aligned runtime TCB as
the native thread pointer. A joinable worker calls its entry, runs thread-specific
destructors, publishes its return value, and exits through the native trampoline.
`pthread_join` waits without busy polling, waits for kernel termination before
reaping, then releases its stack and TCB. Each process still permits three live
secondary join records. Capacity failure returns EAGAIN without changing the output
handle. Default stacks are32KiB; requested16..128KiB lengths round upward to4KiB.
Handles have process-local lifetime; callers must not reuse a joined handle or
race multiple joins with its destruction.

The main thread can call `pthread_exit`: its static TCB and loader-owned stack
remain valid while workers run, its result can be joined, and process teardown
waits for the remaining workers. A normal main return, process EXIT from any
worker, or a user fault still terminates the group. Fault status takes precedence.
This extends THREAD_EXIT59 to the main scheduled task; synchronous single-task
profiles use the explicit no-sibling process-exit fallback.

The [native math extension](NATIVE_MATH.md) now captures creator FPCR/FPSR and
installs that environment before a worker callback. Raw kernel Q registers remain
fresh, while C pthread rounding/status follows the creator and is isolated after
creation. The native C runtime retains its integer compilation contract through
a small user-only assembly shim.

`errno` lives in the current TCB, using TPIDR_EL0 state preserved by the kernel.
The main TCB initializes lazily without allocating. The allocator serializes whole
heap transactions, including map/unmap, metadata changes and error capture, through
a private futex mutex. This makes the supplied malloc/calloc/realloc/free/alignment
operations safe among native C threads. It does not make unrelated libraries
thread-safe. The subsequent [TLS-client concurrency contract](NATIVE_TLS_CLIENT.md)
separately serializes that component's shared PSA state and scopes its RNG to the
executing thread; arbitrary third-party libraries still need their own contracts.

Mutexes support normal, error-checking and recursive types, try-lock, owner-checked
unlock, and busy-destroy refusal. Contended locks sleep on a32-bit futex word;
acquire/release atomics protect the caller's data. No FIFO fairness is promised.
Condition variables support signal/broadcast and absolute REALTIME or MONOTONIC
timeouts. Sequence comparison avoids the unlock-to-wait lost-wake interval; every
return after unlocking attempts to reacquire the original mutex. Timeouts round
upward to100-Hz ticks and long deadlines use bounded600-second waits. Applications
must loop on their predicate because spurious wakeups are permitted. Recursive
mutex condition waits require depth one.

`pthread_once` sleeps under contention and publishes initialized data with release
ordering. Up to eight nested active initializers are tracked per thread. If an
initializer calls pthread_exit, its unfinished flag resets and wakes contenders so
another thread can retry. This is not C++ exception unwinding or POSIX cancellation.

There are64 thread-specific keys. Generation-tagged keys reject stale deleted-key
values after slot reuse. Key deletion does not run destructors. Thread exit clears
each value before calling its destructor, permits it to reset the key, and performs
at most four destructor passes. Registry locks are released before callbacks.

## Explicit boundaries

Detached threads and process-shared mutex/condition attributes return ENOTSUP.
Robust/PI locks, cancellation, signals, priority/affinity, barriers, rwlocks and
timed mutex APIs are not supplied. Unsupported functions are not exported as
success stubs. This TCB is a native runtime layout, not ELF PT_TLS, compiler TLS
relocations, libc++ or `std::thread` support. The subsequent
[compiler-emulated TLS profile](COMPILER_TLS_RUNTIME.md) supplies tested Clang
thread-local values and C++ thread-local destructors through this runtime. Raw SDK
threads using another opaque TCB cannot silently use this libc layout; the runtime
fails closed on a mismatched TCB magic. Engine-sized memory and loader/runtime
integration remain open.

## Verification

The host fixture compiles the real synchronization implementation with deterministic
futex/clock/TCB mocks and prefixes its exports to avoid interposing the host's
pthread ABI. ASan/UBSan checks include contention wake, interrupted wait, owner and
recursive overflow, timeout rounding/clamping, failed clocks, predicate sequence,
mutex reacquisition and unfinished once reset. These are not hardware tests.

`tests/qemu_pthread.py --mode direct|uefi --out FRESH` runs the actual C library in
EL0. Three workers each allocate, verify, grow and free120 buffers per lifecycle;
three lifecycles run per process and three processes are repeated. Tests check
isolated errno and specific values across scheduling, mutex/condition predicates,
exactly-once initialization, key generations/quota, two-pass destructors, custom
stack rounding and fourth-worker capacity rollback. Separate processes check
initializer pthread_exit/retry and joining an exited main thread. The runner
requires stable warm physical pages and zero remaining kernel futex wait records.
The legacy C/C++ header/link/runtime test also calls the native pthread interfaces;
it does not claim a C++ standard library. Exact evidence is recorded in
validation/stage3-native-pthread.json. New Fusion/Pi/laptop qualification is NOT RUN.

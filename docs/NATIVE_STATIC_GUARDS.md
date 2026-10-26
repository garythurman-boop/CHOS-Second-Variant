# Native C++ static-initialization guards

The native AArch64 runtime now implements actual compiler-emitted
`__cxa_guard_acquire/release/abort` operations. This closes the function-local
initialization-guard gap for the qualified no-exceptions profile. It does not
provide global startup constructors, process atexit, exceptions/unwinding,
DSO loading, std::thread/mutex runtime or a complete C++ ABI/browser engine.
A function-local object with a nontrivial destructor still needs the unimplemented
process atexit ABI; it must fail to link rather than silently omit destruction.

The little-endian AArch64 Itanium guard object is eight-byte aligned. Its first
byte remains zero throughout initialization and becomes complete only with a
release publication. The compiler's acquire-byte fast path observes that state.
A checked atomic first word carries pending/waiting bits in the next byte;
the remaining reserved guard bytes are untouched. Competing native threads sleep
on the existing private futex service, recheck after wakes/races/timeouts and
receive the completed object only after acquire synchronization. Futex errors
outside the supported retry set abort the process instead of spinning in a fake
success loop. An initializer can itself wait indefinitely on application work;
individual futex sleeps are bounded, not an overall constructor deadline.

Each native TCB owns a16-entry active-initializer stack. It records full guard
pointers rather than truncating pthread_t or relying on reusable thread IDs.
Recursive initialization and depth overflow fail closed with process exit134;
only the top owning initializer may publish or abort. Abort clears pending state
and permits retry. A worker leaving through pthread_exit abandons all owned
pending guards and wakes competitors before its TLS/key cleanup. The final exit
path repeats that check because a key/TLS destructor can start an initializer and
exit recursively. Completed guards remain cached; pending guards never become
complete simply because their owner exited. Process faults/exit still use the
existing process-wide kernel cancellation and reclamation boundary.

## Verification

`make test-cxx-guard test-cxx-guard-sanitize` checks actual native guard code with
host model hooks, including publication/abort, waiting notification, recursive/
quota/alignment/corrupt-state refusal, unexpected syscall failures, supported
retry errors and complete abandonment. Host names are prefixed to avoid replacing
host C++ runtime symbols. These checks are not live-thread evidence by themselves.

`tests/qemu_cxx_guard.py --mode direct|uefi --out FRESH` executes a real native
C++20 ELF with compiler-generated guard fast paths. Three barrier-synchronized
pthread workers contend on eight sleeping initializers and verify complete record
contents and exactly-once initialization. A constructor's pthread_exit wakes a
retrying owner; another constructor does the same while executing a pthread-key
destructor. Both paths finish without stranded waiters. Nested16 initializers
succeed; recursive initialization and depth17 abort only their own process.
Manual abort/retry is checked too. Three worker lifecycles per process, repeated
processes and a successful relaunch after refusal retain zero native heap bytes,
stable warm physical pages and zero queued futex waiters. The fixture has no
unresolved imports, global initarray or PT_TLS dependency.

See [machine-readable evidence](validation/stage3-static-guards.json). Accepted
logs/guest snapshots are under `/workspace/chos-evidence`. Host/sanitizer suites,
UEFI compiler-TLS/destructor tests, C++ container/failure isolation and threaded
verified TLS1.3 over IPv6 regressions cover the changed TCB and pthread exit path.
New Fusion/Pi/laptop behavior is NOT RUN. Binary publication remains held;
version0.50.0 and full revised Stage3 acceptance are not claimed.

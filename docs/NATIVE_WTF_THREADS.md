# Actual native WTF worker lifecycle

`CurrentThreadCHOS.cpp` now supplies worker creation, join and detach for the
pinned upstream WTF library. Original `Thread::create`, `NewThreadContext`,
`Function`, current-thread stack initialization, `ThreadGroup`, registry and
two-pass TLS cleanup execute unchanged. The C trampoline passes the original
opaque context to `Thread::entryPoint`; it does not copy its private layout.

Creation uses native managed pthreads. The upstream parent/child condition
handshake establishes the real handle and current stack before returning.
Default32KiB and explicit64/128KiB owned anonymous stacks are admitted under the
existing three-secondary-worker/process bound. Invalid sizes, arbitrary custom
stack locations, FIFO and realtime requests are refused before native creation.
Upstream `Thread::create` treats refusal as a release assertion; these cases are
isolated child tests, not a nullable-success interface.

Only ordinary native scheduling is supported. Names, QOS and demotion remain
advisory/unapplied hints, like optional attributes on other generic upstream
backends. No kernel name, priority or affinity change is reported. Current-thread
initialization checks the actual kernel context version, flags, native TID and
stack before running the original entry point.

Join claims the WTF join state under its WordLock, then releases that lock before
blocking in native pthread_join. Original TLS exit needs the same WordLock.
A refused self join restores joinability. Successful joins and detaches retain
a guard even after worker exit: subsequent joins return EINVAL before touching
the released pthread TCB. Repeated detach fails closed before native handle use.
The real managed kernel stack and TCB ownership contract is described in
[NATIVE_DETACHED_THREADS.md](NATIVE_DETACHED_THREADS.md).

The fixture creates three concurrent workers with distinct stack sizes in six
modes: join, live detach, self detach, self-join refusal followed by parent join,
join after native exit, and detach after native exit. It runs one warm sequence
and three measured sequences:72 parent workers plus four workers in five refusal
children per process. Three root lifecycles exercise228 secondary workers across
18 processes per guest mode, including15 expected child aborts. It checks actual
Function captures, current-thread identity and stack bounds, C++ TLS followed by
two ordinary-key destructor passes, original registry/group cleanup, and stable
allocator/mapped/resident bytes. Upstream removes exited workers from a group's
membership while retaining weak back-references until that group is destroyed;
the test checks both parts of that lifetime.

Final direct and isolated UEFI profiles pass, with exact cold-page equality and
zero remaining futex waiters. Rebuilt actual WTF filesystem/configuration, JSC
options/checked-Wasm policy and JSC register/profiler components also pass UEFI
regressions. The unchanged kernels, media, SDK and all other engine objects match
the preceding full host/sanitizer and20-profile baseline. See the
[exact qualification](validation/stage3-wtf-threads.json), including retained
failed fixture builds/runs and diagnoses. This does not claim a rerun of the
unchanged baseline or execution of a full JavaScript VM.

```sh
python3 tools/build_wtf_threads.py --source /path/to/pinned-webkit \
  --native-sdk build/native-sdk --engine-build /path/to/native-engine/cmake \
  --out /tmp/chos-wtf-threads
python3 tests/qemu_wtf_threads.py --application /tmp/chos-wtf-threads/cmake/wtf_threads.elf \
  --out /tmp/chos-wtf-threads-direct
python3 tests/qemu_wtf_threads.py --mode uefi \
  --application /tmp/chos-wtf-threads/cmake/wtf_threads.elf --out /tmp/chos-wtf-threads-uefi
```

This qualifies a bounded worker lifecycle, not every WTF service. Foreign-thread
suspend/resume/register capture, signal delivery, engine-sized resource admission,
a linked/running JavaScript VM, WebAssembly and WebCore/browser execution remain
open. Current32KiB default stacks do not satisfy the engine's64KiB GC reserve.
Native Pi5 Wi-Fi, production security and final independent Fusion/Pi5/laptop
acceptance remain required. Development stays0.30.0 until all revised Stage3
gates justify0.50.0; no new distribution binary follows from this component.

The [native foreign-pause/WTF coordination component](NATIVE_FOREIGN_GC.md) now
supersedes earlier missing-foreign-method diagnoses: the full VM link has no
undefined symbols. Its image remains unadmitted; full VM/GC/browser execution and
the remaining revised Stage3 gates are still open.

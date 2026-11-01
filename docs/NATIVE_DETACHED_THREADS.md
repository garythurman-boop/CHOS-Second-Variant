# Native detached workers and owned stack cleanup

Native pthreads now support detached attributes, live/self/after-exit detach,
and local main-thread detach. Original libc++ std::thread::detach uses this real
backend. Detach transfers cleanup ownership and returns without joining the
worker. Existing three-worker/process and four-process bounds remain unchanged.

Raw request version1 keeps its original behavior, including stack unpinning at
exit and explicit join-record collection. The64-byte managed request version2
requires MANAGED_STACK in reserved[0], optionally CREATE_DETACHED, and zero in
reserved[1]. It retains the exact anonymous-stack pin until collection or detached
disposal. The32-byte information structure remains version1. Native pthreads
select managed requests; arbitrary custom/image-backed stacks remain unsupported.

THREAD_DETACH79 uses the existing REAP58 permission class. Initially detached
creation also requires REAP58 in addition to CREATE56, closing a flags-based
permission bypass. No new positive-mask bit is consumed. The raw operation only
accepts the caller's managed secondary TIDs. C main-thread detach changes local
joinability; its static TCB and process-owned main stack remain until process
teardown. Foreign-process TIDs cannot be inspected, collected or detached.

A managed stack is never unpinned and left available for address reuse before
disposal. The memory owner validates and consumes its exact pin under one lock,
then releases the owned mapping/frames. Argument and ownership refusals preserve
the pin. Unexpected partial VM failures consume the pin into quarantine, retain
uncertain owned frames and stop the process; they do not report successful
cleanup. Separate scheduler kernel stacks retain their existing lifecycle.

Creation acknowledgement retains a fast-exiting detached record until its
creator receives the actual TID. Native pthreads additionally publish their
output handle and TID under a private lifetime lock before allowing the callback
to start. Join and detach arbitrate ownership under that lock. Expired or arbitrary
pthread handles are outside the valid C API contract; a user TCB pointer is not a
global kernel identity or cross-process authority.

All original C++ TLS, ordinary-key, post-key TLS and final notification phases
run before publishing completion. Detached self cleanup then uses a serialized
private static TLS owner while freeing its original TCB. This lets the allocator
unlock its actual pthread heap mutex after the old TCB's arena is unmapped.
The private lifetime lock itself uses raw futex operations without a TLS owner;
no user callback or TLS access follows its final unlock. Checked TCB release
errors fail the process. No user callback observes the temporary cleanup owner.
The heap backends do not call lifecycle APIs while holding the heap lock.

The native fixture covers112 managed library workers in each parent and eight
in its children, plus26 raw workers per cycle. Three cycles exercise360 managed
and78 raw workers across18 processes. It verifies repeated slot reuse, FP/errno
isolation, C++ TLS followed by two ordinary-key passes and post-key TLS before
real exit notifications. Detached-main testing observes its actual static TCB
completion publication before allowing the remaining worker to finish. Each wave
requires zero heap allocations and the original owned resident/mapped bytes.

Raw tests preserve version1 behavior, enforce managed pins, reject collection
and double detach while live, and prove a stale TID cannot retire a new mapping
at the old address. Reduced permissions deny both detached creation and detach.
Foreign ownership fails. Normal exit and an actual isolated child data abort
cancel three live detached workers. Exactly three expected child faults occur;
all three parent lifecycles return to the cold physical-page baseline with no
futex waiters. Final direct and isolated UEFI profiles pass. All20 final guest profiles, the
full host suite and the configured ASan/UBSan suite pass. Regressions cover raw
threads, pthreads, C++20/23, TLS/emutls, synchronization, virtual memory, actual
WTF/JSC components, ICU, IPv4/IPv6 TLS1.3, typing and e1000e desktop/platform
checks. Exact inputs, results and retained investigations are recorded in the
[qualification manifest](validation/stage3-native-detach.json).

```sh
make native-sdk CC=clang LD=ld.lld -j4
python3 tools/build_native_detach.py --native-sdk build/native-sdk --out /tmp/chos-detach
python3 tests/qemu_native_detach.py --application /tmp/chos-detach/cmake/native_detach --out /tmp/chos-detach-direct
python3 tests/qemu_native_detach.py --mode uefi --application /tmp/chos-detach/cmake/native_detach --out /tmp/chos-detach-uefi
```

Use fresh output directories and synchronized kernel/ISO symbols. Forced process
termination reclaims owned resources without promising user TLS destructors.
Cancellation/unwinding, process-shared or robust mutexes, scheduling/affinity,
full POSIX and foreign GC suspension remain outside this bounded single-CPU
profile. The complete WTF thread adapter, engine-sized resources, VM/JavaScript/
WebAssembly execution, browser, native Pi5 Wi-Fi, production security and final
platform acceptance remain open. Development remains0.30.0; this does not qualify
new Fusion/Pi5/laptop hardware or complete Stage3/version0.50.0.

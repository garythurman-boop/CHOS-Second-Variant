# Native foreign-thread pause and real WTF GC coordination

The native scheduler and pinned WTF adapter now provide real same-process foreign
suspend, register capture and resume. The full public-C-API JavaScriptCore link
has no unresolved symbols. Its proposed50,663,424-byte image is still rejected
by the unchanged4MiB application arena; no VM/JavaScript/Intl/WebAssembly or
modern-browser execution follows from this component.

Services80(SUSPEND) and82(RESUME) use the existing REAP58 permission class.
Service81(PAUSED_CONTEXT) uses INFO57. They accept native TIDs bound to the caller's
complete address-space generation; no new positive-mask bit is consumed. Existing
current-thread service66 remains the original832-byte/version1 snapshot with
CURRENT|REGISTERS_VALID|STACK_VALID flags7. The foreign snapshot has the same
layout and SUSPENDED|REGISTERS_VALID|STACK_VALID flags14. There is no writable
register state, program-counter replacement, signal emulation or cross-process
thread access.

A task's pause bit is independent of its ordinary wait state. Timer deadlines and
futex wakes still make it ready; dispatch skips it until the final resume. An
exclusive controller per process owns nested pauses, bounded to32 per target.
Other controllers cannot seize or resume those leases. Invalid requests preserve
ownership. Controller thread exit releases its kernel leases. Process exit/fault
releases pauses before waking and terminating its workers. This does not unlock
arbitrary user mutexes or promise TLS/RAII destructors during forced termination.

The source is the actual saved EL0 timer frame, or the actual lower-EL entry frame
while its syscall is blocked in EL1. Borrowed frames must belong to the pinned
target kernel stack and have EL0 PSTATE. Kernel frames are never converted into
invented user registers. Lower-entry ownership ends with local IRQs masked through
ERET. Owned/pinned user stack bounds and PC/SP are checked before committing the
snapshot. Inconsistent live scheduler identities fail closed rather than being
reported as dead threads that GC may skip. These are single-CPU/preemptive native
contracts, not SMP or physical-platform qualification.

WTF's real ThreadSuspendLocker already supplies its original global lock. The
adapter uses an actual stable uint32 kernel TID stored during original thread
initialization, independent of the pthread TCB which detach/join can free. Actual
Thread::suspend/getRegisters/resume use native leases and full31GPR/32SIMD128-bit
state, FPCR/FPSR, PC and SP. Only a genuinely dead/nonexistent target may return
ESRCH to the original stack marker; other live failures abort safely, preventing
silent omission of a live stack. The optional sampling profiler stays OFF until
its separate protocol is qualified. Foreign signal delivery remains unavailable.

The raw and actual-WTF fixtures seed real heap pointers into x19..x28 and distinct
values into both halves of every SIMD register. They check snapshots and actual
restored registers after resume. Both busy EL0 and blocked futex-SVC states are
exercised: blocked coverage requires the exact post-SVC label, syscall arguments
and one queued waiter reported by wake. They verify nested pauses, stable counters
and contexts through wake events, main-thread capture, permission/foreign-process/
stale/other-controller refusals, invalid output preservation, and owner/process
exit/fault cleanup. The WTF fixture also retains its real Thread object after
native TCB disposal and checks stable-ID ESRCH, plus isolated fail-closed children
for self suspension, unheld reads and denied live suspension.

All24 final guest profiles pass, including the stronger direct/isolated-UEFI raw
and actual-WTF fixtures, prior raw/pthread/C++/TLS/startup/synchronization/memory/
WTF/JSC/math/ICU components, IPv4/IPv6 TLS1.3, typing and e1000e desktop/platform.
The full final host and configured sanitizer suites pass. Frozen complete SDK,
kernels/media/apps/source and all actual engine archive/object members are
recorded in the [qualification manifest](validation/stage3-native-foreign-gc.json). The clean original
39-file patch applies and all postimage hashes match; a missing native TID typedef
was corrected to the actual uint32 kernel identity before WTF runtime testing.

```sh
python3 tools/build_native_pause.py --native-sdk build/native-sdk --out /tmp/chos-pause
python3 tests/qemu_native_pause.py --application /tmp/chos-pause/cmake/native_pause \
  --out /tmp/chos-pause-direct
python3 tools/build_wtf_foreign_gc.py --source /path/to/pinned-webkit \
  --native-sdk build/native-sdk --engine-build /path/to/native-engine/cmake \
  --out /tmp/chos-wtf-gc
python3 tests/qemu_wtf_foreign_gc.py --mode uefi \
  --application /tmp/chos-wtf-gc/cmake/wtf_foreign_gc.elf --out /tmp/chos-wtf-gc-uefi
```

Use fresh output directories and synchronized SDK/kernel/ISO symbols. GC callers
must respect upstream's allocation/lock constraints while other threads are
stopped. A component does not qualify complete conservative collection or a live
VM. Engine-sized image/address layout, loader/CHFS, memory quotas/global reserve,
stacks and scheduling admission remain required;32KiB defaults still fail the
engine's64KiB GC reserve. Existing storage IRQ-save latency remains a separate
limit. WebCore/browser, native Pi5 Wi-Fi, production security and final independent
Fusion/Pi5/laptop acceptance remain open. Development stays0.30.0; Stage3 and
version0.50.0 remain incomplete.

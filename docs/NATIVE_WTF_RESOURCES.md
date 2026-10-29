# Native CPU and memory measurements for WTF

`RESOURCE_INFO` (67) returns an owned, versioned 96-byte snapshot for the calling
process and current thread. It shares the existing read-only `THREAD_INFO` (57)
capability; positive permission bits 1 through 62 and their meanings are unchanged.
No target PID, foreign thread, physical address, kernel pointer or root-table
identity is accepted or returned. The request requires the exact size and zero
flags. The entire output range must be writable before the kernel copies anything.
Bad sizes/flags, wrapping addresses, read-only pages and partially writable ranges
are refused. Reserved output fields are zero.

## CPU accounting

The single-CPU scheduler measures architectural-counter intervals at native C
entry, exception-return and dispatch boundaries. It separately accumulates user
and kernel ticks for each task and its generation-owned address-space aggregate.
CPU time measures active dispatch intervals, not elapsed wall time or the 100Hz
periodic tick count. The counter frequency is returned explicitly. Small assembly
entry/return and C observer costs belong to their adjacent measured intervals;
this is scheduling accounting, not an instruction-counting profiler. Hypervisor
steal time cannot be subtracted without a separate supported steal-time ABI.

Successful dispatch changes, voluntary yield, sleeping, interlocked futex waits,
task exit and kernel-root idle transitions stop/restart accounting. A blocked
thread's wait and other dispatched tasks' execution are excluded. Interrupt and
syscall handling while the private task remains active is charged as kernel time;
work executed by separate kernel-root service tasks is excluded. Consequently this
is not full attribution of every shared service's cost to clients.

Fixed kernel records retain a process's exited-thread totals after scheduler task
slots are reaped/reused. Full slot/generation/identity matching prevents a new
process from inheriting an old process's usage. Task pins already prevent an old
address-space identity from being reused while a thread may execute. All updates
run under the scheduler's local IRQ mask. The IRQ accounting helper allocates
nothing and takes no address-space, process, heap or service locks. Dispatch policy,
address-space switch decisions and the saved exception-frame ABI are unchanged.
SMP is not supported by this accounting profile.

Backward/wrapped active counters, malformed internal modes or either thread or
aggregate overflow invalidate CPU measurements. Later reads do not invent a value
or silently restore validity. A missing frequency or unscheduled legacy execution
also leaves the CPU-valid flag clear. Memory availability is independent. The
current process snapshot does not include child processes.

## Memory charge

The kernel reports owned user frames multiplied by 4096 as resident bytes and
counts mapped user pages separately. Eagerly allocated but inaccessible `PROT_NONE`
pages remain owned and charged. Protection changes and discard do not free their
frames; unmap/release does. Privileged page tables, kernel allocations and separate
service-task buffers are excluded. This definition is exact for the current eager,
non-swapping native profile; it is not a future shared-page/PSS or peak-RSS model.

## Actual engine integration

The checked JSCOnly configuration selects `CPUTimeCHOS.cpp` and
`MemoryFootprintCHOS.cpp` instead of the incompatible POSIX CPU and generic
Windows-memory assumptions. `CPUTime::get` returns no value when real accounting
is unavailable or denied. `forCurrentThread` and `memoryFootprint`, whose upstream
interfaces cannot represent unavailable data, fail closed. Counter division is
performed separately for user/system fields to avoid integer-sum overflow. No
wall-clock CPU substitute, getrusage stub, heap-only footprint or mutable cache is
used. Original upstream `CPUTime.cpp` computes usage percentages from the real
CPU measurements and monotonic elapsed denominator.

The native fixture links original CPUTime, CurrentTime, MonotonicTime and WallTime
units with the checked native adapters. It checks:

- Measurable CPU work, both user/kernel advancement, plausible usage percentages,
  and exclusion of a real one-second sleep from process/current-thread usage.
- Three cycles of three workers, independently sleeping/burning, retaining exited
  worker totals after join, with zero remaining native heap ownership.
- Independent child-process accounting after the parent accumulated CPU time;
  capability denial preserves the entire output and returns optional refusal.
- Exact memory growth, `PROT_NONE` charge, discard and release; buffer canaries,
  zero reserved fields, malformed lengths/flags, wrapping pointers, read-only and
  cross-page refusal without a partial copy.
- Three process lifecycles returning to the cold PMM baseline, active preemption
  and no queued futexes.

Pure host tests compare 200,000 independent alternating-owner interval sequences,
large blocked gaps, repeated timestamps, owner retention, bad modes, counter wrap
and per-thread/process overflow. Normal and ASan/UBSan runs pass.

```sh
source /workspace/chos-tools/activate
make native-sdk CC=clang LD=ld.lld -j4
python3 tools/build_wtf_resource.py --source "$WEBKIT_SOURCE" \
 --native-sdk build/native-sdk --engine-build "$JSC_BUILD/cmake" --out "$APP_OUT"
python3 tests/qemu_wtf_resource.py --mode direct \
 --application "$APP_OUT/cmake/wtf_resource" --out "$DIRECT_OUT"
python3 tests/qemu_wtf_resource.py --mode uefi \
 --application "$APP_OUT/cmake/wtf_resource" --out "$UEFI_OUT"
```

Direct and UEFI component qualification pass. This does not qualify complete WTF
linking/thread lifecycle/GC suspension, JavaScript execution, WebCore/rendering,
a modern browser, production TLS/time/trust policy or live Pi 5 Wi-Fi. The full
WTF build still stops on six filesystem/iostream compilation units. Those remain
implementation work, in addition to runtime and engine-sized resource limits.
New Fusion/Pi/laptop qualification is not inferred from these QEMU checks.

The qualification set includes14 accepted guest profiles: the final resource
fixture in both boot modes; current-thread register context; shared-AS threads in
both modes; C++ synchronization; native memory/ELF isolation; ICU resources;
Console/browser/editor typing, repeat and blinking; TLS1.3 over IPv4 and IPv6;
and direct/UEFI Ethernet, storage, USB and desktop stress. Full host and sanitizer
suites pass, with an additional final ABI/accounting normal/sanitizer check.

The initial typing run passed Console/browser repeat but timed out at the editor
press after a focus-cancel test. The fixture released then immediately repressed
Backspace, allowing physical USB report coalescing even though repeat state had
already been cleared by focus. It now waits for the old window's real press and
the normalized physical release across the focus transition. Product input code,
500ms/25Hz timing and all acceptance deadlines are unchanged. The rerun passes.
An initial TLS run timed out on a3-second process-diagnostic wait after12 completed
peer scenarios; its tail contains the expected diagnostic by teardown. The exact
timeout cause remains unestablished. A separate identical-input run passes the
original deadlines. Neither initial run is retroactively marked PASS. Both raw
traces, fixture build/command failures and the accepted evidence remain retained.

Machine-readable inputs/results: [stage3-wtf-resources.json](validation/stage3-wtf-resources.json).
The retained raw archive is 111345699bytes, SHA-256
`d8762c5f2a21465c26dc322271ff5cc8a05171be60485b8b2ead2587ddb4f5fa`. Every member's bytes, length
and mode were verified against the retained original tree. No new ISO is published.

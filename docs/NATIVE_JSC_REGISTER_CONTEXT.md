# Actual ARM64 register visibility and native profiler queries

The original JavaScriptCore ARM64 RegisterState macro now compiles under the
native profile without an unused setjmp header. Its x19..x28 inline capture is
unchanged. Other platforms retain their original header/fallback selection.
The native MachineContext stack-pointer accessor now reads the actual `sp` field
of the checked CHOS context structure, rather than a generic fallback field that
the structure does not have.

ProfilerSupport's thread-ID query now calls the real kernel thread-ID API,
requires a positive representable result and fails closed on errors. A pthread
handle or TLS pointer is not converted into a kernel ID. Its existing timestamp
method continues through the actual native monotonic-clock backend. Native
performance-log source excludes an unused fcntl include; its file-handle code is
retained. No fake POSIX header, logging-success stub or zero register snapshot is
introduced.

The component links the actual ProfilerSupport source and original WTF archive.
It executes the real RegisterState macro, MachineContext accessor and profiler
query methods. An AAPCS64 assembly harness puts ten distinct real heap pointers
in callee-saved registers. The capture must find every pointer in the original
register snapshot or the compiler's callee-save stack spills. The oracle vector
is outside the scanned stack interval; the RegisterState object itself is also
excluded from that spill scan. The raw scan occurs before the independent kernel
context query, preventing that second context from supplying the expected roots.
Salted, aligned heap addresses change between captures; their byte markers must
remain intact. These are implementation-specific native stack observations, not
portable ISO C++ stack introspection.

The kernel snapshot independently validates version, size, flags, thread identity
and actual current-stack bounds. The accessor must return its real SP. Profiler
IDs must equal both snapshot IDs and direct kernel IDs; native monotonic timestamps
must advance across a scheduled sleep. A root and nine worker lifecycles run per
process. All user allocations/maps and futex waiters must be released, and three
complete process lifecycles must return to the cold physical-page baseline.

Compilation caught and retained two fixture/adapter mistakes before execution:
a wrong API name and preprocessor-only OS/CPU macros used in static_assert. The
correct adapter uses `chos_thread_id`; platform checks use preprocessing. An
actual engine compilation also caught the generic/native SP field mismatch.
Exact failures and inputs remain in [the qualification](validation/stage3-jsc-register-context.json).

This qualifies own-current register visibility and selected profiler queries.
It does not qualify foreign-thread suspension, complete conservative GC, sampler
operation, WorkQueue initialization, performance-log construction or a full
JavaScriptCore link/runtime. The actual full target still records missing narrow
scanf support, a string declaration, queue headers and sampling-context access.
Those remaining services/configuration and an engine-sized resource profile are
separate work. Native Wi-Fi, browser rendering, production security and platform
acceptance also remain open. Development version stays0.30.0; no0.50.0 or new
Fusion/Pi5/laptop qualification is claimed.

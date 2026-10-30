# Original native chrono and linked containers

The native SDK now includes the original pinned LLVM19.1.7 `<chrono>`, `<list>`
and `<forward_list>` headers and their active private dependencies. The importer
verifies the release archive and existing originals, discovers the real native
preprocessor closure before mutation and records exact original hashes. No local
calendar, duration or linked-container class replaces the upstream implementation.
C++17, C++20 and C++23 include/instantiation probes use the actual native SDK.

Existing native system/steady-clock implementations remain in use. The new
`file_clock::now()` backend queries the same checked real-time service and returns
Unix-epoch nanoseconds in the original libc++ wide representation. The multiply
and addition fit that representation for every validated native time_t value.
Unavailable/denied clock queries use the existing no-exception termination path;
no zero timestamp is fabricated. This is a current-clock query. CHFS does not gain
stored creation/modification timestamps from it. Duration periods describe units,
not a new physical-clock resolution guarantee.

Python datetime/calendar generates512 independent Gregorian vectors covering
pre-epoch dates, leap boundaries, month ends and years1..9999. Native execution
checks date/day conversion, weekdays, day-of-year, last days and indexed weekdays.
Separate fixed checks cover year0 and negative-year leap rules. Original duration
casts, negative floor/ceil, tie-to-even rounding, hh_mm_ss and time_t conversions
execute alongside real wall/steady/file-clock queries and scheduled sleep. File
clock readings must lie between native before/after snapshots, and original
from_sys/to_sys conversions must roundtrip.

The component tests original list/forward_list sorting, merging, uniqueness,
splicing, moves/copies, node-address preservation and independent ordered/sum
checks. Three waves of three native workers repeat calendar/container operations.
Each wave must leave zero live user allocations/mapped heap bytes. Two children
then lose either MAP35 or CLOCK_INFO48: attempted container allocation and file
clock query must terminate with exact abort134 while the parent continues. Three
complete application lifecycles must return to the cold physical-page baseline
with no pending futex waiter. These are explicit no-exception failure semantics,
not recoverable C++ exceptions.

The broader run exposed two stale thread-fixture assertions: CPU discovery has
reported one logical CPU since08b8342d, while these fixtures still expected the
older unknown-count value0. The assertion now requires the real value1. This
fixture-only rebuild must leave SDK, kernel and ISO hashes unchanged. An initial
synchronization run also timed out on its second launch; its original trace is
retained. Follow-up diagnostics preserve live task/futex/thread state on any new
failure without extending acceptance deadlines. A later passing run does not
establish the original stall's cause; that investigation remains explicit in the
qualification rather than being called a synchronization fix.

The actual JavaScriptCore build now gets through the original `<chrono>`/`<list>`
includes and builds its precompiled header. Its next recorded blockers are
`fcntl.h` in PerfLog and an unconditional `setjmp.h` include in GC register-state
handling. The actual ARM64 branch already captures registers with inline assembly;
its platform-header selection needs correction while preserving that capture.
Foreign-thread suspension and complete GC coordination remain separate services.
A fake header or zero register snapshot would not satisfy these requirements. No full JavaScriptCore link, JavaScript/WebAssembly execution
or browser rendering is established by this component.

Timezone database, localized chrono formatting/parsing, the complete standard
filesystem, full hosted C++ and full engine/runtime acceptance remain outside
this bounded profile. The existing no-localization/no-timezone/no-filesystem
configuration is unchanged. Tests and exact build inputs are recorded in
[the qualification](validation/stage3-native-chrono.json). Run the fixtures only
on disposable test disks. No new Fusion/Pi5/laptop qualification or0.50.0 release
is claimed; all revised Stage3 gates remain required.

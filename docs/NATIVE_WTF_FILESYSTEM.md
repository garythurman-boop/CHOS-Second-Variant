# Linked native WTF filesystem and current-thread adoption

The actual pinned WTF target now builds its complete static archive with the
native CHOS backend selected. A native executable links that archive and executes
its original String, Vector, path, file, directory, entropy and synchronization
code. No replacement String/Vector implementation or host filesystem API stands
in for it. This is selected library component execution, not complete WTF runtime,
JavaScriptCore, JavaScript, WebKit rendering or browser acceptance.

The new native filesystem backend opens real owned snapshots/working copies,
queries actual metadata, copies and moves files, reads complete restartable
directory pages, and performs bounded recursive traversal/removal. Directory
collection retries changed tokens at most three times and presents callbacks only
after collecting a consistent complete view. It returns failures without claiming
a truncated list is complete. Actual source identifiers distinguish CHFS and ROMFS;
no Unix inode, timestamp or host device is fabricated. Where a CHFS mount identity
fits the upstream32-bit volume-comparison interface, it is used as a runtime volume
identifier. The actual CHFS allocation unit is512 bytes.

The common upstream `readAll` and512KiB-buffer `appendFileContents` paths now execute.
Native transfer joining remains necessary because append interprets a short read
as EOF. Truncate handles locally enforce write-only behavior across moves; this
is an adapter mode over the owned native working-copy service, not a new kernel
capability. Unsupported lock requests fail before creation/truncation. Common
salt and whole-file-overwrite helpers require explicit successful flush before
reporting durable completion. Copy-overwrite and exclusive copy fallback use
different native create policies, preserving existing destinations when an
exclusive copy is requested. Failed writes/flushes are not represented as success.

Temporary names obtain actual OS entropy and use exclusive creation. Prefix and
suffix validation rejects directory traversal and unsupported characters before
creating a file. Length bounds are checked before making directory prefixes.
No predictable time/PID/random fallback or host entropy is used. The component
checks that stored salt is reused, and that temporary files/directories can be
created, written, flushed and removed through the real backend.

Native descriptor helpers validate actual owned file handles. Spawn creates an
empty child table, so close-on-spawn is the existing mandatory policy. Inheritance
and duplication cannot be enabled and fail explicitly. Native regular-file reads
and writes access already-owned memory; their nonblocking helper does not pretend
to implement Unix socket readiness or fcntl flags. Open and explicit flush can
still perform storage I/O. The single-user CHFS profile does not implement Unix
UID/mode permissions; its protected paths and native capability checks still apply.

## Current-thread service and a corrected capability declaration

Actual current-thread adoption allocates an upstream Thread, retains the native
pthread handle, obtains its checked current stack bounds from the kernel, and
owns its reference through native pthread TLS. The original two-pass destructor
algorithm releases client data, unregisters the exiting thread and releases the
retained reference. WordLock yielding uses the real scheduled YIELD syscall.
Thread creation/join/detach through WTF and foreign-thread GC suspension are still
separate, unimplemented platform work; std::thread in this fixture uses the
previously qualified native C/C++ thread runtime.

The strengthened registry test exposed that the generic upstream platform header
advertised stack-bound discovery for a thread that had not started. CHOS only
provides a current-thread snapshot. That incorrect capability flag suppressed
registration from within adopted workers. The CHOS predicate now disables that
advertisement; each worker discovers/registers its actual own bounds. The fixture
retains its registry assertion, verifies root plus three live workers, and verifies
only the root remains after joins. Three worker waves test unique IDs, actual
stack membership, AtomString tables, directory access and stable warm heap use.
The original failed registry run and exact inputs are retained.
The native root retains three live allocations in a64KiB arena before CRT exit
in the final samples; worker waves return to the same observation and complete
process cleanup returns to the cold physical-page baseline.

Unix signal initialization is explicitly excluded on CHOS; user faults remain
owned by the kernel's process exception boundary. No signal handler initialization
stub claims success. Config freezing now attempts the real native protection API
instead of taking the upstream generic no-op branch. The current kernel protects
anonymous mappings only, so static ELF config-page protection is unavailable and
is explicitly refused by the fixture. Full engine config hardening, trap services
and GC suspension are not qualified by this component.

## Deployment and qualification

The first linked ELF was5.61MiB, dominated by debug sections. The builder preserves
it and produces a deployment ELF with only debug sections removed. It verifies
identical entry, loaded-segment addresses/permissions/bytes and ABI notes, with no
W+X segment. Actual allocated image size is about0.5MiB and remains within the
existing native profile. No loader or CHFS limit was bypassed. The builder checks
the source pin, every maintained patch/port hash, actual CHOS configuration and
SDK, then builds the real WTF target before linking the fixture.

The native component checks real dynamic strings/vectors and UTF conversion,
lexical paths,32KiB-plus reads/writes, readAll,512KiB append, explicit persistence,
copy/exclusive-copy/move, a70-entry paginated listing, recursion, temporary paths,
salt, mode/error handling and current-thread ownership. Process-lifetime root
objects may remain before CRT exit; worker waves must return to the same warm
heap observation, and whole-process reaping must restore cold physical pages with
no futex waiters. One IPv4 TLS regression timed out waiting for a process-diagnostic response
after a malformed-peer rejection; the serial command was echoed without its
response before teardown. The same profile/deadlines passed on two separate runs
with identical kernel/ISO artifacts. Its precise initial stall cause remains
unresolved and the failed trace is retained. The fixture's deliberate unavailable-config-protection check is
not a successful protection test. Ten accepted guest profiles and the full host/sanitizer suites qualify this
component, with the additional independent TLS confirmation retained separately.
Exact final results and retained investigations
are in [the validation record](validation/stage3-wtf-filesystem.json).

## Remaining limits

CHFS still has47-character ASCII paths,4MiB files, four descriptors, an8MiB owned
file-buffer budget, and whole-file I/O locking. Writable upstream opens currently
reserve4MiB, so only two such maximum-capacity handles fit simultaneously. Unicode
and percent-encoded engine filenames need further filesystem work. Recursive native
helpers are bounded to32 levels. Native move does not atomically replace an existing
destination. Stored timestamps, advisory locks, coherent shared file mappings,
symlinks/hardlinks, an exported capacity/free-space service, duplication and
inheritance remain unavailable and return explicit failures. These are material
engine persistence/runtime gaps, not full filesystem conformance.

The native image, stack, anonymous-memory and process/thread profiles remain too
small for the complete engine. Full Thread/GC, JavaScript execution, browser
rendering/network/application integration, production security policy, native
Pi5 wireless and revised platform acceptance remain open. New Fusion/Pi/laptop
qualification is NOT RUN. Stage3 remains incomplete and version remains0.30.0.

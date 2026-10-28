# Revised Stage 3 current evidence

Current publication policy: the user now authorizes interim binaries for critical
verified milestones suitable for Fusion testing. This supersedes historical hold
statements below. Version 0.50.0 still requires full Stage 3 acceptance.
See [native read streams and Fusion checklist](NATIVE_READ_STREAMS.md).

The latest [native virtual-memory milestone](NATIVE_VIRTUAL_MEMORY.md) passes
20 live QEMU profiles across direct/UEFI boot, plus full host/sanitizer suites.
It adds real anonymous protection/discard/partial unmap and runs upstream WebKit
VM helpers through the native SDK. Empty kernel heap arenas now return to PMM;
ICU's strict page equality passes. Existing1MiB/4MiB/8MiB budgets remain bounded.
Actual JavaScriptCore configuration succeeds; compilation next stops at fcntl.h
and stderr stream APIs. No engine execution or new target-hardware qualification
is claimed. This is source-only; the test ISO remains cc29ea8/version0.30.0.

Stage 3 is **not complete**. The revised prompt's modern browser, integrated
production TLS policy, complete isolation/runtime and real Pi 5 wireless
requirements remain active. Native TLS certificate validation has separate
component evidence below; it is not the integrated browser deliverable.
The earlier machine-readable [results](validation/stage3-input-ipv6.json) preserve
that component baseline and open gates and distinguish target reports from automated guest evidence.

The user confirms Fusion Ethernet and example.com after the prior 54b9731 build.
Typing build **d91d135** is published with held Backspace/characters/navigation,
500 ms initial delay, 25 Hz repeats and focus-aware blinking carets. All ten
published files were downloaded back and checksum-verified. Nine clean-build
guest profiles, full host/sanitizer suites and direct smoke passed for that build.

The subsequent IPv6 source iteration compiles a pinned, unmodified lwIP 2.2.1
dependency closure alongside the retained IPv4 stack. It passes actual QEMU
DAD/SLAAC/router/neighbor/echo traffic on Intel 82574L, VMXNET3 and PCI VirtIO,
plus direct VirtIO. Link loss invalidates state and reconnect repeats DAD/SLAAC.
IPv4 DHCP/echo and DNS/HTTP pass on each supported backend. This is IPv6 control
plane evidence, not IPv6 web browsing, DNS6/DHCPv6 or public Internet validation.

Fourteen accepted guest profiles also cover Network Manager configuration/save/
rollback/reconnect, keyboard/pointer typing, real EL0 IPv4/IPv6 status and IPv6
echo, protected profiles, guarded allocation/fault/reaping, original platform
behavior and graphical install/disk-only boot/persistence/reset/power-off.
Installer completion was 20.926 s, its largest measured step 18.508 ms and actual
shutdown 5.035 s in this shared-cloud sample. Memory returned to the same 117,999
free physical pages after normal, faulted and scheduled applications. These are
component observations, not modern browser benchmarks or Fusion certification.

For that earlier IPv6 control-plane baseline, compiled kernel source is **54bc0f5**. The final fixture commit **e1bb871** changes
only typing-test synchronization; kernel, headers, userland, dependency and build
inputs are identical. One parallel typing run sampled the browser before its last
queued character was consumed. Its failure is retained, and the corrected test
waits for all 36 characters before measuring held-key deletion; that rerun passes.
Full host/sanitizer and direct smoke checks pass on the clean compiled source.

That IPv6 follow-up was source-only under the then-current alternating artifact cadence.
The latest downloadable ISO remains d91d135 and contains the typing fixes; it does
not contain this later IPv6 implementation. See [IPv6 scope](IPV6_CONTROL_PLANE.md),
[typing behavior](TEXT_INPUT.md) and [Pi 5 wireless evidence](PI5_WIRELESS.md).

The user reports the tested build works without significant issues. Plain I
opening Setup was identified as an unwanted Welcome-view hotkey and removed.
This target report does not cover source-only IPv6 or future untested features.
New build publication is now held until every revised Stage3 gate is met and
thoroughly tested; this supersedes the earlier alternating binary cadence.

Native executable runtime progress is now documented in NATIVE_EXECUTABLES.md.
Actual direct-boot ELF applications exercise sparse RX/R/RW mappings,256KiB BSS,
arguments and user-only faults with complete reaping. Modern engine/runtime,
verified TLS, real Pi5 Wi-Fi and the other full-stage gates remain open.

See validation/stage3-native-runtime.json for local native-runtime evidence and
retained failures. Sparse-page accounting and the obsolete installer fixture are
corrected. One parallel UEFI typing run timed out at the firmware banner before
kernel entry; the standalone rerun passes, but that original failure remains
NOT PASS and its exact pre-kernel cause is unresolved. Full qualification is not
claimed. Release-hold tests reject forced publication; all binaries remain held.

## Subsequent runtime and transport source work

The UEFI CPU/process profile now runs actual isolated applications in QEMU,
including deliberate RO, XN, sparse-hole and kernel-text access faults, timer
scheduling and complete deferred frame cleanup. The high-address fixture boots
the same kernel above 2 GiB and retains native high-BAR USB. See
[UEFI_PROCESS_RUNTIME.md](UEFI_PROCESS_RUNTIME.md). Identity DMA with data caching
off remains an explicit narrow profile; native IOMMU ownership is still absent.
Fusion must independently validate this new runtime source.

A real [native C allocation/string layer](NATIVE_C_RUNTIME.md) runs at EL0 using
owned MAP/UNMAP pages, including alignment/overflow/failure behavior and exhaustion
without corrupting live allocations. It is not full libc/C++/POSIX. Native text
editing now preserves held Backspace, caret blinking and durable bounded saves;
ROMFS/binary/oversized previews stay read-only. The HTTP recovery browser batches
bounded input events so key-up/motion traffic cannot require a full redraw for
every queued event. It remains an HTTP preview, with HTTPS blocked.

[IPv6 transport results](validation/stage3-ipv6-transport.json) add actual TCP6
streams and AAAA over UDP6 with RDNSS lifetime/withdrawal, hostile-response rejection,
process ownership, bounded retries and fail-closed entropy absence. This does not
make the recovery browser IPv6-capable or implement DHCPv6/manual IPv6 settings.

[SDIO/CYW protocol implementation](SDIO_CYW_PROTOCOL.md) adds real bounded command,
identity, backplane, framing and NVRAM primitives. These have host/sanitizer and
AArch64 compilation evidence. Pi 5 host-controller/regulator/pin/clock ownership,
firmware startup, radio authentication and actual scan/connect remain unimplemented;
no live wireless support is claimed.

Known regressions exposed by UEFI applications are retained with their fixes:
standalone Text Viewer lacked editing; browser redrew too often and lost queued
input; native Console's added state-name pointer table was invalid for relocation-
free CHCX and faulted only that application. The table was replaced by inline
characters. The established privileged UEFI desktop Console is preserved for
pci/driver/clock/bootlog diagnostics; the limited standalone Console remains an
explicit isolated application. No administrative network setter was added to EL0.

Test inputs now verify that symbols match the kernel actually embedded in each ISO
or installed ESP before launching QEMU. Earlier network fixture failures caused by
a mismatched copied kernel are not Ethernet failures or accepted evidence. Corrected
matched tests verify actual DHCP/DNS/HTTP traffic through isolated browser windows.

## Mandatory gates still open

* Integrate a maintained genuine modern engine and demonstrate HTML/CSS/JavaScript,
  browser features and conformance in CHOS. No engine currently renders in CHOS.
* Complete engine-sized memory/runtime, C++ standard library, POSIX/event/loader
  APIs and web-content permission/isolation boundaries. Bounded native pthreads,
  futexes, messages and compiler-emulated TLS now have component evidence. Current
  static ELF and tiny process budgets are not a browser-ready runtime.
* Complete browser integration of the tested native TLS1.2/1.3 transport,
  production trust roots/update/time policy, reseeding and reliable entropy on
  Fusion. Native certificate/hostname/date/failure fixtures now pass; they do not
  complete that production/browser gate.
* Implement actual Pi 5 radio/firmware/authentication, scanning, connection manager,
  secure credentials and live AP/reconnect/failure tests.
* Finish IPv6 browser integration, DHCPv6 where required, manual IPv6 settings,
  multiple-interface policy and the complete network settings/service requirements.
* Run engine conformance/security/live-site tests, measured browser responsiveness
  and resource-pressure benchmarks, and independent Fusion/Pi/laptop qualification
  where those environments are available.

None of these software gaps is closed merely by unavailable hardware. The binary
publication hold remains active. Existing d91d135 downloads remain unchanged.

The [consolidated local runtime record](validation/stage3-uefi-runtime.json)
includes exact input manifests, matched binaries, accepted component profiles and
retained failures. It explicitly leaves full Stage 3 and binary release incomplete.

A further [native clock prerequisite](NATIVE_CLOCK_API.md) exposes bounded
read-only monotonic/UTC status and actual C/C++ time conversions. It does not
close authenticated-clock, secure entropy, TLS or modern-engine gates.

The user reserves **0.50.0** for full revised Stage3 completion. A subsequent
[firmware entropy prerequisite](FIRMWARE_ENTROPY.md) has QEMU/host evidence, but
Fusion availability, live reseeding/lifetime policy, TLS and the other mandatory
gates remain open. Neither the version nor binary hold is advanced for it.

[Bounded process messages](PROCESS_IPC.md) now have actual isolated parent/child/
foreign-process evidence on direct and UEFI QEMU, plus cleanup and ownership
checks. They are not POSIX sockets/shared memory/descriptor passing, and do not
close the modern browser, complete runtime or content-sandbox gates.

[Inherited syscall permissions](PROCESS_PERMISSIONS.md) now constrain children
before first execution, and ordinary child launches cannot restore omitted
rights. Actual nested EL0 tests deny filesystem/network/GUI/entropy access.
Browser roles, site/path policy and complete content sandbox integration remain
open, along with the engine, TLS, wireless and other mandatory gates.

[NATIVE_FLOATING_POINT.md](NATIVE_FLOATING_POINT.md) records full base FP/SIMD
exception state, fresh-process zeroing and live concurrent EL0 arithmetic/context
tests in direct and UEFI QEMU. This closes another runtime prerequisite, while
engine-sized memory, threads/POSIX, actual engine integration, TLS and live Wi-Fi
remain open. Binary publication remains held.

[NATIVE_TLS_CLIENT.md](NATIVE_TLS_CLIENT.md) now records an actual cross-compiled,
isolated TLS1.2 client using the pinned MbedTLS dependency. Local direct/UEFI
certificate/hostname/purpose/date, SNI/ALPN, four ciphersuites, truncation, timeout,
exhaustion/cancellation and missing clock/entropy fixtures are implemented.
This is not browser integration, TLS1.3, a production CA trust/update policy or
authenticated time. WPE/Soup integration, the modern engine and live Pi5 wireless
remain active gates. Recovery-preview HTTPS remains blocked and binaries held.

The subsequent native TLS protocol iteration adds reviewed upstream TLS1.3/PSA
and certificate-authenticated ephemeral exchange. Both TLS1.3 AES-GCM suites,
TLS1.2-only peers, intermediate chains/invalid issuers and overlapping client
lifetimes have real QEMU fixtures. See [current TLS protocol evidence](validation/stage3-native-tls13.json).
The earlier TLS1.2-only description is historical. Production CA/time policy,
engine/Soup integration, full browser behavior, live wireless and independent
Fusion qualification remain open. No recovery-preview HTTPS or binary release
is enabled by this source milestone.

The native secure transport also has an IPv6 constructor over the existing
owner-bound TCP6 service. Shared certificate policy, named SNI, bounded deferred
connect, mixed IPv4/IPv6 contexts and actual IPv6 wire frames are tested by the
fixtures in [the IPv6 TLS report](validation/stage3-native-tls6.json).
Browser DNS/URL integration, IPv6 settings/DHCPv6, production trust policy, full
engine/runtime and live wireless remain incomplete. No binary is published.

[Native shared-address-space threads](NATIVE_THREADS.md) now provide real bounded
EL0 workers, private saved TPIDR_EL0 state, stack lifetime pins, join/reap and
process-wide exit/fault cancellation. This is not complete pthread/futex, compiler
TLS or a thread-safe C/C++ runtime. The modern engine and other Stage3 gates
remain open; binary publication remains held.

[Private futex wait/wake](PRIVATE_FUTEX.md) now supplies an interlocked compare/
queue/sleep transaction for shared-AS workers, bounded notification/timeouts and
process cancellation. This is a real native synchronization primitive; pthread,
compiler TLS, thread-safe C/C++/TLS libraries, full engine integration and the
remaining internet/wireless gates stay open. Binary publication remains held.

The [native C thread runtime](NATIVE_PTHREAD_RUNTIME.md) adds actual bounded
pthread creation/join, private mutex/condition/once operations, generation-tagged
thread-specific keys, per-thread errno and serialized allocation. Direct and UEFI
EL0 runs exercise real contention, timeout, destructor, capacity rollback and main
thread exit/join with stable warm pages and no remaining futex records. Host and
sanitizer regressions plus native TLS/permissions/allocator/thread tests pass; see
[exact evidence](validation/stage3-native-pthread.json). This supersedes the earlier
allocator/errno/pthread prerequisite status. Complete POSIX/C++/compiler TLS,
engine-sized memory/events/loader, thread-safe TLS library integration, actual
modern engine and remaining network/wireless gates stay open. New Fusion/Pi/laptop
qualification is NOT RUN; binary publication remains held.

The [native TLS client](NATIVE_TLS_CLIENT.md) now has a separate pthread concurrency
contract: complete bounded API calls serialize shared PSA lifetimes and the external
RNG accepts only its current owning thread. Actual three-worker TLS1.3/direct/UEFI/
IPv6 and TLS1.2 runs validate independent clients, wrong-host rejection while valid
workers continue, GUI response during withheld replies and stable cleanup. Missing
clock/entropy still opens no connection. See [exact evidence](validation/stage3-threaded-tls.json).
Object lifetime/pending-write ownership and unrelated PSA consumers remain explicit
boundaries. This supersedes the earlier single-thread-only TLS status; production
trust/time/reseed policy, modern engine/runtime and live wireless remain open.
The binary hold and reserved completion version0.50.0 remain unchanged.

The [compiler-emulated TLS runtime](COMPILER_TLS_RUNTIME.md) now runs real Clang
C11/C++17 thread-local variables and C++ thread-local constructors/destructors on
native pthreads. Pinned unmodified LLVM compiler-rt uses checked native adapters;
compiler storage survives all four ordinary key-destructor passes and is reclaimed
after join. Direct/UEFI tests exercise48 controls, alignment, isolation, main return/
exit and bounded quota/cleanup failures. The initial weak startup regression was
fixed, then host/sanitizer, raw-thread, pthread, HTTPS and typing regressions passed
on corrected artifacts. See [exact evidence and retained failures](validation/stage3-compiler-tls.json).
This is an explicit -femulated-tls static build profile, not PT_TLS/dynamic loading,
a complete C++ standard library or a modern engine. Engine-sized runtime, browser,
production trust/time and live wireless gates remain open; binaries remain held.

[Base C library work](BASE_C_LIBRARY.md) now addresses concrete libc++ prerequisites:
real numeric conversion, sorting/search and allocation-free per-thread UTF-8/16/32
state. Host differential/exhaustive scalar tests and actual direct/UEFI thread
tests pass alongside compiler-TLS/HTTPS regressions. See
[exact evidence](validation/stage3-base-c.json). This does not complete libc++,
the modern engine, production trust policy or wireless; binary publication is held.

The [native math profile](NATIVE_MATH.md) now builds pinned original musl float/
double and actual ARM64 fenv code.889 high-precision scalar fixtures, special-value
checks, host sanitizers and direct/UEFI FP-environment tests pass. Native pthreads
inherit creator controls/status while raw kernel workers retain fresh vector state.
Legacy FP isolation, pthread and verified IPv6 TLS regressions pass; see
[exact evidence](validation/stage3-native-math.json). Binary128/nexttoward, complete
C++/POSIX and engine-sized runtime remain open, alongside the actual browser,
production trust policy and wireless gates. The binary publication hold remains active.

The [native libc++ subset](NATIVE_CXX_SUBSET.md) now runs actual array/vector/string,
RAII, allocation/deallocation and atomic operations on three native pthread workers.
Pinned unchanged LLVM code and real CHOS filename APIs link without host libraries.
Direct/UEFI guest tests verify normal operation, OOM/bounds process isolation,
clean relaunch and stable warm pages; aggregate host/sanitizer and threaded IPv6
TLS regressions pass. See [exact evidence](validation/stage3-native-cxx.json).
This is a static no-exceptions/no-RTTI container profile, not a complete C++ ABI,
std::thread/mutex runtime or modern engine. Engine-sized memory/events/POSIX,
production trust policy, actual browser integration and real wireless remain
open; binary publication remains held.

The subsequent [native static-guard runtime](NATIVE_STATIC_GUARDS.md) supplies
real compiler-emitted C++ local-static initialization, acquire/release publication,
private-futex contention and retry after initializer pthread_exit, including exit
inside key-destructor execution. Direct/UEFI guest tests cover competing workers,
16-level nesting, recursive/quota refusal and stable cleanup; host/sanitizer,
compiler-TLS, C++ containers and verified IPv6 TLS regressions pass. See
[exact evidence](validation/stage3-static-guards.json). This supersedes the earlier
missing-local-guards status. Global constructor/atexit, complete C++/POSIX and
engine-sized memory/events/loader, actual browser, production trust and wireless
requirements remain open. Binary publication remains held.

[Actual JavaScriptCore upstream-source bring-up](JAVASCRIPTCORE_BRINGUP.md) now
reproduces configuration against inspected WebKit2.54.2 source with an explicit
CHOS identity and isolated target-library searches. Configuration is INCOMPLETE:
native ICU data/i18n/uc are absent and further POSIX/C++/loader work remains.
Six original headers extend the closure to C++23; native C++23 container tests
and the exact upstream integer128 type/alignment compile probe now pass. Compiler-only CMake thread/library
fallbacks are explicitly not accepted as link evidence. No engine binary,
JavaScript execution, rendering or browser-conformance result is claimed; see
[the retained nonzero result](validation/stage3-jsc-bringup.json). The binary hold
and all modern-browser/wireless acceptance gates remain active.

The [native static CRT](NATIVE_STATIC_CRT.md) now runs global constructors and
real process cleanup,including last-pthread termination and nontrivial local
statics. It fixes C++ TLS destruction after key callbacks and retains compiler
TLS through global teardown. Direct/UEFI lifecycle,host/sanitizer and runtime/
network/input regressions are recorded in
[the evidence](validation/stage3-static-crt.json). This closes that bounded static
startup/atexit gap; full C++/POSIX and engine integration remain open.
An [actual ICU79.1 source probe](dependencies/icu-bringup.json) identifies native
condition_variable/mutex/call_once as concrete next dependencies. No ICU library,
modern engine or wireless runtime support is claimed. Binary publication stays held.

The [native C++ synchronization profile](NATIVE_CXX_SYNCHRONIZATION.md) adds actual
mutex/recursive/timed locks, condition variables, monotonic timed waits, 64-bit
call_once with owner-exit retry, and shared/weak ownership. It also runs original
ICU79.1 mutex/init-once code as a bounded component with a test-only cleanup
recorder. This supersedes the missing C++ mutex/CV/once runtime status; it does
not supply full ICU common/i18n/data, std::thread, JavaScriptCore or web rendering.
All remaining full-stage acceptance gates and the binary hold stay active.

The [native C++ thread profile](NATIVE_CXX_THREADS.md) adds original libc++
std::thread move/join/ID/sleep and notify_all_at_thread_exit over native workers,
with ordered post-key TLS cleanup and real termination handlers. It preserves the
three-worker budget and no-exceptions boundary; detached threads, public futures,
experimental jthread and complete engine runtime remain open. This supersedes
previously missing std::thread status only for the documented bounded profile.

The [native external-build SDK](NATIVE_SDK.md) now links ordinary CMake C/C++ main
programs against the actual CRT/static runtime and executes them in CHOS. Missing
libraries/imports fail real links; host libraries are excluded. The native JSC
configuration still requires target ICU data/i18n/uc and does not build/run the
engine. The earlier static-only probe limitations remain attached to that older
triage profile, rather than the new executable-link mode.

[Native RTTI and C byte/intmax APIs](NATIVE_RTTI.md) now have direct/isolated-UEFI
runtime evidence, including original LLVM dynamic_cast/type_info, concurrent
workers, terminating bad casts and clean relaunch. Actual ICU common syntax
inventory improves from120/202 to194/202 with the native SDK and opt-in RTTI.
This is not linked ICU/data/i18n or an engine. Exceptions, C streams/formatting,
wide/locale/floating parsing and larger runtime/security/platform gates remain
open; see validation/stage3-native-rtti.json. Binary publication stays held.

[Native binary128 compiler helpers](NATIVE_BINARY128.md) now link through the SDK
and pass exact-rational arithmetic/conversion and per-worker addition-rounding
fixtures in direct/isolated UEFI QEMU. Those tests found an upstream19.1.7 multiply
carry defect; the pinned original20.1.8 helper header fixes it without weakening
the oracle. Complete dynamic-FENV semantics, floating parsing/formatting and full
ICU/engine integration remain open. See validation/stage3-native-binary128.json.

[Native floating conversion](NATIVE_FLOAT_PARSING.md) now runs original musl
strtof/strtod/strtold/atof over bounded memory strings, with exact-ratio target
fixtures and host/sanitizer differential checks. The SDK also supplies the small
long-double math closure and real nexttoward behavior these dependencies need.
ICU common syntax inventory is195/202; full ICU/data/i18n and the actual modern
engine, production security and Pi5 radio gates remain open. See
validation/stage3-native-floatparse.json. No new binaries are published.

[Native ranges/span and complex arithmetic](NATIVE_RANGES_COMPLEX.md) add actual
C++20/23 view/algorithm and numerical guest tests, with original LLVM compiler
helpers and independent high-precision references. Actual native object generation
now succeeds for197/202 original ICU common units (previously195 syntax successes).
This remains distinct from full ICU/data/i18n linking and engine integration.
See validation/stage3-native-ranges-complex.json; all full-stage gates and the
binary/version hold remain active.

[Native wide strings and UTF8](NATIVE_WIDE_STRINGS.md) add real std::wstring,
original wide memory/string algorithms, bounded bulk conversion and independent
thread-local conversion states. Host exhaustive-scalar/sanitizer and direct/UEFI
runtime checks pass, with std::thread/compiler-TLS regressions. Wide formatted/
numeric I/O stays unsupported and fails real link probes. Actual ICU common
object generation reaches200/202; file/time platform sources, full data/i18n/
link/runtime and the modern engine remain open. See validation/stage3-native-wide.json.

## Native read streams and owned file positions

Real read-only buffered C streams now link through the native SDK and pass native
direct and isolated UEFI execution. SEEK63 uses READ permission without consuming
the signed mask's sign bit. Ownership, reduced permissions, snapshot replacement,
seek overflow, EOF, read-ahead, three concurrent std::thread readers, cleanup and
stable warm physical frames have guest evidence. The ISO embeds an explicit
`/BIN/STDIO.ELF` diagnostic for Fusion user testing. It also passes a launch through
actual USB input in the desktop Console. Full host and ASan/UBSan suites pass;
UEFI e1000e desktop/storage/input, VMXNET3/VirtIO dual-stack, reduced capabilities
and actual native typing/caret/persistence regressions pass. Original ICU common
source code generation is now **201/202** through the same SDK with the real
MAP_STDIO backend; putil and full ICU/data/i18n/runtime remain unfinished.
See [scope and Fusion checklist](NATIVE_READ_STREAMS.md) and
[machine-readable evidence](validation/stage3-native-stdio.json). An interim
runtime qualification binary is authorized; it is not Stage 3 completion.

Interim user-test build **cc29ea8** is prepared from a clean committed source.
Clean UEFI ROMFS runtime/desktop Console/typing and installed-image regressions
pass; see [exact clean input hashes](validation/stage3-native-stdio-clean.json)
and the [Fusion checklist](../artifacts/cc29ea8/TESTING.md). New Fusion validation
is pending. This supersedes d91d135 as the current test download after publication.

## Genuine native ICU common runtime

The [native ICU common port](ICU_COMMON_NATIVE.md) now builds all **202/202**
original common units into libicuuc.a and executes original Unicode conversion,
case folding/properties, UnicodeString, file loading, threading, clock failure
and actual cleanup paths in direct and isolated UEFI. The independent corpus
contains **1,530 Unicode15 full case-fold mappings**. Native SDK/CMake executable
links and UEFI execution pass; full host and ASan/UBSan suites and retained
read-stream/TLS/no-clock regressions pass. The separate original bootstrap data
archive remains explicitly empty and is never substituted for a full data library.
The real JSCOnly preflight now finds uc/version79.1 and retains its nonzero
failure for **data and i18n**. Actual next-step i18n code generation passes
210/254 sources; gaps are snprintf, signal.h, cstdarg and map. See
[exact evidence and failures](validation/stage3-icu-common.json). Modern engine,
complete data/i18n, production browser security and Pi radio remain unfinished.

The cc29ea8 interim ISO/source/image/kernel files and Fusion checklist are now
published with tag build-cc29ea8. All ten GitHub files were downloaded back and
matched local SHA256SUMS; evidence is retained in
/workspace/chos-evidence/native-stdio-github-downloads.json. This new ICU source
work does not replace that published test build or claim Stage3/version0.50.0.

## Ordered containers and narrowed i18n gaps

Original map/set/tree/cstdarg headers now qualify under native C++20 direct and
SDK C++23 isolated UEFI. Model-based tree operations, extraction/collision/merge,
C++23 ranges, mixed variadic arguments including binary128, concurrent independent
containers, heap0 and stable repeated-process frames pass. Full host regression
passes. [Evidence and limits](validation/stage3-native-ordered.json) distinguish
the C++20 cases from the extra C++23 API calls.

A checked native decimal header removes only an unused signal.h include; ICU
already disables its signal-raising code. No signal service is invented. Actual
i18n code generation now passes **251/254** units; choicfmt.cpp,
number_decimalquantity.cpp and plurrule.cpp still require a real snprintf. This
is object-level dependency evidence, not a complete i18n/data library or engine.
The [container profile](NATIVE_ORDERED_CONTAINERS.md) records the method. The
cc29ea8 test binaries remain the current download; version0.50.0 stays reserved.

## Native bounded string formatter

Real snprintf/vsnprintf now link through the SDK and pass native direct and
isolated UEFI execution. Original musl formatting uses a private per-invocation
memory sink; public read-stream FILE and unsupported output APIs are unchanged.
A checked source copy rejects signed INT_MIN width overflow and positional slot0.
Normal/ASan/UBSan host comparisons cover4,000 integer and1,500 floating inputs at
ten capacities and four rounding modes. Native tests cover120 independent exact
Decimal binary64/binary128 fixtures, six capacities, mixed/copied arguments,
wide UTF8, per-thread errno/rounding, heap0 and stable repeated-process frames.
Full host and configured sanitizer suites pass. See
[profile and retained diagnoses](NATIVE_STRING_FORMATTING.md) and
[evidence](validation/stage3-native-format.json). This supplies the real formatter
required by the three remaining i18n compile failures; a complete i18n/data/engine
build and runtime still need separate qualification.

## Genuine native ICU i18n runtime

All **254/254** i18n sources now form a real native archive. Direct and SDK UEFI
fixtures pass original regex/capture/Unicode property behavior, bounded regex
stack and adversarial timeout, custom plural/select/choice rules, serialization
through snprintf, Latin/Hangul NFC/NFD, native threading and actual cleanup.
User heap returns to zero and repeated processes retain stable warm frames.
Full host regression passes. The SDK exports public i18n headers/library through
an explicit native package. The pinned JSCOnly preflight now finds uc+i18n79.1
and retains a nonzero failure for **missing data only**. The bootstrap archive
stays empty and separately named. See [scope and limits](ICU_I18N_NATIVE.md) and
[evidence](validation/stage3-icu-i18n.json). Full data/Intl, the real browser engine,
production security, wireless and physical/Fusion qualification remain open.

## Original external Unicode normalization and IDNA data

Four pinned original ICU79.1 resources now load natively for NFKC/NFKD, full
and simple NFKC case folding and UTS46 domain processing. Tests cover all20,171
Unicode18 normalization rows, all20 transformations per row and293,187 further
assigned scalar identities. All6,396 IDNA rows execute19,188 operations under
the original ICU79 error/success-string policy, including its documented trailing
root-dot exception. Another19,182 UTF8/UTF16 API comparisons and malformed-UTF8
checks pass. Missing/malformed headers and denied file access fail.
Repeated native workers and cleanup return heap allocations to zero and retain
stable warm process frames. Direct Make and SDK/isolated UEFI executions pass;
full host regression passes. See [profile, provenance and precise oracle limits](ICU_NORMALIZATION_DATA_NATIVE.md)
and [accepted evidence](validation/stage3-icu-data.json).
The SDK exports selected-resource paths explicitly; full ICU data/CLDR, Intl,
JavaScriptCore/WebKit, the modern browser, production security and live WiFi
remain unfinished. No new binary replaces cc29ea8 and version0.50.0 is not released.

## Default ICU package and successful JSCOnly configuration

Original verified host generators now produce4,321 default ICU resources and a
real AArch64 libicudata.a. Three independent builds match byte-for-byte. All
resource offsets/lengths/hashes and malformed-package tests pass; full host
regression passes. The SDK finds data+i18n+uc, and a complete pinned JSCOnly
checkout now configures successfully against it. See [generation, limits and
retained failures](ICU_DEFAULT_DATA.md) and [evidence](validation/stage3-icu-default-data.json).

The full-data u_init executable remains correctly refused by the existing2MiB
linker bound; no larger profile is substituted. The first actual engine build
fails on missing libc++ bit-operation headers and sys/mman.h. This is a genuine
configuration milestone, not a built/running engine or modern browser. Full
runtime/memory integration, browser security/rendering/features, live WiFi and
independent target qualification remain open. No new ISO or0.50.0 release.

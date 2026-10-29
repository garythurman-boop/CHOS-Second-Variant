# Modern browser engine port decision

This assessment implements the research/architecture portion of the revised Stage
3 request. It does not claim an integrated engine, JavaScript execution or modern
web rendering. The existing HTTP preview remains separate recovery functionality.

## Evidence and selection

Inspected upstream source snapshots and license/build requirements are recorded
with immutable commit URLs and SHA-256 in
[the research manifest](dependencies/browser-engine-research.json). Failed lookups
are recorded as unavailable, not silently used as evidence. A source reference for
research is not a pinned production release or vendored dependency.

| Engine | Upstream evidence | Fit and implementation cost |
|---|---|---|
| Chromium/Blink/V8 | Chromium Linux build instructions and BSD-style top-level license at 5bb1d4519dcc361c4e75771c736e75b650ec6630 | Mature modern web platform; native Linux build/IPC/sandbox/threads, substantial runtime and graphics dependencies; not a CHOS binary target |
| WebKit/WPE/JavaScriptCore | WPE CMake dependency manifest and JavaScriptCore build at 931e840055223f1f1400f09e1bad1334ca0b92f7 | Mature ARM64 embedded engine with a C-facing embedding boundary; custom software display/input backend is a suitable port seam; GLib/GIO/threading/IPC/C++ and graphics dependencies remain substantial |
| Servo/SpiderMonkey | Supported OS list and Cargo manifest at 958f66877626ecb6ecd4bdbfd636d2517e6760b3; MPL-2.0 license | Modern Rust engine; manifest requires Rust >=1.88, std, mozjs 0.26.9, Tokio, Rustls and graphics/font infrastructure; CHOS needs Rust std/platform/runtime and rendering integration |
| Ladybird/LibWeb/LibJS | Build instructions and BSD-2-Clause license at bf35424e41a23f7e9ee7fc215f396942af57a79b | Maintained independent engine, not a text renderer; also requires a large C++/POSIX/process/rendering port. No evidence of a ready CHOS backend |

Select **WPE WebKit as the initial port architecture**, with JavaScriptCore,
software-backed native CHOS window surfaces, isolated WebContent/Network processes
and a C-facing CHOS platform adapter. This choice uses the embedded-port boundary
rather than a Linux desktop shell. Software rendering must be demonstrated on a
supported WPE release before accepting this architecture as implemented. The
inspected development manifest reports 2.55.0; that snapshot is research evidence,
not a release candidate. If this seam cannot provide the required software path,
re-evaluate Servo/Ladybird using concrete build/render results.

## Required runtime implementation

The UEFI runtime now has bounded CPU isolation and scheduling with QEMU evidence
(UEFI_PROCESS_RUNTIME.md), while Fusion runtime validation remains open. It cannot
yet host the modern engine or a complete untrusted-content sandbox. Both profiles retain only
four processes, 64 KiB CHCX images, at most 32 KiB stacks, four open files/process,
a four-MiB address-space frame budget and four TCP sockets. No complete libc/C++
standard library, mmap, pthread/futex, process IPC/shared-memory/descriptor passing,
dynamic loader, event-FD/poll layer or browser sandbox exists. These are active
implementation dependencies, not merely unavailable hardware.

The WPE manifest explicitly requires GLib/GioUnix/threads, HarfBuzz/ICU, JPEG/PNG/
WebP, Epoxy, Soup3, LibXml2, SQLite, Zlib and Threads. Port the necessary interfaces
and libraries with pinned sources, licenses and reproducible offline builds.
Do not treat a host library's presence as a working CHOS target library. Do not
remove process isolation, relax W^X, or give web content raw kernel/device access
to bypass a missing runtime. Initial JIT-disabled execution is acceptable if the
selected engine supports it and real JavaScript/conformance evidence is recorded.

`tools/browser_runtime_probe.py` checks actual cross-target header/compiler
prerequisites. Missing facilities make it exit nonzero. Its output is a dependency
report, not an engine build, a web standards result, or a successful Stage 3 gate.
Next runtime work must implement bounded user mappings/allocation, ownership,
preemption/cancellation and IPC, then the C/C++/threading/event APIs required by the
chosen release. No arbitrary modern-engine memory/performance limit is claimed
without guest measurements.

## Networking, TLS and licensing boundary

Browser processes consume OS-owned TCP/status APIs. Only trusted Network Manager
code applies profiles. Ordinary EL0 file-mutation syscalls cannot replace/delete
its profile or move its protected parent. This boundary is tested by a real EL0
application. It is not yet a complete web-content sandbox or per-site permissions.

WPE's Soup3 stack brings GnuTLS dependencies; that engine networking integration
remains open. The native transport now uses pinned MbedTLS, extending the existing
reviewed DRBG dependency through a bounded C interface with actual EL0 handshake,
certificate and failure tests. This is a tested CHOS transport seam, not evidence
that Soup/GnuTLS or WPE already uses it. Engine integration must resolve the broker
adapter or the complete Soup dependency chain without assuming a drop-in swap.
Production entropy/time/trust-root policy still needs qualification; accepted EFI
RNG availability on Fusion has NOT RUN. No HTTP downgrade or certificate-check
bypass is permitted. The HTTP recovery preview keeps HTTPS disabled.

WebKit contains LGPL/BSD and other separately licensed components. Before vendoring,
record actual release tarball/commit, digest/signature provenance, every component
license and source/relinking obligations, root/codec/font licenses and a security
update process. Browser-engine security updates must be tracked separately from
CHOS releases; no unmaintained snapshot should be shipped as secure browsing.
The same obligation applies to Servo's MPL files and its transitive dependencies
or to the alternate engines' bundled components. No engine source is vendored in
this milestone, so these dependencies are not represented as shipped libraries.

## Open completion gates

No engine binary is linked into CHOS and no modern HTML/CSS/JavaScript, tabs,
cookies/web storage, forms/downloads, HTTPS, media, WebAssembly or public-site
compatibility is claimed. These remain mandatory in the revised request, alongside
real Wi-Fi, IPv6, multiple interfaces, a complete control-panel/network service and
platform-specific tests. A working Ethernet manager does not close those gates.

## Verified embedding seam and first runtime implementation

Additional immutable-source evidence locates `WPEView::render_buffer` and the
`WPEBufferSHM` interface at Source/WebKit/WPEPlatform/wpe. The latter explicitly
provides ARGB8888 memory buffers with GBytes and checked dimensions/stride. This
supports selecting a native software-buffer port seam; it does not demonstrate
that the engine currently builds or renders on CHOS. Corrected path lookups and
WebCore/JavaScriptCore license texts are included in the research manifest.

The next runtime source iteration implements bounded private anonymous RW/XN
pages, guard protections, partial-failure rollback/quarantine and process-exit
cleanup. See USER_MEMORY.md for actual live EL0 evidence. This is a real first
allocation primitive; the complete POSIX/C++/threading/IPC and UEFI isolation gaps
remain. The compiler preflight still correctly fails missing POSIX/WPE headers.

Native executable progress is recorded in NATIVE_EXECUTABLES.md: strict CHOS-ABI
static ELF64 with separate RX/R/RW segments, bounded larger sparse images and
deferred cleanup now has live direct-EL0 evidence. This is not Linux binary
compatibility, a complete C/C++ runtime or engine integration. UEFI runtime qualification, engine-sized memory, threads/IPC and secure transport
remain open. Native allocation/string prerequisites are recorded in NATIVE_C_RUNTIME.md.

PROCESS_IPC.md records a further nonblocking, owner-bound mutual-process message
primitive with real EL0 tests. WPE process integration, POSIX stream/descriptor
semantics, shared buffers, threads and full engine-sized runtime remain open.

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

The native TLS adapter now accepts IPv6 peers through the same verified context,
using owner-bound TCP6 rather than a separate discovery/configuration path. See
NATIVE_TLS_CLIENT.md and validation/stage3-native-tls6.json. This supplies a real
transport seam; it does not complete WPE/Soup, DNS-to-navigation, modern engine,
production CA/time or wireless integration.

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

An actual LLVM libc++19.1.7 cross-compile now identifies concrete C-header/runtime
gaps. The [base C iteration](BASE_C_LIBRARY.md) implements numeric/search/sort and
allocation-free per-thread UTF-8/16/32 conversions with host and actual EL0 evidence.
This addresses its div/mbstate prerequisites. Math, scheduler/thread, C++ library/
ABI and further engine dependencies remain active; this compile probe is not an
engine build or browser conformance result.

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

The subsequent [synchronization component](NATIVE_CXX_SYNCHRONIZATION.md) closes
the concrete mutex/CV/call_once gap and runs original ICU79.1 umutex.cpp in a
bounded native test fixture, with its cleanup recorder explicitly test-only.
[Native std::thread](NATIVE_CXX_THREADS.md) now supplies upstream C++20/23
create/move/join/exit-notification lifecycles and real termination handlers. Full
ICU data/i18n/uc, public futures/exception ABI, detach, engine-sized memory/runtime,
WTF/JavaScriptCore/WebCore platform integration and modern browser conformance
remain open. These are tested prerequisites, not an engine rendered in CHOS.

Current native ICU common progress and remaining data/i18n gate are recorded in
[ICU_COMMON_NATIVE.md](ICU_COMMON_NATIVE.md). The common library now executes in
native guests; the original threading-only/compile-only limits above are historical.
The user now authorizes critical interim test builds; full Stage3/version0.50.0
acceptance remains separate and open.

The [native i18n library](ICU_I18N_NATIVE.md) now builds all254 units alongside
common. Four original [normalization/UTS46 resources](ICU_NORMALIZATION_DATA_NATIVE.md)
add native Unicode18 normalization and domain-data qualification. These selected
resources are exported explicitly; they do not satisfy the full ICU data/CLDR
component. The last actual pinned JSCOnly preflight finds uc+i18n and fails for
missing data. Full data generation/packaging, engine-sized bounded memory and
subsequent engine/platform integration remain open; no engine execution is claimed.

The [default ICU data milestone](ICU_DEFAULT_DATA.md) now supplies an actual native
libicudata.a with4,321 generated resources. The complete pinned JSCOnly source
closure configures successfully against the SDK. Full embedded data is still
refused by the current2MiB executable limit; actual engine compilation stops at
missing libc++ bit-operation headers and sys/mman.h. This supersedes the earlier
missing-ICU-data configuration diagnosis. No engine execution, modern browser,
production security, live wireless or full Stage3/version0.50.0 success is claimed.

The subsequent [larger native profile and generated ICU resource execution](NATIVE_LARGE_RESOURCES.md)
qualifies1MiB allocations,4MiB images and8MiB owned frames in both QEMU boot
modes. C++23 byteswap is now supplied and tested. The historical2MiB/4MiB and
missing-byteswap limits above describe earlier snapshots. The full32MiB embedded
data package remains over budget; JavaScriptCore still requires real virtual
memory APIs and further platform work. This is not Stage3 completion.

The subsequent [native virtual-memory component](NATIVE_VIRTUAL_MEMORY.md) now
supplies owned anonymous protection/discard/partial unmap and bounded native
unistd operations. Original WebKit VM helpers execute against the SDK with a
checked CHOS discard adaptation. Actual JavaScriptCore compilation moves past
the missing memory headers to fcntl.h and stderr stream APIs; no complete engine,
modern browser or Stage3 completion is claimed.

The subsequent [native bmalloc service milestone](NATIVE_BMALLOC_SERVICES.md)
builds the actual upstream system-malloc library and executes allocation, entropy,
diagnostics and mutex paths in CHOS. Original C++23 expected/hash facilities also
run; the original numbers header's widened-double precision limitation is explicit.
Actual engine compilation now reaches WTF threading/numeric dependencies beyond
the earlier fcntl/stderr blocker. This is component evidence, not JavaScript,
rendering, production entropy, new hardware qualification or Stage3 completion.

The subsequent [native WTF synchronization and clock component](NATIVE_WTF_SERVICES.md)
executes original primitive/time headers and clock units using real private pthread
and checked clock services. Original numeric algorithms and transactional native
header import are qualified. Complete WTF/JSC compilation, Thread lifecycle/GC
suspension, engine-sized memory and all full browser/wireless/platform gates remain
open. The development version remains0.30.0; this source work does not replace the
cc29ea8 user-test ISO or imply Stage3 completion.

The subsequent [native current-thread context component](NATIVE_THREAD_CONTEXT.md)
provides real own-thread stack/GPR/SIMD/FPU snapshots with read-only capability
checks and actual AArch64 SDK assembly links. Native WTF register declarations and
current stack discovery compile. Foreign-thread suspension/GC, complete Thread
lifecycle, engine-sized stack/memory and the other modern browser/wireless/platform
requirements remain open; this is not Stage3/version0.50.0 completion.

The [native WTF entropy/process/page component](NATIVE_WTF_ENTROPY.md) now
executes actual upstream adapters in both QEMU boot modes, with explicit denied
and missing-source failure and modeled partial/retry error tests. Full engine,
production entropy security, browser, wireless and platform gates remain open.

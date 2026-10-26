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

Prefer the selected engine's maintained TLS/network implementation through an
OS transport adapter. WPE's Soup3 stack brings GnuTLS dependencies; evaluate that
complete dependency chain against an isolated CHOS transport before selecting a
second independent TLS implementation. Require validated entropy, clock policy,
trust roots, SNI/hostname/chain/date validation and fail-closed deterministic tests.
Fusion has no supported entropy source yet. No HTTP downgrade or certificate-check
bypass is permitted. The HTTP preview keeps HTTPS disabled.

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

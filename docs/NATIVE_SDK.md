# Native SDK and real cross-compilation links

`make native-sdk CC=clang LD=ld.lld` stages the actual native C/C++ headers, static
runtime/math libraries, CRT entry/initialization objects, CHOS ABI note and linker
script under build/native-sdk. LLVM/musl licenses and a complete SHA256 inventory
are included. The installer builds a fresh sibling tree and replaces only an
intact prior SDK it owns; added/modified files cause refusal instead of deletion.
The C archive uses the explicit Makefile object list, not a directory glob that
could accidentally retain obsolete runtime objects. `tools/verify_native_sdk.py`
checks membership and every staged hash. This is a bounded static SDK, not a full
hosted C/C++/POSIX implementation or a modern browser.

Use ports/native/chos-toolchain.cmake with CMake and set CHOS_SDK_ROOT to the staged
root. The profile builds EXECUTABLE try-compile tests against real native CRT and
runtime archives. It does not use static-only probes, force success results or
provide dummy libraries. The compiler uses normal language main linkage/return
rules via -fhosted, which does not imply complete hosted library conformance.
Existing freestanding library/kernel builds retain their own flags. C++ wrapper
headers precede C headers to preserve include_next and overload contracts.

CHOS system identity inherits neither Linux nor POSIX platform flags. Target
headers/libraries/packages are searched only in the staged root; host build
programs remain usable. pkg-config's library directories and sysroot are restricted
to the target tree. No host libc, C++ library or ICU is substituted. Static native
images have a4MiB span limit,8MiB owned-frame quota and bounded heap/thread budgets;
those must be expanded for the engine.

## Actual checks

`tests/native_sdk` is a normal CMake project with ordinary C/C++ main functions.
Positive C allocation and C++ thread probes really link. A deliberately missing
library and a deliberately missing import both fail. C and C++ executables run
with argc/argv in direct and UEFI QEMU, including zeroed C allocation, global C++
vector construction, three std::thread workers, cleanup and alternating process
relaunch. The SDK also configures/builds after relocation into a path containing
spaces; the relocated executables are the UEFI fixture inputs. Guest checks use
disposable media, immutable boot snapshots, exit-status verification, stable warm
PMM pages and no queued futexes. See validation/stage3-native-sdk.json.

The original compiler-only JavaScriptCore preflight remains explicitly available
for dependency triage. Its new --native-sdk option selects the real linker
profile and verifies the staged SDK before running actual upstream CMake. The
native run still stops on missing ICU>=70.1 data/i18n/uc; no engine was built or
executed. The complete CMAKE_HAVE_LIBC_PTHREAD test rejects missing detach/cancel/
atfork APIs. CMake's smaller -pthread probe really links native create/join and
therefore reports Threads_FOUND. That is evidence for this tested subset, not
full POSIX or an external libpthread. Missing -lpthread/-lpthreads libraries are
correctly rejected. Earlier archive-only Threads results remain unqualified.

## Remaining gates and evidence retention

Full target ICU/data generation, C stream/formatting/numeric/locale dependencies,
engine-sized memory, further POSIX/loader/IPC/isolation, JavaScriptCore/WebCore and
browser conformance, production TLS policy and real Pi5 wireless remain open.
Version0.50.0 and new binary releases remain held; local SDK/media builds are
permitted. New Fusion/Pi/laptop execution is NOT RUN.

To make room for sustained work,61 completed runtime test disks were losslessly
compressed. completed-disk-archives.json under /workspace/chos-evidence records
original path, mode, byte count, SHA256 and gzip restoration instructions. Both
archive decompression and a second original read were verified before replacing
the raw file. Active QEMU directories were excluded. No test contents or failures
were discarded; logs and boot snapshots remain available. Earlier completed
firmware archives have their separate completed-runtime-vars-archives.json index.

The subsequent [RTTI profile](NATIVE_RTTI.md) adds original LLVM type information
and dynamic_cast to libchos-c++.a, with its upstream license in the staged SDK.
Consumers opt in using -frtti after the default flags; exceptions stay disabled.
Native byte C classification, intmax conversions and integer format macros are
also staged. This extends the bounded SDK without claiming full hosted conformance.

[Binary128 compiler helpers](NATIVE_BINARY128.md) are now staged as
libchos-quad.a and linked on demand in the same static runtime group. An actual
CMake long-double divide probe and the full native arithmetic fixture link and
run through the SDK. This does not supply complete long-double libm or dynamic
floating-environment conformance.

[Native floating parsing](NATIVE_FLOAT_PARSING.md) adds libchos-floatparse.a and
real C-locale strtof/strtod/strtold/atof. The CMake probe really links strtod, and
its full fixture executes through the SDK in isolated UEFI. Small original
long-double math helpers and nexttoward are also now present; full streams,
formatting, locale switching and long-double transcendental support remain open.

[Ranges/span and binary32/64 complex arithmetic](NATIVE_RANGES_COMPLEX.md) are now
included in the bounded native profile. libchos-complex.a supplies actual compiler
mul/div helpers on demand; CMake performs a real complex link and its C++23
range/complex fixture executes in UEFI CHOS. Exceptions, localization, stream I/O
and binary128 complex support remain outside this qualified subset.

[Wide strings and fixed UTF8 conversion](NATIVE_WIDE_STRINGS.md) are staged as
libchos-wide.a with original C++ wrappers and an actual wchar_t string
instantiation. CMake links wstring and rejects unsupported wide formatting and
wide numeric parsing. The UEFI fixture uses the SDK executable. C-locale wide
classification is invariant; ICU Unicode/locale data and full streams remain open.

Read-only buffered C streams and owned signed-64-bit file positions are now available
in the SDK through libchos-stdio.a. See [profile, evidence and limitations](NATIVE_READ_STREAMS.md).

The SDK also stages ICU common with a separately named bootstrap data target.
Use the [explicit native ICU package/profile](ICU_COMMON_NATIVE.md); real i18n and
complete data libraries remain absent, so the full engine dependency gate stays open.

Original ordered associative containers and cstdarg are now available; see the
[native C++20/23 profile and tests](NATIVE_ORDERED_CONTAINERS.md).

The SDK now exports real bounded snprintf/vsnprintf through libchos-format.a;
see [the native profile and numerical/ABI checks](NATIVE_STRING_FORMATTING.md).

Native ICU i18n is also exported through CHOSICUI18n; consult the
[qualified APIs and missing data gate](ICU_I18N_NATIVE.md).

The SDK additionally stages four original normalization/UTS46 data resources
through CHOSICUNormalizationData. See [deployment and native corpus qualification](ICU_NORMALIZATION_DATA_NATIVE.md).
This is explicit selected data, not the complete ICU data/CLDR component.

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

The [native standard-stream component](NATIVE_STANDARD_STREAMS.md) supplies
checked unbuffered console output and actual upstream WTF FilePrintStream/PrintStream
execution. Normal/sanitizer,SDK links and seven guest profiles qualify the component.
Ordinary files stay read-only;complete stdio,engine/browser and hardware gates remain open.

The [native numeric/search component](NATIVE_CHARCONV.md) now executes original
libc++ charconv/Ryu and musl memmem through the actual WTF span search path.
Seven guest profiles,independent Decimal vectors and full host/sanitizer suites
qualify the bounded subset. Full WTF compilation still records17 blocked units;
engine/browser,production security,wireless and platform acceptance remain open.

The [native UTC/WTF date and UTF component](NATIVE_WTF_CALENDAR.md) executes
actual upstream parsing,conversion and process helpers with bounded native APIs.
Eight accepted guest profiles and full host/sanitizer suites qualify this component;
earlier timeout traces remain recorded. Full WTF still has eight blocked units,
and subsequent engine/browser,security,wireless and platform gates remain open.

Native self-only CPU/memory snapshots and WTF adapters are described in
[NATIVE_WTF_RESOURCES.md](NATIVE_WTF_RESOURCES.md). CPU accounting excludes blocked
and idle intervals and retains completed threads; memory charges owned user
frames. Unavailable measurements remain explicit. These component services do not
qualify full WTF/JSC or change the bounded engine memory profile.

The native read-only snapshot service additionally supplies exact open-time file
length/current position and cursor-independent `pread` through owned descriptor
operations. Both share the READ permission class. See
[NATIVE_POSITIONAL_READS.md](NATIVE_POSITIONAL_READS.md) for bounds, snapshot
semantics and qualification; no POSIX inode or writable-handle model is implied.

The native SDK exports `chos/file_write_api.h` and the working-copy open/write/
truncate/flush/abort helpers in `chos/user.h`. Calls70..73 require WRITE_FILE18;
open also requires OPEN8/READ9, and abort shares CLOSE10. Old mask bits and read-only
opens remain compatible. These are bounded native services with explicit conflict
handling, not POSIX fcntl/stdio or a complete browser filesystem. See
`NATIVE_WRITABLE_HANDLES.md` for ownership, quota, close and persistence semantics.

The existing `libchos-quad.a` now also supplies the selected original LLVM19
128-bit integer division/modulo and binary32/binary64 conversion helpers. The
musl math archive supplies original `ldexpl` over the selected binary128 scaling
routine. Archive names/link groups remain compatible. The integer dependency
manifest/verifier preserves original bytes; this is selected ABI coverage, not
full compiler-rt or all floating-environment conformance. See `NATIVE_WTF_INT128.md`.

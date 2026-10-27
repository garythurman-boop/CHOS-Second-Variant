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
images still have the2MiB span limit and the current small heap/thread budgets;
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

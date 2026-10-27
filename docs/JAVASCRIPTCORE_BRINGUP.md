# JavaScriptCore upstream-source bring-up

This is a reproducible dependency investigation against actual upstream source,
not an engine build, JavaScript execution, web rendering or Stage3 acceptance.
WPE WebKit remains the selected initial architecture. The inspected production-
release candidate is now tag `wpewebkit-2.54.2`, commit
`32717fabcb9dcad9aa00a54ef1f23e41ee64e849`, rather than treating the earlier2.55
research snapshot as a pinned distribution. Full engine/dependency/license and
security-release qualification remain open. No WebKit binaries are distributed.

The [source/patch manifest](dependencies/webkit-jsc-bringup.json) records Git object
identity, inspected file hashes, the sole port patch and its exact post-image.
A partial HTTPS Git checkout supplied the actual root files, Source/cmake,
Source/WTF, Source/JavaScriptCore and Tools/CMake sources. This is not a mocked
folder tree or a host JavaScript engine linked into CHOS. GitHub archive retrieval
was unavailable and the official release site returned403 in this environment;
HTTPS Git retrieval succeeded. No TLS/signature bypass was used and no release
signature verification is claimed. Full WebCore/WPE is not checked out yet.

The unmodified configure rejected unknown OS `Generic`. The small checked patch
recognizes explicit CMake system identity CHOS without inheriting Linux/UNIX
feature claims. A local CMake Platform/CHOS definition disables shared libraries.
This is identification only: no native WTF OS/API backend is implemented by it.
The toolchain uses actual AArch64 Clang, the native C/C++20/23 header subsets and the
same math configuration used by guest tests. Target include/library/package
searches stay inside the CHOS sysroot; host build executables remain usable.
A host ICU installation cannot satisfy this target dependency.

## Actual result and limits

CMake3.31.6, Ninja1.11.1.4 package, Clang19.1.7 and Ruby3.3.8 ran the upstream
JSCOnly configuration with static JSC, system malloc and JIT disabled. Ruby was
added locally from Debian packages checked against the onboarding's signed
package indexes, without a system-wide install. The final reproducible configure
reaches a hard failure requiring **CHOS-built ICU >=70.1 data/i18n/uc**. No ICU
headers/libraries were faked or substituted from the x86 host. A configured or
linked JavaScriptCore target has not been produced.

The initial `INT128_IS_USABLE` failure came from the C++20 header closure's
missing C++23 `__utility/to_underlying.h`, not a CPU limitation. Six original
upstream headers now extend the closure to656 files. The exact engine integer128
type/alignment probe now passes. Native C++23 container/allocation/failure tests
pass in direct and UEFI QEMU; the existing C++20 profile remains supported. This
is not full C++23 conformance or integer128 arithmetic-runtime qualification.
Several file/time/POSIX probes still fail on genuinely absent APIs.
The full pthread test rejects absent
detach/cancel/atfork functions, as it should. Its fallback `Found Threads` result
is **not accepted evidence**: the temporary compiler-only toolchain builds static
try-compile archives, so CMake's library-existence fallback does not really link
`-lpthreads`. That limitation is explicit in the toolchain and result report.
The separately verified native pthread subset remains real; full POSIX support
is not implied by that configure output. A final executable-link toolchain and
accurate native feature checks are still required before an engine build.

## Reproduction

Prepare the compiler and native SDK first:

```
source /workspace/chos-tools/activate
make CC=clang LD=ld.lld -j4 libcxx libm
```

Create a separate reference checkout (not a CHOS worktree):

```
git clone --depth 1 --single-branch --branch wpewebkit-2.54.2 \
  --filter=blob:none --sparse --no-checkout https://github.com/WebKit/WebKit.git WEBKIT_REF
git -C WEBKIT_REF sparse-checkout set Source/cmake Source/WTF Source/JavaScriptCore Tools/CMake
git -C WEBKIT_REF checkout --detach 32717fabcb9dcad9aa00a54ef1f23e41ee64e849
python3 tools/preflight_javascriptcore.py --source WEBKIT_REF --out FRESH_OUTPUT
```

The runner verifies the pin, source and patch bytes, refuses unexpected source
changes, applies the sole patch to a clean reference, and records the exact
command, log digest and nonzero CMake result. It accepts its own matching patch
on subsequent runs but refuses existing output directories. Supply `--cmake`,
`--ninja`, `--ruby` when build executables are outside PATH. With the local extracted
Ruby, additionally set:

```
export RUBYOPT=--disable-gems
export RUBYLIB=/workspace/chos-tools/root/usr/lib/ruby/3.3.0:/workspace/chos-tools/root/usr/lib/x86_64-linux-gnu/ruby/3.3.0
```

`--out` stores configure.log, result.json and CMake's detailed compiler diagnostics.
An incomplete configuration intentionally returns nonzero. Do not label this
command a passing engine test. The recorded run is
`/workspace/chos-evidence/jsc-cxx23-preflight`; earlier vanilla/native
configuration attempts remain alongside it. See
[the checked-in result](validation/stage3-jsc-bringup.json).

Next implementation must expand actual C++23/C++ ABI support, global constructor/
atexit and compiler builtins, native executable/large-image/virtual-memory support,
POSIX descriptors/events/thread adapters and target ICU. Then build and execute
JSC on CHOS, run its language tests, and continue WebCore/WPE software rendering,
font/image dependencies, the TLS/network broker and real browser integration.
Production trust/time/entropy policy, browser isolation/conformance, real Pi5
wireless and independent new Fusion qualification remain full-stage requirements.
Binaries stay held and completion version0.50.0 is not assigned.

The subsequent [C++ synchronization component](NATIVE_CXX_SYNCHRONIZATION.md)
implements real mutex/CV/call_once and shared ownership over native pthreads.
Original ICU79.1 umutex.cpp now compiles and runs in the bounded component fixture
with debug assertions. This closes the specific missing threading prerequisite;
no target ICU uc/i18n/data libraries have been built or installed for FindICU, so
the upstream configure gate is not marked passed. Larger runtime/memory and
production engine/security qualification remain required.

[Native std::thread](NATIVE_CXX_THREADS.md) now supplies actual upstream worker
lifecycles and ordered exit notifications in the bounded C++20/23 profile. This
is another runtime prerequisite, not an ICU library installation or passed JSC
configure/execute gate. Existing engine-sized memory, full runtime/isolation,
ICU data/i18n/uc and production security requirements remain active.

The [native SDK](NATIVE_SDK.md) now supplies a real executable-link profile.
`tools/preflight_javascriptcore.py --native-sdk build/native-sdk ...` selects it
without changing the preserved compiler-only triage mode. Actual native C++ thread
and negative missing-library/import probes now have link and guest evidence.
The upstream run still stops at missing target ICU data/i18n/uc. Threads_FOUND
reflects the real create/join probe; the complete detach/cancel/atfork test still
fails. Full POSIX and an engine build are not claimed.

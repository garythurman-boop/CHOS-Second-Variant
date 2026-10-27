# Native C++ ranges and binary32/64 complex arithmetic

The bounded native libc++ profile now includes original LLVM19.1.7 ranges,
span and complex headers. Thirty-seven headers were imported from the pinned
release archive with their original bytes. The existing manifest/verifier checks
all763 libc++ files. Filter/transform/iota/take/drop/reverse/split/join views,
projection algorithms, span and C++23 ranges::to/zip/chunk_by have actual native
execution evidence. The default no-exceptions/no-localization profile is retained.
This is not a complete standard library, locale/stream implementation or engine.

Complex float/double multiplication and division require real compiler helpers.
Four original LLVM20.1.8 sources implement those helpers, using the already pinned
internal header closure and original20.1.8 fp_lib.h. The dependency manifest
records the immutable source commit and per-file hashes. A separately extracted
libchos-complex.a is staged and linked in the native SDK's runtime group; no host
complex/math library is substituted. Binary128 complex arithmetic is not supplied.

## Numerical qualification

The fixture uses396 mpmath200-digit float/double references across multiply,
divide, sqrt, exp, log, sin, cos, tan, abs, arg and complex power. Expected numerical
bits come from that independent high-precision oracle. Acceptance uses a fixed
error bound of8 machine epsilon times the maximum magnitude of the two reference
components. This is a bounded numerical regression, not exhaustive IEEE/C++ or
JavaScript conformance, nor a correctly-rounded complex transcendental claim.

The initial per-component ULP test rejected complex pow(-3+2i,0.75-0.25i): its
small real component differed by72 float ULPs. The original libc++19 host code
produces identical bits to CHOS; the error relative to the full result magnitude
is below one epsilon. The fixture now uses the documented magnitude-scaled bound;
reference value bits and upstream code are unchanged. A second exact-zero check
for sqrt(-4-0i) exposed original libc++'s polar-form tiny real residue on the host
as well. The same numerical bound applies there; the negative imaginary branch
is checked, and sqrt(+0-0i) retains exact signed-zero tests. Both earlier failures
and host reproductions remain recorded.

Host normal/ASan/UBSan tests directly execute the four original compiler helper
implementations with prefixed symbols, checking72 independent high-precision
mul/div cases and NaN/infinity/zero behavior. That host path does not establish
ARM64 ABI behavior; native guest fixtures do. SDK CMake actually links complex
multiplication, while deliberately missing imports/libraries continue to fail.

## Native runtime evidence and scope

C++20 direct mode and C++23 isolated UEFI mode run the actual static ELF fixtures;
the UEFI program is built by ordinary CMake through the SDK. Each profile executes
three processes. Three std::thread workers exercise repeated range and complex
operations across three worker lifecycles per process. Views remain lazy and refer
to live source values. All user heap allocations/maps are released after join;
child exit, stable warm physical frames, no queued futex waiters and no unresolved
imports/PT_TLS/W+X segments are checked. The existing RTTI UEFI fixture and full
host/sanitizer suites also pass. See validation/stage3-native-ranges-complex.json.

Actual code generation now succeeds for197/202 original ICU79.1 common units,
including the formerly missing complex/ranges paths. The objects total3,800,656
bytes. This is object-generation evidence only: no complete ICU archive, data/
i18n installation, final link or engine runtime is claimed. Five units still fail
on wide-character or file/time interfaces. Native stdio/locale, data generation,
engine memory/isolation, production TLS/browser integration and Pi5 wireless
remain open. New Fusion/Pi qualification is NOT RUN. Stage3/version0.50.0 and new
binary publication stay held.

Twenty-five completed prior guest boot inputs were losslessly compressed to keep
space for sustained builds. /workspace/chos-evidence/completed-boot-input-archives.json
records original/archive paths, modes, byte counts, SHA256 and restoration steps.
Decompression and a second original hash were verified before replacing each raw
file; active QEMU directories were excluded. Test contents and failures were retained.

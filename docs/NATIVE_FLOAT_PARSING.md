# Native floating text conversion

The native SDK now provides real C-locale `strtof`, `strtod`, `strtold` and `atof`
from original musl1.2.5 sources. Decimal/hexadecimal input, exponent/sign handling,
infinity/NaN, end pointers, nearest rounding and tested directed halfway cases
run in native CHOS. The separately extracted libchos-floatparse.a avoids imposing
binary128/math objects on legacy programs that do not use floating conversion.
The SDK links it with the real math/compiler/C runtime archives in a linker group.
No host conversion or math library is used by the target application.

The private scanner is a real bounded memory-string backend. Original sh_fromstring
supplies its sentinel; the native adapter replaces that with one-past-NUL before
scanning, preserving lookahead/pushback without reading beyond the string. Field
limits and EOF update actual scanner state. FILE/offset names are internal to
these translation units, not exported through the public stdio header. No file
stream, descriptor, Linux syscall or I/O-success stub is supplied. Future native
stdio still needs its own real implementation.

The math archive additionally includes original binary128 classification/sign,
fmodl, scalbnl, frexpl, fabsl and copysignl, and binary32/64 nexttoward. All14 newly
imported scanner/math files matched the immutable musl Git mirror commit over
HTTPS. The two dependency manifests and offline verifier retain exact upstream
bytes, licenses, source selections and the existing hardware AArch64 fenv backend.

## Actual verification

Host normal and ASan/UBSan tests compare20,000 generated decimal/hex/malformed
inputs at float/double/host-long-double widths against glibc, using exact-sized
input allocations. Value/classification, signed zero, end pointers, overflow and
errno preservation are checked. Native long double is independently tested as
binary128; host x87 evidence is not substituted for it.

The native fixture covers169 decimal/hex inputs at all three target widths using
an exact rational oracle. Python computes expected binary representations from
integer ratios; host float/double conversions provide a secondary check only.
Boundary/halfway, normal/subnormal, overflow/underflow, malformed prefixes,
infinity/NaN and preserved errno cases are exercised. Three native workers parse
halfway cases under different inherited FPCR modes, retain distinct errno values
and preserve their FP environment across scheduling over three lifecycles.

Direct mode executes the Makefile-linked fixture. Isolated UEFI executes the
ordinary CMake/SDK-linked fixture. Each profile runs three processes with verified
zero exit, stable warm physical pages, no queued futexes and no unresolved imports,
ELF TLS or writable-executable LOAD segments. Existing binary32/64 math/FP isolation
also has a new UEFI regression run. Full host/sanitizer suites pass. See
validation/stage3-native-floatparse.json.

Underflow ERANGE reporting is implementation-defined by C7.22.1.3. Original musl
may leave errno clear for a tiny result where glibc reports ERANGE; tests permit
that difference only for underflow results. Overflow, numerical bits and end
pointers remain strict. No-conversion musl EINVAL is also distinct from glibc's
clear errno. Initial failures caused by overstrict glibc-only errno expectations
are retained with their diagnosis; expected numerical bits were not changed.

One regression launch was refused before QEMU execution because the freshly built
kernel and older ISO differed. Rebuilding media restored matching inputs before
the accepted final runs. The failed launch remains recorded, not counted passed.

## Remaining requirements

The current SDK lets195/202 original ICU common units pass syntax checks. This
is not a complete ICU uc/i18n/data build, link or runtime result. Complex/ranges,
wide-character, file/time/locale interfaces and data generation remain open.
The existing binary128 helpers do not supply full dynamic-FENV/exception semantics
or long-double transcendental math. Full stdio/formatting, larger engine memory,
process/security integration, the actual JavaScript/rendering engine, production
TLS policy and real Pi5 wireless are still required. New Fusion/Pi execution is
NOT RUN. Stage3/version0.50.0 and new binary publication remain held.

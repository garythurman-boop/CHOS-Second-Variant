# Native C++ RTTI and byte C APIs

The static native runtime now includes the original LLVM19.1.7 libc++abi
`private_typeinfo`, `stdlib_typeinfo`, `stdlib_exception` and `cxa_aux_runtime`
implementations. `docs/dependencies/libcxxabi-rtti.json` pins all nine original
files and the upstream commit; `tools/verify_libcxxabi.py` checks exact membership
and SHA256 offline. These implement real type information and the Itanium
`dynamic_cast` search, including virtual inheritance and access/ambiguity checks.
The native diagnostic backend prints an invariant's literal diagnostic template
and aborts; it is not a variadic printf implementation.

RTTI is opt-in with `-frtti` after the normal profile flags. The SDK and existing
applications retain their default `-fno-rtti`. Exceptions and stack unwinding are
still absent. Upstream's no-exceptions bad-reference-cast and null-polymorphic-
typeid paths call the real native `std::terminate`, which ends the application
with status134. No throw/unwind or full libc++abi support is claimed. Standard
exception *value classes* and their type information are distinct from throwing.

The C layer adds all fourteen standard byte classification/case operations and
`isascii`/`toascii`, with C-locale ASCII behavior and EOF handling. These classify
bytes, not Unicode code points. The existing UTF8 conversion APIs remain separate.
`imaxabs`, `imaxdiv`, `strtoimax` and `strtoumax` use the qualified LP64 native
integer/parser ABI, including end pointers and overflow errno. All narrow
PRI/SCN exact/least/fast/max/pointer format macros are provided; fast prefixes
follow stdint limits rather than assuming Clang and GCC select identical types.
The macros do not supply printf/scanf or wide conversion functions.

## Verification

`tests/qemu_rtti.py` runs the actual native ELF under direct and isolated UEFI
QEMU. Six processes per profile cover ordinary runs, both terminating cases and
successful relaunch. Three worker threads run64 object-graph checks each over
three lifecycles per successful process. Tests cover cross-translation-unit type
identity, virtual diamonds, cross-casts, pointer adjustment, ambiguous/private
bases, failed/null casts, successful reference casts, void pointers, array/
fundamental/member-pointer types and standard exception-value RTTI. They also
exercise the real byte/intmax APIs, overflow errno, zero remaining user heap,
stable warm physical frames and no queued futexes. Images have no unresolved
imports, ELF TLS segment or writable-executable LOAD segment.

Host normal and ASan/UBSan tests compare all byte operations for EOF..255 and
20,000 randomized integer parser inputs with glibc. Every standard narrow format
width is checked against host printf/scanf. The existing exhaustive Unicode-scalar
conversion tests also pass. Full host/sanitizer suites, native SDK CMake links/
UEFI execution and native std::thread lifecycle/refusal regression pass.
See [recorded evidence](validation/stage3-native-rtti.json).

## ICU and remaining scope

An inventory of202 original ICU79.1 common C++ translation units, checked with
native SDK headers and `-frtti`, now has194 syntax successes (previously120).
This is syntax evidence only: no complete ICU archive, data library, i18n library,
link or engine execution is established. Remaining units expose missing complex/
ranges headers, file/time interfaces, floating parsing and wide-character APIs.
Even passing units still require native link/runtime qualification. File streams,
formatting, locale/data generation, engine-sized memory/isolation, production TLS
integration and real Pi5 wireless remain active requirements. New Fusion/Pi
execution is NOT RUN. Stage3 and version0.50.0 are not complete; binaries stay held.

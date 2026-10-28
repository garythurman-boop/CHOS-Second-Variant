# Native ICU i18n library

All 254 ICU79.1 i18n translation units now build into a real ARM64
libicui18n.a alongside the 202-unit common library. Direct and isolated UEFI
SDK applications execute original Unicode regex, custom plural/select/choice
rules, NFC/NFD and cleanup paths. This closes the prior source-compilation gap;
it is not complete ICU/CLDR data, Intl, JavaScriptCore, WebKit or Stage3 acceptance.

## Source and native configuration

The [manifest](dependencies/icu-i18n.json) pins the same immutable ICU commit as
common and hashes all 541 original files. Verification checks exact membership
and original bytes. The native build force-includes a checked decContext header
through its original include guard, removing only the unused signal.h include.
The generator verifies the original header and the source's existing disabled
signal-raising code. No signal service, trap emulation or altered arithmetic
behavior is supplied. The original numeric formatter is now backed by the
[real bounded C formatter](NATIVE_STRING_FORMATTING.md).

The SDK stages original public headers, libicui18n.a and an explicit package:

```cmake
find_package(CHOSICUI18n REQUIRED CONFIG)
target_link_libraries(app PRIVATE CHOS::ICUI18n CHOS::ICUBootstrapData)
```

The bootstrap data target stays explicitly named and empty. It is not installed
as libicudata. The real pinned JSCOnly configuration now finds native uc and i18n
version79.1 and retains its nonzero failure for **missing data**. No dependency
success is synthesized, and no engine configuration or execution success follows.

## Runtime evidence and limits

A real native application verifies regex captures, case-insensitive matching,
Unicode letter properties, syntax errors, a16KiB backtracking-stack limit and
an actual match timeout for an adversarial pattern. Custom plural rules classify
100 integer values and fractions; select rules exercise matching and fallback;
choice rules serialize/reparse through snprintf and preserve behavior across
boundary values. Those custom rules do not establish CLDR locale availability.
The fixture uses explicit UnicodeString construction required by the public API.
An initial truncated negative plural rule was tolerated by the upstream parser;
the accepted negative test uses an invalid keyword and verifies a reported error.

NFC/NFD composition and decomposition of Latin combining marks and Hangul pass
using ICU's original compiled NFC data. This qualifies those cases; it does not
qualify all normalization, compatibility-normalization data or Unicode conformance.
Missing external resources still fail. There are no fake locale resources or
success callbacks.

Three native workers repeat the APIs across three lifecycles. Actual u_cleanup
returns user heap allocations to zero; three process executions exit0, preserve
warm physical-frame counts and leave no private futex waiters. Both direct Make
and ordinary CMake/SDK UEFI applications pass. A stripped copy fits the existing
2MiB file and image limits; the unstripped symbol image remains available. No
loader or memory quota is widened to make this fixture run.

See [machine-readable evidence](validation/stage3-icu-i18n.json). Full data and
locale resources, engine integration, production browser security and real WiFi
remain open. These are QEMU results; new Fusion/Pi/laptop qualification is NOT RUN.
The cc29ea8 ISO remains the selected Fusion test build. Version0.50.0 is reserved
for complete revised Stage3 acceptance.

# Native ICU common library

All 202 ICU 79.1 common translation units now build into a real native ARM64
`libicuuc.a`. Native direct and isolated UEFI applications run original ICU
UTF8/UTF16 conversions, Unicode character properties and case folding,
UnicodeString allocation/casing, the actual file loader, native threading, and
actual ICU cleanup. This closes the common-library prerequisite; it does not
complete ICU's data/i18n libraries, JavaScriptCore, WPEWebKit or Stage 3.

## Source and platform profile

[The manifest](dependencies/icu-common.json) pins commit
`ddc6b66926bfac6036144ca9902992a0d6d50af9` and hashes all 428 original files.
`tools/verify_icu_common.py` checks exact membership and bytes. The importer
validates selected checkout bytes against the pinned Git blob IDs. Original
sources remain unchanged; the native build creates a checked putil.cpp copy
with the two gettimeofday call sites changed to return NaN on failure, instead
of consuming uninitialized clock fields. Original deprecation warnings remain
visible; third-party sources are not compiled with the project's blanket Werror.
Our platform code and fixture retain strict warnings.

The port selects ICU's genuine MAP_STDIO backend. It uses process-owned CHOS
snapshot descriptors, read buffering and signed file positions. Dynamic library
loading is disabled because CHOS has no dynamic linker. Native character encoding
is UTF8. The default native calendar is fixed UTC, independent of the desktop's
display time-zone preference. The libc metadata supports invariant C/POSIX locale
formatting; unsupported locale names fail. This is not a POSIX locale/TZ parser
or a complete Unicode locale service. ICU's own locale resources still need data.

New gettimeofday calls the real clock API, validates availability and leaves its
output unchanged on failure. Its obsolete timezone argument must be NULL.
localtime_r follows the current UTC-only native calendar. getenv reads an actual
per-process environment table; the current spawn ABI starts each process with
an empty table and does not inherit an environment. No setenv/exec-environment
service is advertised. Locale environment precedence is honored when selecting
the supported invariant locale; unavailable locale requests never report success.

The official original stubdata source is compiled separately as
`libicustubdata.a`, exactly for upstream's bootstrap purpose. It is deliberately
empty. It is never named libicudata, automatically substituted for real data, or
counted as a complete engine dependency. Tests require absent data to fail.
The raw file-loader fixture contains opaque known bytes; it verifies file I/O,
not an ICU resource dataset. Complete normalization, locale resources, collation,
dictionaries and other data-dependent behavior remain unqualified.

## SDK use and checks

`make CC=clang LD=ld.lld native-sdk` stages common headers/library, license,
native configuration and an explicit CMake package. Consumers use:

```cmake
find_package(CHOSICUCommon REQUIRED CONFIG)
target_link_libraries(app PRIVATE CHOS::ICUCommon CHOS::ICUBootstrapData)
```

The bootstrap target is selected explicitly and visibly. Ordinary executable
CMake link probes pass for ICU common. No i18n or complete data library is staged.
The actual JavaScriptCore preflight now finds ICU uc and version 79.1, then fails
with its original nonzero result for **missing data and i18n**. It does not
configure or execute the engine successfully.

The native fixture uses 1,530 independent full case-fold mappings from Python's
Unicode15.0.0 database, including expansions and supplementary scalars, plus
invalid UTF8/surrogates and buffer overflow checks. Three native std::thread
workers repeat the operations across three lifecycles. Real u_cleanup returns
user heap allocations to zero; repeated processes retain stable warm physical
frames and no private futex waiters. A permission-reduced child is denied the
clock syscall and receives NaN from the patched original ICU function. SDK
executable links and isolated UEFI execution use the same real implementation.

Build with `make icu-common-test`; run `tests/qemu_icu_common.py --mode direct`
or `--mode uefi` with a fresh --out directory. The loader fixture is a separate
`icu_common_test.app.elf`: debug information is stripped from a copy to meet the
existing 2MiB file limit; PT_LOAD contents and the unstripped symbol image are
retained. No memory/loader quota is widened to make the test pass.
See [machine-readable evidence](validation/stage3-icu-common.json).

Full host and configured sanitizer suites remain required. New native library
results are QEMU evidence, not Fusion, physical Pi or laptop qualification.
Production browser clock trust, CA updates, entropy reseeding, browser isolation,
full data/i18n, the rendering engine, and the actual CYW43455 radio path remain
open Stage 3 gates. Version 0.50.0 remains reserved for complete acceptance.

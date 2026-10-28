# Default ICU data and native engine configuration

The pinned upstream ICU79.1 generators now produce the default4,321-resource
package:33,558,160 bytes. A real AArch64 libicudata.a embeds those exact bytes
under the original icudt79_dat symbol. Three separate builds produce identical
packages and native archives. The original empty libicustubdata.a remains
separately named for small fixtures; it is never substituted for this dataset.

## Source, generation and integrity

The [source manifest](dependencies/icu-data-source.json) records all7,171 original
source members, Git blob IDs, modes and SHA256 values from the same ICU commit as
the native common/i18n libraries. A deterministic29,618,037-byte source archive
is vendored with its license. Its importer fixes Git tar.umask to0022 so archive
modes match the Git executable bits; original file bytes are unchanged.
Verification enforces the compressed hash and every member before extracting
regular files into a fresh directory, retaining original timestamps/modes.

Original host common/i18n libraries and generation tools build with GCC/G++.
They are build tools, not CHOS libraries or Linux compatibility. Tests/samples/
extras/io are disabled only in this host generator build. Original data generation
uses the default upstream configuration, without filtering out locale families.
Original genccode emits assembly; the native compiler assembles it for AArch64.
Verification checks the archive member, ELF architecture, exact data symbol and
byte equality of its read-only payload with the generated package.

The [generated-data pin](dependencies/icu-generated-data.json) records the complete
package and resource inventory hashes. The bounded host reader checks every table
entry, name, offset, interval, length and individual resource SHA256. Tests reject
truncation, count/offset corruption, overlapping names/payloads, unsafe paths and
changed bytes. This host integrity tool is not a new guest loader or runtime
signature policy. Production resource authentication remains a browser security
gate.

The upstream generator emits warnings for absent lstm_res and adaboost_res targets.
They are retained in build reports. This pin lacks those optional neural break
model resources; this is the upstream default package, not every optional ICU
feature or full ICU/CLDR/Intl conformance. Data and algorithm availability must
still be checked by actual consumers.

```sh
source /workspace/chos-tools/activate
make CC=clang LD=ld.lld icu-full-data
make CC=clang LD=ld.lld test-icu-data-package
make CC=clang LD=ld.lld native-sdk
```

The source archive permits an offline generator build with Python3, GNU make,
GCC/G++ and the native Clang/LLVM tools installed. Large temporary sources,
assembly and host tools live beneath TMPDIR, not the repository. Builder output
replacement checks its prior ownership/hashes before replacing anything. Failed
builds retain commands and logs. Native SDK staging verifies the actual package
and target archive before copying it.

## SDK and memory limit

The SDK exports CHOS::ICUData through `find_package(CHOSICUData CONFIG)`, and
standard ICU discovery now finds data+i18n+uc. The full archive is separate from
the original selected-resource package and explicit bootstrap target.

A real native u_init executable link attempt is **refused** by the existing
2MiB linker assertion. The SDK regression requires that exact diagnostic and
rejects unrelated unresolved-symbol failures. It does not replace app.ld or
remove the limit to turn this into a successful executable probe. The complete
package has not executed inside CHOS; the4MiB frame limit also needs a real,
bounded runtime expansion. Small external-resource fixtures retain their prior
qualified behavior. Ordinary libc remains its existing fixed-locale profile.

## Actual JavaScriptCore result

With the complete JSCOnly source/build-tool closure, the pinned upstream CMake
configuration now returns0 against the native SDK and real ICU data library.
Its report is CONFIGURED_NOT_BUILT, not an engine execution or browser result.
Initial attempts after resolving ICU exposed missing sparse-checkout bmalloc,
unifdef, gtest and header-map tools. These were checkout-completeness issues;
they were supplied from the same pinned Git commit. The preflight now checks
required roots and every tracked member's presence before starting CMake.
Only the existing checked CHOS CMake identity patch is applied.

The first actual2611-step native engine build stops on missing original libc++
__bit/byteswap.h and sys/mman.h. One bmalloc translation unit compiles; this is
not a complete allocator, WTF, JavaScriptCore or WebKit. Further code/runtime
gaps may follow these first errors. Complete image/heap budgets, OS APIs,
content isolation, rendering, browser features/security and live Pi WiFi remain
open. Neither the modern engine nor Stage3/version0.50.0 is complete.

See [machine-readable evidence](validation/stage3-icu-default-data.json).
The cc29ea8 Fusion test ISO is unchanged. New source/runtime qualification in
Fusion, Pi and laptops remains separate from QEMU and host build evidence.

The subsequent [larger native profile and generated ICU resource execution](NATIVE_LARGE_RESOURCES.md)
qualifies1MiB allocations,4MiB images and8MiB owned frames in both QEMU boot
modes. C++23 byteswap is now supplied and tested. The historical2MiB/4MiB and
missing-byteswap limits above describe earlier snapshots. The full32MiB embedded
data package remains over budget; JavaScriptCore still requires real virtual
memory APIs and further platform work. This is not Stage3 completion.

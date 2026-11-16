# Native libxml2 prerequisite

Original libxml2 2.15.4 now builds as an AArch64 static library against the
verified CHOS native SDK and runs selected parser tests in direct and CPU-isolated
UEFI QEMU boots. This is a dependency checkpoint in roadmap step 2. Full XML
conformance, WebCore integration, browser behavior and revised Stage 3 completion
remain unqualified.

## Source and transport policy

`third_party/libxml2` retains all 4,426 files (42,464,381 bytes) from the official
GNOME release, including its original `Copyright` and test inputs. The verifier
checks the exact inventory and every file hash against
[`dependencies/native-xml.json`](dependencies/native-xml.json). The release
archive SHA256 is
`98087fd181d9070724f3fbc65c7377db03038eb92bd882374daff44940138821`.

The GNOME checksum was obtained over verified TLS. Independent authentication
uses a signed Debian InRelease, its Sources.xz digest and the Debian original
archive digest. All 975 overlapping source files match byte for byte; there are
zero differing originals. This comparison authenticates those shared files;
the additional GNOME release files are pinned by the GNOME archive checksum.
The Debian metadata, signature-verification log, source-package digest and
comparison receipt are retained under `ports/xml/authentication`. No upstream
detached signature is claimed: the attempted GNOME `.asc` URL returned HTTP 404.

[`ports/xml/native-memory-io.patch`](../ports/xml/native-memory-io.patch) is
applied to an isolated build copy. The checked-in originals remain unchanged.
CHOS does not provide the POSIX open/dup descriptor transport required by this
version of xmlIO.c. Its native default pathname/fd transports therefore report
unsupported or return NULL, according to the API. The port supplies no fake
open/fcntl declaration or successful descriptor operation. Original memory,
FILE and caller-provided I/O callback implementations remain available.

The browser resource service must supply parser input and install an explicit
per-context external-resource policy. The fixture denies file and HTTPS DTD
requests, sets parser amplification bounds, uses NONET/NO_XXE where appropriate
and does not enable HUGE. These checks do not establish production safety for
all hostile XML.

## Built and exercised scope

The static archive contains 34 original implementation objects. Tree, SAX/push,
XPath, HTML, reader/writer, schema, pattern and serialization code are compiled.
Catalog, XInclude, dynamic modules, iconv, ICU conversion and zlib are disabled.
Threads and compiler-emulated thread-local storage use the real native runtime.
`LIBXML2_WITH_TLS=ON` selects thread-local storage; it is not an HTTPS/TLS
cryptography implementation. Native HTTP transport was removed upstream in
libxml2 2.15.

The selected runtime fixture exercises:

- Tree structure, namespaces, attributes, UTF-8, entities, XPath and memory serialization.
- Push input in 1-, 7- and 32-byte fragments and SAX2 events from one-byte input.
- Partial custom reads, injected read errors and exactly one I/O close callback.
- Malformed XML, malformed UTF-8, external file/HTTPS DTD denial and entity amplification rejection.
- Unsupported native fd constructors and three concurrent pthread workers,
  each repeating independent parsing and cleanup eight times.

The broader compiled HTML/schema/reader/writer interfaces have no full runtime
qualification in this checkpoint. Original upstream tests are retained but the
complete upstream conformance suite was not run.

## Reproduction

Activate the prepared compiler/tool environment and use new output directories:

```sh
source /workspace/chos-tools/activate
python3 tools/verify_native_xml_sources.py
python3 tools/build_native_xml.py --native-sdk /path/to/verified-sdk --out /path/to/new-native-build
python3 tools/build_native_xml.py --host --sanitize --out /path/to/new-host-build
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /path/to/new-host-build/application/native_xml
python3 tests/native_xml/qemu.py --mode direct --runs 3 --image-root /path/to/frozen-images --application /path/to/new-native-build/application/native_xml --out /path/to/new-direct-run
python3 tests/native_xml/qemu.py --mode uefi --runs 3 --image-root /path/to/frozen-images --application /path/to/new-native-build/application/native_xml --out /path/to/new-uefi-run
```

The prepared CMake and Ninja are installed outside the default PATH on this
machine. Pass their executable paths with `--cmake` and `--ninja` if needed.
`--image-root` contains `chos.elf`, `uefi-kernel.elf` and `chos.iso`; the harness
snapshots them and verifies the UEFI ISO's embedded kernel. It creates its own
disposable CHFS disk and firmware variables. The build report deliberately says
`LINKED_NOT_EXECUTED`; execution results are recorded separately.

## Recorded result

Evidence is retained outside the checkout at
`/workspace/chos-evidence/native-xml-20261112`. The reviewed record is
[`validation/stage3-native-xml.json`](validation/stage3-native-xml.json).

| Check | Result |
| --- | --- |
| Final source and authentication receipt inventory | PASS, 4,426 original files |
| Verified SDK6 and AArch64 archive/closed executable link | PASS, 34 archive members |
| Host3 ASan, UBSan and leak detection | PASS, exit 0 |
| Target3/direct4, three independent process executions | PASS, 110,116 cold free pages restored each time |
| Target3/uefi4, three independent process executions | PASS, 494,486 cold free pages restored each time |

The final native archive SHA256 is
`68c1fec22fcfd6f4a1bfef32edcda93b604359369f0bea38cade413bf6a726a5`;
the tested executable SHA256 is
`2703b2f6ba263a9e8f359ba7ecca91c1eaeef9089285e3729012e69306629377`.
Its prepared installation prefix is
`/workspace/chos-evidence/native-xml-20261112/target3/prefix`, with real CMake and
pkg-config metadata for the next WebCore dependency-discovery step.

After parser cleanup and worker joins, the fixture reports two allocations and
65,536 mapped heap bytes in main. These are the main thread's runtime TLS state,
which lives until CRT teardown after main returns. The harness does not assert
an empty heap at that point. Every process exits with status zero and restores
its complete cold physical-page baseline, zero committed engine frames, no
engine owner and zero futex waiters.

Preserved failed attempts include the initial original xmlIO.c compile requiring
unavailable `fcntl.h`, an early command missing CMake in PATH, and direct1's
harness reading an incomplete serial process row. The transport patch addresses
the actual unsupported descriptor interface; the harness now waits for the
process report's end marker and a complete memory-report line. Sandbox host3
leak inspection and direct3/uefi3 QMP socket creation were blocked by execution
restrictions. Approved runs outside that sandbox passed and their logs remain
alongside the failed attempts. Earlier host/target builds and direct2/uefi2 are
retained, but the result above qualifies only the final fixture and manifest.

Physical Fusion, Pi 5 and laptop XML execution, full XML conformance and actual
WebCore XML consumption remain open. This prerequisite does not satisfy the
modern browser or revised Stage 3 acceptance gates.

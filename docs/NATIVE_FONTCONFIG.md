# Native Fontconfig catalogue checkpoint

Fontconfig 2.17.1 now performs actual font selection on native AArch64 CHOS using
real FreeType faces, libxml2 configuration rules and pthreads. This is a page-build
prerequisite; WebCore rendering and the modern browser remain incomplete.

The [source manifest](dependencies/native-fontconfig.json) pins 618 original files
(2,960,594 bytes) from the archive authenticated through Debian's signed InRelease
and source-index checksum chain. Exact receipts remain under
`ports/fontconfig/authentication`; the offline verifier checks all original bytes
and retained receipts. The build never edits the vendored originals.

The [explicit port](../ports/fontconfig/README.md) supplies an application font
catalogue, not a POSIX filesystem or successful default scan. Twenty-five original
translation units provide pattern substitution, family/language/charset matching,
font sorting/listing/formatting and render patterns. Real CHOS file metadata and
snapshot `FILE` reads feed original FreeType font queries. The caller explicitly
loads font paths and installs its `FcConfig`; unconfigured global operations fail
with `ENOTSUP`. Default config files, directory scans, serialized caches and
watchers remain unsupported.

Direct and isolated UEFI QEMU each pass three complete lifetimes with exit 0.
Each run loads all five existing fixture fonts and checks Latin, Arabic U+0627 and
Devanagari U+0915 fallback, the XML `sans-serif` alias, charset coverage, real
selected `FC_FILE` face reopening, render size/antialias options, pattern format
conversion, font sorting/listing, three concurrent matching workers and teardown.
Only regular faces exist in this corpus: requesting bold/italic correctly falls
back to a regular face; real bold/italic catalogue selection is not qualified.
Missing files, unsupported directory/config loading, unconfigured global calls
and required malformed/truncated XML are checked too. Host ASan/UBSan and leak
checks pass on the same selected profile.

Six allocations occupying 64 KiB remain before native `main` returns after the
negative XML cases; the fixture records them rather than claiming zero heap.
An additional evidence-only native probe calls genuine `xmlCleanupParser` after
all XML/Fontconfig consumers stop: six allocations fall to two, identifying four
as libxml process/error state. The remaining emulated TLS survives until CRT
teardown. This global cleanup must not run while another XML consumer is active.
After normal CRT teardown and process reaping, physical pages return exactly to
110,116 (direct) or 494,486 (UEFI); engine ownership, commit ledgers and queued
futexes return to their cold baseline. See the [exact validation report](validation/stage3-native-fontconfig.json).

Review found and corrected two actual upstream/profile boundary problems: both
unconfigured default-loader retry paths now return failure instead of looping,
and the libxml2 complete-buffer parser now receives its final-input flag. Earlier
truncated XML acceptance and a bounded timeout reproduction are retained. Initial
native testing also exposed a fixture's unsupported 256 KiB worker-stack request;
it now uses the actual 128 KiB contract. A later malformed-input test incorrectly
used upstream's optional `complain=false` convention; required loads use `true`.
The resulting failure and fixture early-return leak report remain recorded.

Build from the prepared workspace:

```sh
source /workspace/chos-tools/activate
export PATH=/workspace/chos-tools/build-python/cmake/data/bin:/workspace/chos-tools/build-python/bin:$PATH
python3 tools/build_native_fontconfig.py \
  --out /workspace/chos-evidence/UNIQUE-FONTCONFIG \
  --native-sdk /workspace/chos-evidence/webcore-link-20261112/memory/sdk7 \
  --freetype-prefix /workspace/chos-evidence/native-fonts-20261107/final-jsc-text/prefix \
  --xml-prefix /workspace/chos-evidence/native-xml-20261112/target3/prefix \
  --gperf /workspace/chos-evidence/native-fontconfig-20261113/tools/usr/bin/gperf
python3 tests/native_fontconfig/qemu.py \
  --out /workspace/chos-evidence/UNIQUE-FONTCONFIG-DIRECT \
  --application /workspace/chos-evidence/UNIQUE-FONTCONFIG/application/native_fontconfig \
  --image-root /workspace/chos-evidence/native-cairo-20261108/clean-cairo-images
```

Use `--mode uefi` and a fresh output directory for UEFI. Host qualification uses
`--host --sanitize` with the existing host FreeType/XML prefixes, then runs the
fixture against `third_party/text-fixtures/fonts` with leak detection enabled.
The native archive is static and its package metadata preserves its actual
FreeType/XML dependency closure. A Cairo build must enable its real Fontconfig
backend before the required WebCore FreeType entry points are available.

Full Fontconfig/FreeType/XML conformance, allocation-pressure security,
WebCore/browser integration, physical Fusion/Pi 5/laptop qualification and revised
Stage 3 acceptance remain open. Development version remains 0.30.0.

# Native ICU normalization and IDNA data

Four original ICU79.1 resources now supply NFKC/NFKD, NFKC case folding,
NFKC simple case folding and UTS46 domain processing through the native file
loader. They are selected data resources, not the complete ICU/CLDR data library.
JavaScriptCore configuration still requires actual complete data; no bootstrap
archive is renamed to libicudata and no engine or browser success is inferred.

## Provenance and deployment

The [manifest](dependencies/icu-normalization-data.json) pins ICU commit
`ddc6b66926bfac6036144ca9902992a0d6d50af9`, original Git blob IDs, lengths and
SHA256 values for four resources, two full Unicode18 test corpora and the
Unicode License V3. The importer verifies Git blob identity before writing.
The verifier enforces exact membership, original hashes, little-endian/ASCII
Nrm2 headers, format5 and Unicode18 data version. No swapping or hand-written
replacement tables are necessary for these original little-endian resources.

The native SDK provides an explicitly named package:

```cmake
find_package(CHOSICUCommon REQUIRED CONFIG)
find_package(CHOSICUNormalizationData REQUIRED CONFIG)
target_link_libraries(app PRIVATE CHOS::ICUCommon CHOS::ICUBootstrapData)
# CHOS_ICU_NORMALIZATION_DATA_DIRECTORY is the host staging directory.
# Deploy its icudt79l subdirectory beneath the chosen guest data directory.
```

Applications must call `u_setDataDirectory` with the **guest** directory before
starting ICU workers. The tests deploy to `/ICU/icudt79l` and set `/ICU`.
An SDK host path is not a guest path. This package exposes paths, not an invented
full data target. The SDK installer verifies originals before staging and records
all staged bytes in its ownership manifest. Distribution/runtime integrity policy
for the future browser still needs integration; the ordinary ICU loader does not
cryptographically authenticate arbitrary CHFS files. Corrupt-header checks do not
establish safety for arbitrary malicious data payloads.

## Native qualification

The standalone application uses genuine common-library APIs and the real read
stream loader. Its fixture generator only encodes the upstream test columns,
documented escapes, UTF16 and field inheritance. It computes no expected Unicode
results through ICU, Python Unicode routines or the native implementation.
The original corpus files remain committed and hash-pinned.

The normalization suite checks all20 required transformations for each of the
20,171 official rows, then NFC/NFD/NFKC/NFKD identity for all293,187 other assigned
non-surrogate scalars not listed in Part1. Full and simple NFKC case folding have
additional selected cases; this is not their complete conformance suite.

All6,396 UTS46 rows execute ToUnicode nontransitional and ToASCII nontransitional
and transitional:19,188 UTF16 operations. The options match ICU79's original
UTS46 harness: STD3, Bidi, ContextJ and ContextO, with nontransitional flags where
required. The oracle checks error versus success and exact successful strings,
matching that harness. Its documented ICU-22882 exception allows an otherwise
valid trailing root dot for a sole A4_2 status. Error outputs and each individual
error bit are not qualified by that oracle. Another19,182 operations compare UTF8 and UTF16 API results, error bits and
transitional-difference flags for all6,394 scalar-valid source rows. The two
ill-formed UTF16 source rows remain in the UTF16 suite and are explicitly excluded
from UTF8 equivalence, since they have no valid UTF8 representation. Overlong,
surrogate, out-of-range, stray-byte and truncated UTF8 inputs are reported
disallowed in the concurrent lifecycle fixture. None of this integrates IDNA into browser navigation
yet or establishes DNS, HTTPS, origin or certificate behavior.

Missing resources and short, wrong-magic, wrong-endian and unsupported-format
headers fail. A child denied OPEN and READ cannot load external normalization
data. Three workers repeat selected normalization/casefold/IDNA calls through
three cleanup lifecycles. Real u_cleanup returns user heap allocations to zero;
the harness checks repeated process exit0, stable warm frames, timer progress
and no queued private futex waiters.

Two retained harness diagnoses are explicit. The first attempted a139,264-byte
bitmap allocation beyond the existing128KiB per-map ceiling. That fixed test
bitmap now occupies bounded BSS; no allocator or process quota was widened.
The initial permission test removed READ alone and incorrectly expected OPEN
itself to fail. OPEN has its own permission, so the corrected restricted child
removes both permissions. No kernel capability behavior was changed.

Both direct Make and ordinary SDK/CMake executables pass in native QEMU, the
latter under isolated UEFI. The full host regression suite passes. Product C
runtime code is unchanged; existing sanitizer evidence is retained, and no native
C++ sanitizer result is claimed.

See [machine-readable results](validation/stage3-icu-data.json) for accepted runs
and hashes. This remains QEMU evidence; new Fusion/Pi/laptop qualification is
NOT RUN. Full locale/CLDR resources and data packaging, engine/rendering services,
production browser security and the real Pi radio remain open. The cc29ea8 ISO
is unchanged and version0.50.0 remains reserved for complete revised Stage3.

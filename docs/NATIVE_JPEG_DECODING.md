# Native JPEG decoding

CHOS builds the original libjpeg-turbo 3.2.1 libjpeg API for isolated AArch64
applications with required NEON support. A native reference consumer exercises
integer decoding, RGB/grayscale, upsampling, scaling, progressive JPEG and
arithmetic JPEG against checked reference pixels.

This is a codec prerequisite. It does not add JPEG to the recovery browser or
implement WebCore image adapters, HTML/CSS layout or the modern browser. Stage 3
remains incomplete at 0.30.0. The [WebCore integration boundary](WEBCORE_PORT_BOUNDARY.md)
records the actual next engine component: original WebCore decoders and Cairo
backing stores, rather than another independent codec consumer. Current WPE
uses Skia and disables Cairo; a custom CHOS software port needs explicit work.

## Original dependency

This software is based in part on the work of the Independent JPEG Group.
The original [IJG notice](../third_party/libjpeg-turbo/README.ijg),
[libjpeg-turbo licensing terms](../third_party/libjpeg-turbo/LICENSE.md) and
per-file notices are retained. The host-only cjpeg/djpeg tools retain their
unmodified upstream sources and notices.

[The source manifest](dependencies/native-jpeg.json) pins all 619 original files,
licenses and test assets. The official source archive SHA256 matches the official
release asset digest. Its RSA/SHA256 detached signature was verified locally
with the key linked by the project's official signature policy. Original public
key, signature and verification output are retained in
[authentication materials](dependencies/jpeg-authentication/).

Original source bytes are unchanged. Native builds select the upstream
`NO_PUTENV` guard for an unused inline environment setter: CHOS has real getenv
but does not provide setenv. This does not fabricate a successful setter or
remove a dependency required by the selected libjpeg API. TurboJPEG and the
native command-line tools are excluded. The archive compiles more precision and
compression paths than this consumer exercises; compilation does not qualify
all of them.

The library uses upstream's actual no-backing-store memory implementation.
Temporary-file fallback is not supplied. The reference consumer interposes its
four platform allocation hooks at link time, charges a per-decoder budget and
then calls the original allocation/free routines. This instrumentation belongs
to the consumer; applications using the ordinary archive must supply their own
resource and error policy. It is not a modified upstream allocator or an
already integrated browser sandbox.

## Qualification

[The validation report](validation/stage3-native-jpeg.json) records the final executable, source and
reference hashes, actual guest results, host sanitizer results and retained
failures. The host tools use the generic implementation, while the target uses
AArch64 NEON. The selected reference corpus has 22 cases:

- 21 original upstream integer-IDCT pixel regressions, covering RGB/grayscale,
  fancy/merged upsampling, progressive inputs and twelve scaling ratios.
- One full decode of the original arithmetic-coded JPEG with an explicitly
  identified generic host reference, without claiming a historical golden for
  that additional case.

The generator checks the 21 original full-PNM MD5 regression values using both
its own comparison and upstream's comparator. These legacy values identify
regression output; they are not security authentication. Source provenance uses
the signed SHA256-pinned release. Complete raster SHA256/FNV64, tool identities
and exact decode settings are recorded, and encoded assets are frozen separately.
Binary PNM parsing consumes only the actual header delimiter so whitespace-valued
first samples are preserved.

The native consumer hashes every decoded sample and checks dimensions and
component count. It reads one output row at a time, so these tests do not address
WebCore's larger contiguous image-buffer requirement. A real suspending source
manager retains incomplete input and deferred marker skips. Strict errors and
warnings reject short input instead of accepting a synthesized end marker.

Arithmetic entropy decoding has an upstream `JERR_CANT_SUSPEND` boundary. Its
header still receives fragmented input; the consumer waits for all bounded
encoded input before starting that entropy decoder. This is explicit buffering,
not asynchronous arithmetic entropy support. Pixel comparisons remain exact.

Mutable recovery state and the allocation ledger are heap-owned. Native nonlocal
recovery stays within the current thread. After libjpeg destruction, every
tracked platform allocation must have been released. The consumer bounds encoded
input to 2 MiB, source/output dimensions to 1024×768, charged codec allocations to
8 MiB and the ledger to 512 entries. Its small context and single output row are
additional to that charged budget. The tests inject failure at every platform
allocation of a successful selected decode, separately for ordinary and
fragmented delivery, and exercise truncation, malformed dimensions, trailing
input and budget exhaustion. Three real pthread workers use independent states.

Both direct and UEFI QEMU pass three full process lifecycles, each checking all
22 ordinary decodes, 88 fragmented deliveries and the failure/worker tests.
Every run exits normally with exact cold physical-page restoration, released
engine ownership, zero committed frame reservations and zero futex waiters.
The consumer checks zero live native heap allocations and mapped bytes before
exit. Its largest total charged codec footprint in the selected native corpus
is 143,141 bytes. The host consumer passes address, undefined-behavior and leak
sanitizers. All 212 original upstream host tests also pass with those sanitizers;
the complete repository host/sanitizer suites pass. The e1000e UEFI regression
passes actual DHCP/DNS/HTTP, in-flight link-loss cancellation and reconnection.

The initial native and sanitized-host fragmented arithmetic attempts failed with
upstream `JERR_CANT_SUSPEND`. Those failures are retained in the validation report;
the bounded complete-input path fixes the actual library contract, without
skipping arithmetic cases or loosening the exact sample comparisons.

This is selected 8-bit sample-space qualification. It does not establish color
management, EXIF display orientation, all image precisions, encoder correctness,
full native upstream conformance, fuzzing coverage or production hostile-content
isolation. New Fusion, physical Pi 5 and laptop execution remain untested.

## Reproduce

Use the prepared Clang/LLD, CMake and Ninja environment and a verified
[native SDK](NATIVE_SDK.md). Each output directory must be fresh.

```sh
python3 tools/verify_native_jpeg_sources.py
python3 tools/build_native_jpeg.py --host --sanitize --out build/host-jpeg
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/host-jpeg/application/native_jpeg
python3 tools/build_native_jpeg.py --native-sdk build/native-sdk --reference-build build/host-jpeg/jpeg --out build/native-jpeg
python3 tests/qemu_native_jpeg.py --mode direct --application build/native-jpeg/application/native_jpeg --cases build/native-jpeg/cases/cases.json --out build/jpeg-direct
python3 tests/qemu_native_jpeg.py --mode uefi --application build/native-jpeg/application/native_jpeg --cases build/native-jpeg/cases/cases.json --out build/jpeg-uefi
```

The host builder runs the original upstream core tests before generating the
corpus. The optional `make native-jpeg` target consumes a completed host reference
build through `NATIVE_JPEG_REFERENCE`. Freeze ordinary kernel/ISO inputs during
guest qualification; the runner accepts `--image-root` for frozen `chos.elf`,
`uefi-kernel.elf` and `chos.iso`. All test disks and firmware variables are disposable.

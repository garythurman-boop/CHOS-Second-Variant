# Native WebP decoding and animation

Original libwebp 1.6.0 now supplies an AArch64 CHOS memory-codec prefix with
qualified decoding, incremental input, demuxing and animation. This is a WebCore
build dependency: it does not yet execute WebCore's WebP adapter or render a page.
Revised Stage 3 remains incomplete at development version 0.30.0.

## Source and package

The [source manifest](dependencies/native-webp.json) pins all 367 original files,
including licenses and patent notices, and four retained authentication receipts.
The [verifier](../tools/verify_native_webp_sources.py) rejects changed, missing,
extra or symlinked original sources and checks the receipt digests. Debian's
archive keyring verified the signed sid InRelease; its SHA256 authenticates
Sources.xz, whose source record authenticates the original release archive:
`e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564`.
The detached upstream signature is retained but is **not independently verified**;
this checkpoint relies on the authenticated Debian source chain.

The [builder](../tools/build_native_webp.py) copies the exact source into a new
output directory and invokes upstream CMake through the explicit
[memory-library profile](../ports/webp/CMakeLists.txt). No original source patch,
compiler-probe bypass or replacement codec is used. Static owned-executable code
generation disables PIC for the native targets. Actual upstream compiler probes
must detect AArch64 NEON and actual Threads support. The SDK and installed package
inventories are hashed before and after the build.

The prefix contains `libwebp.a` (112 members), `libwebpdecoder.a` (64),
`libwebpdemux.a` (2) and `libsharpyuv.a` (7), original public headers, pkg-config
metadata, and upstream CMake exports under `share/WebP/cmake`. Every native
archive member is AArch64. The actual consumer uses the installed
`WebP::webpdemux` target and its transitive WebP, SharpYUV and Threads dependencies.
It has no unresolved symbols, writable-executable segment or ELF TLS segment.

The original encoder and SharpYUV code compile for CHOS, but **native encoder
execution is not qualified**. A separately compiled host encoder creates the
fixtures; the native application decodes and demuxes them. Mux, command-line
programs, file/image transports and graphics utilities are not built. This is
not a complete upstream tools distribution.

## Executed checks

The [validation report](validation/stage3-native-webp.json) records exact source,
SDK, package, executable, generated-case, frozen-image and result hashes.

- Three 19 × 13 RGBA cases cover two lossless images and one lossy image with
  alpha. Lossless expected bytes come directly from an independent integer pixel
  formula. The lossy expected bytes come from the original separately compiled
  host decoder, not an independent implementation. Caller-owned output strides,
  row padding, trailing canaries and a deliberately undersized output are checked.
  Each process completes 1,482 full-decode pixel comparisons; incremental and
  animated outputs receive additional complete byte comparisons.
- Incremental append with 1- and 7-byte fragments, update with 11-byte fragments,
  row visibility, and cancellation partway through input complete 1,171 deliveries.
  Static frame extraction and 124 partial-demux observations are also checked.
- A manually assembled two-frame RIFF animation verifies canvas size, three-loop
  metadata, frame durations, timestamps 80/200 ms, complete RGBA composition and
  reset/replay. The frames fully replace the canvas; this does not qualify every
  animation blend/disposal combination.
- Selected truncated data, bad RIFF signatures, impossible RIFF lengths and
  output bounds fail correctly. Every allocation reached by the selected
  lossless-decoder and animated-demux paths is failed in turn: 27 injected
  failures, each with an empty allocation ledger after cleanup and successful
  subsequent decoding. This is not exhaustive malformed-content coverage.
- Three actual pthread workers each run twelve decodes. The allocation-failure
  ledger is empty and disabled before concurrent work; inactive wrappers bypass
  bookkeeping. Native heap allocations and mapped bytes are zero after join.
- Direct and UEFI QEMU each pass three process lifetimes with exit 0, exact cold
  physical-page restoration (110,116 and 494,486 pages), released engine ownership,
  zero committed frame reservations and zero futex waiters. The harness waits for
  complete process reports before checking exit status. Each run uses disposable
  media and firmware variables with frozen, hash-checked kernels and ISO.
- Separately built host consumers pass ordinary execution and GCC AddressSanitizer,
  UndefinedBehaviorSanitizer and LeakSanitizer with leak detection and halt-on-error
  enabled. The sanitized fixture generator also runs under the sanitizer build.

Retained failures include the initial host CMake Python wrapper, a native fixture compile,
LeakSanitizer's ptrace restriction, QEMU socket creation denied by the sandbox,
and the first native fixture's emulated-TLS recursion in its malloc interposer.
That fixture ledger was changed to ordinary serial storage; original codec and
runtime sources were unchanged. The final inactive realloc/free paths also
bypass all ledger bookkeeping so future concurrent calls cannot modify it.
A later fixture change switched the consumer from manual archive paths to the
real installed CMake export; the final host and native qualification uses that
export. Earlier failures and superseded passing runs remain in the evidence.

These are selected codec and lifecycle checks, not full upstream conformance,
fuzzing, a hostile-content security assessment or physical-platform acceptance.
New Fusion, Pi 5 and ARM64 laptop execution remains unrun for this component.
WebCore adapter, browser and full Stage 3 acceptance remain separate work.

## Reproduce

Use fresh output paths, the prepared tools, and an immutable verified SDK:

```sh
source /workspace/chos-tools/activate
export PATH=/workspace/chos-tools/build-python/cmake/data/bin:/workspace/chos-tools/build-python/bin:$PATH
python3 tools/build_native_webp.py --host --out build/webp-host
build/webp-host/application/native_webp
python3 tools/build_native_webp.py --host --sanitize --out build/webp-sanitize
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/webp-sanitize/application/native_webp
python3 tools/build_native_webp.py --native-sdk build/native-sdk --host-build build/webp-host --out build/webp-native
python3 tests/native_webp/qemu.py --mode direct --application build/webp-native/application/native_webp --cases build/webp-native/cases/cases.json --image-root /path/to/frozen/images --out build/webp-direct
python3 tests/native_webp/qemu.py --mode uefi --application build/webp-native/application/native_webp --cases build/webp-native/cases/cases.json --image-root /path/to/frozen/images --out build/webp-uefi
```

The frozen directory must contain `chos.elf`, `uefi-kernel.elf` and `chos.iso`.
Sanitizer execution requires an environment that permits LeakSanitizer to run;
QEMU must be permitted to create its private QMP Unix socket. Do not disable
these checks to obtain a passing result. Cross consumers can select the actual
installed target with `find_package(WebP CONFIG REQUIRED PATHS
"${WEBP_PREFIX}/share/WebP/cmake" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)`.

# Native zlib memory streams

The CHOS AArch64 dependency prefix now supplies original zlib 1.4 memory APIs,
including real default allocation, compression utilities, and raw deflate, zlib
and gzip stream formats. This extends the earlier PNG prerequisite's nine-unit
`Z_SOLO` profile. It supplies the dependency needed by WebCore compression
streams; this qualification does not execute WebCore or establish page rendering.
Revised Stage 3 remains incomplete at development version 0.30.0.

## Source and profile

The existing [authenticated source manifest](dependencies/native-png.json) pins
all 263 original zlib files. The existing
[verifier](../tools/verify_native_png_sources.py) checks every original file,
license and retained authentication artifact before and after each build.
The upstream archive SHA256 matched the official release digest over TLS and its
detached signature was verified locally. Its legacy DSA/SHA1 signature alone is
not a modern strong-digest authentication claim. No vendor original is modified.

The [build driver](../tools/build_native_zlib.py) copies that verified source to
a fresh output directory and applies the dated
[memory API patch](../ports/zlib/memory-api.patch) with zero fuzz. Only `zutil.c`
differs. The patch excludes its `gzguts.h` include when the private
`CHOS_ZLIB_MEMORY_API` definition is enabled; it changes no allocator or codec
implementation. `gzguts.h` supplies the unrelated file implementation's
`fcntl.h`, POSIX feature declarations and legacy platform adaptations.

The actual AArch64 preprocessor audit retains original `STDC`, `STDC99`,
`HAVE_MEMCPY`, 32-bit `Z_U4` and 64-bit `Z_U8`. The native SDK supplies the real
stdlib, string, limits and sys/types interfaces. Original `z_off_t` is an LP64
`long`, and the gzip OS byte retains upstream's default value 3. No legacy
Windows/16-bit, dynamic CRC-table or runtime-built Huffman-table path is selected.
The memory units require no macros or file helpers from the omitted include.
Original `zcalloc` and `zcfree` retain upstream's malloc/calloc selection, using
actual native malloc and free on this LP64 target. `Z_SOLO`, `NO_GZIP` and
`NO_GZCOMPRESS` are absent.

The archive includes these eleven original translation units:
`adler32`, `compress`, `crc32`, `deflate`, `infback`, `inffast`, `inflate`,
`inftrees`, `trees`, `uncompr` and `zutil`. This retains the upstream memory API,
including its size_t convenience variants. It intentionally omits `gzFile` and
filesystem entry points. Original public headers still describe those upstream
APIs; the consumer's negative link probe requires actual unresolved `gzopen` and
`gzclose` symbols. No fcntl/open/gz stubs, file success substitutes, or new runtime
or kernel APIs are introduced. Gzip memory streams remain fully enabled.

The installed prefix provides `lib/libz.a`, original `zlib.h` and `zconf.h`,
the upstream license, `lib/pkgconfig/zlib.pc`, and a CMake package exporting
`ZLIB::ZLIB`. The package declares `CHOS_ZLIB_MEMORY_API=TRUE` to identify its
boundary. The private build definition does not propagate to consumers.
Every native archive member is AArch64; the native consumer has no unresolved
symbols. Host archives are built independently and cannot satisfy the native link.

## Executed qualification

The [validation report](validation/stage3-native-zlib.json) records exact source,
SDK, archive, executable, fixture and guest evidence hashes.

- Nine independent Python host-zlib 1.3.2 vectors cover empty input, binary text,
  and a 49,152-byte mixed payload crossing the deflate window, each in raw, zlib
  and gzip form. Native decompression compares every byte with the original
  payload. The host reference is not linked into the tested executable.
- Each process completes 81 stream checks, including 54 fragmented deliveries.
  Compression and inflation use input/output windows of 1/1, 7/13, 257/61 and
  65,536/65,536 bytes. Default callbacks are populated by upstream initialization.
  Compression utilities and repeated deflate/inflate reset/reuse also pass.
- Selected truncated streams, bad Adler32/CRC32 checksums, incompatible wrappers,
  utility output bounds and trailing-input policy produce 59 rejection checks.
  Automatic zlib/gzip detection also passes. Raw deflate deliberately has no
  wrapper checksum and cannot reject every arbitrary byte sequence.
- All allocations reached by the selected mixed-payload compression and inflation
  paths are failed in turn for each format: 21 failures per process, each returning
  `Z_MEM_ERROR` with an empty allocation ledger after cleanup.
- Three actual pthread workers complete 36 rounds of fragmented decode, encode,
  decode and truncated input. After join, native heap allocations and mapped bytes
  are zero.
- Direct and UEFI QEMU each pass three process lifecycles with exit 0, exact cold
  physical-page restoration, zero committed frame reservations and futex waiters,
  and released engine ownership. Kernels and ISO are frozen qualified snapshots;
  guest disks and firmware-variable copies are disposable.
- The separately compiled host fixture passes GCC address, undefined-behavior and
  leak sanitizers with `detect_leaks=1` and halt-on-error enabled. A CMake negative
  link probe confirms that the installed memory package supplies no gzFile API.

Retained setup failures include a Python CMake wrapper without its module path,
the prepared Clang installation without host ASAN runtime archives, and the first
native consumer's package search being rooted into the immutable SDK. The final
run uses the actual CMake binary, installed GCC sanitizer runtimes and an explicit
external-prefix package search. The first sandboxed LeakSanitizer launch could
not run under ptrace; the qualified launch runs outside that tracing sandbox.
These are recorded failures, not successful codec checks.

This is selected memory API qualification, not the complete upstream conformance
suite, a fuzzing campaign or a production hostile-content security assessment.
WebCore CompressionStream integration and full page behavior remain separate
work. No new Fusion, physical Pi 5 or laptop execution is claimed.

## Reproduce

Use the prepared toolchain, an immutable verified native SDK, and fresh outputs.
The full commands and exact qualified paths are retained in the report.

```sh
source /workspace/chos-tools/activate
export PATH=/workspace/chos-tools/build-python/cmake/data/bin:/workspace/chos-tools/build-python/bin:$PATH
python3 tools/build_native_zlib.py --native-sdk build/native-sdk --out build/native-zlib
python3 tools/build_native_zlib.py --host --sanitize --out build/host-zlib
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/host-zlib/application/native_zlib
python3 tests/native_zlib/qemu.py --mode direct --application build/native-zlib/application/native_zlib --image-root /path/to/frozen/images --out build/zlib-direct
python3 tests/native_zlib/qemu.py --mode uefi --application build/native-zlib/application/native_zlib --image-root /path/to/frozen/images --out build/zlib-uefi
```

The frozen image directory must contain `chos.elf`, `uefi-kernel.elf` and
`chos.iso`. Cross consumers link `ZLIB::ZLIB`. For an external target prefix,
use `find_package(ZLIB CONFIG REQUIRED PATHS "${ZLIB_PREFIX}/lib/cmake/ZLIB"
NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)` so CHOS's root-only package search does
not prepend the SDK path. The prefix may also be passed through the platform's
existing FindZLIB library/include settings.

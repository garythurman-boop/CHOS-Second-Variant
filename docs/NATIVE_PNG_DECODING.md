# Native PNG image decoding

CHOS now builds upstream libpng 1.6.59 and zlib 1.4 for its isolated AArch64
application ABI. A native consumer decodes the original PNGSuite corpus and
incremental input with real allocation, error recovery and worker threads.
The existing SDK supplies the actual native nonlocal-jump implementation.

This supplies another WebCore dependency. The modern browser does not yet use
these libraries, and this checkpoint does not implement HTML/CSS/DOM, browser
image caching or page rendering. Stage 3 remains incomplete at version 0.30.0.
The preceding [Cairo](NATIVE_CAIRO_RENDERING.md) and
[text](NATIVE_TEXT_RENDERING.md) checkpoints retain their separate evidence.

## Sources and build

[The source manifest](dependencies/native-png.json) pins all 263 original zlib
files, 614 original libpng files and the two independent host-reference files.
Original implementation and license bytes are retained unchanged. zlib retains
its [license](../third_party/zlib/LICENSE); libpng retains its
[license](../third_party/libpng/LICENSE); the host-only PyPNG reference retains
its [MIT notice](../third_party/png-reference/LICENCE).

The libpng signed release tag was verified locally against its maintainer's
public key and targets the exact pinned source commit. The zlib archive SHA256
matched the official release asset digest over TLS; its detached signature was
also verified locally. zlib's upstream signature uses legacy DSA/SHA1, so that
signature alone is not a modern strong-digest authentication claim. Public keys,
original signatures and local verification transcripts are retained in
[authentication materials](dependencies/png-authentication/). Key origins and
fingerprints are recorded in the manifest. No signature or TLS check was bypassed.

The offline build verifies original inventories, port sources and the SDK before
and after compilation. Target archives contain 18 AArch64 libpng members,
including the upstream NEON implementation, and nine zlib memory-core members.
The executable has no unresolved symbols. Host and target dependencies are built
separately; a host library cannot satisfy the native link.

The native zlib profile uses upstream's documented `Z_SOLO` configuration with
real caller-supplied allocation callbacks. It supplies the memory-stream core;
`gzFile`, default allocators and compress/uncompress convenience APIs are excluded.
The full file profile previously stopped at the absent native `fcntl.h` interface.
No successful file operation or POSIX declaration was fabricated to pass that build.
Selected PNG paths exercise inflation; the entire compression API is not qualified.

libpng's documented prebuilt configuration header avoids an upstream custom
preprocessing command that omitted the cross compiler's target argument. This
is an upstream-supported cross-build input, not invented capability results.
The small [UTC patch](../ports/png/native-utc.patch) uses native `gmtime_r` with a
caller-owned calendar buffer. It applies to a fresh build copy and carries an
explicit modification notice. Invalid time conversion retains upstream behavior.

## Executed checks

The [validation report](validation/stage3-native-png.json) records actual native
build hashes, the independent reference cases, host results, guest results and
retained failed attempts.

- All 51 original PNGSuite images, upstream's interlaced 91×69 `pngtest.png`
  and a generated 1024×768 RGBA image match independent PyPNG reference values.
  Every decoded RGBA8 byte contributes to the checked hash; reference manifests
  also retain complete-output SHA256 values. Tests cover grayscale, palette,
  RGB, alpha, transparency and selected 1/2/4/8/16-bit and interlaced inputs.
- Progressive callbacks perform 209 additional image deliveries per process:
  the 52 smaller images at 1, 7, 64 and 1024-byte fragments, and the large image
  at 1024-byte fragments. Adam7 rows are combined through the real libpng API.
- Both entry points reject the tested damaged data, truncated streams, CRC
  failures, dimension/input bounds and memory exhaustion. Every allocation in a
  successful `pngtest.png` decode is failed in turn, separately for classic and
  progressive decoding. Progressive completion rejects trailing bytes.
- A compressed-stream checksum is damaged while repairing its PNG chunk CRC.
  This requires the actual inflater, rather than the chunk CRC check, to reject
  corruption. Width/height limits are likewise tested with valid chunk CRCs.
- Three actual native pthread workers alternate classic and progressive decodes
  and malformed input. Native UTC conversion is tested concurrently, including
  pre-epoch, leap-day, 2038 and out-of-range inputs.
- Direct and UEFI QEMU each complete three process lifecycles with exit 0,
  exact cold physical-page restoration, zero committed frame reservations,
  released engine ownership and zero futex waiters. The consumer also checks
  zero live native heap allocations and mapped bytes before exit.
- The separately built host consumer passes address, undefined-behavior and
  leak sanitizers. The complete repository host and sanitizer suites pass.
  The Fusion-relevant e1000e QEMU regression passes actual DHCP/DNS/HTTP,
  cancellation during link loss and reconnection in the same browser process.

The reference policy preserves encoded sample values, scales 16-bit samples to
8 bits, expands transparency and supplies opaque alpha where needed. It applies
no sBIT rescaling, ICC/gamma transform or background composition. PyPNG's default
convenience conversion initially applied an extra significant-bit rescale to
`pngtest.png`; the oracle now explicitly selects the same encoded-sample policy.
The original input and decoder remain unchanged, with exact hash comparisons.

## Recovery policy and bounds

The reference consumer owns mutable recovery state on the heap so a native
`longjmp` cannot leave changed automatic state indeterminate. Each decode has its
own recovery target and allocation ledger; jumps never cross threads. Cleanup
checks accounting, destroys libpng state and releases remaining output rows on
normal, corrupt-input and allocation-failure paths.

The consumer limits encoded input to 2 MiB, dimensions to 1024×768, tracked
allocations to 1024, charged decode allocations to 8 MiB, ancillary chunk
allocations to 64 KiB and the chunk cache count to 32. Its small context and
caller-owned encoded input are additional to the charged decode budget.
Output rows are allocated separately. The native large-image peak is 3,209,866
charged bytes, without expanding CHOS's existing per-allocation bound.
Large contiguous WebCore image buffers remain separate work.

The first corrupted-stream test exposed libpng's permissive progressive error
handling after the final row. Making benign errors fatal alone did not cover its
ordinary-warning branch. The final consumer makes both benign errors and ordinary
warnings fatal; the damaged stream is now rejected and all selected valid images
pass. This strict policy may reject images with otherwise recoverable metadata.
It is an explicit consumer policy, not a change to upstream global defaults or
proof that a future browser has adopted it. Failed attempts and their diagnosis
remain in the validation report. LeakSanitizer also caught a temporary test-buffer
leak on the failing test's early return; the final successful fixture has no leaks.

This is selected codec qualification, not the full upstream test suite, a fuzzing
campaign, production hostile-content isolation, color management or APNG playback.
Other image formats and full WebCore bindings remain open. New Fusion, physical
Pi 5 and laptop execution were not run. Existing user-confirmed Fusion Ethernet
support remains distinct from the automated QEMU regression evidence.

## Reproduce

Use the prepared Clang/LLD, CMake and Ninja environment and an installed native
SDK as documented in [the SDK guide](NATIVE_SDK.md). Each output directory must
be fresh so previous evidence is retained.

```sh
python3 tools/verify_native_png_sources.py
python3 tools/build_native_png.py --native-sdk build/native-sdk --out build/native-png
python3 tools/build_native_png.py --host --sanitize --out build/host-png
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 build/host-png/application/native_png
python3 tests/qemu_native_png.py --mode direct --application build/native-png/application/native_png --cases build/native-png/cases/cases.json --out build/png-direct
python3 tests/qemu_native_png.py --mode uefi --application build/native-png/application/native_png --cases build/native-png/cases/cases.json --out build/png-uefi
```

`make native-png` also builds the optional native dependency and consumer through
the existing SDK export target. Synchronize ordinary kernel/ISO inputs before
running the guest matrix, then freeze them throughout qualification. The guest
runner accepts `--image-root` for frozen `chos.elf`, `uefi-kernel.elf` and
`chos.iso` inputs. It always uses disposable CHFS disks and firmware variables.
This source checkpoint does not replace the published Cairo user-test binaries.

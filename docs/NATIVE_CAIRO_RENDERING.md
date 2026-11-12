# Native software graphics

The isolated CHOS application ABI now builds and links Cairo 1.18.4 and pixman
0.44.0 with the native FreeType dependency. A real Cairo client draws gradients,
clipped dashed curves, transformed groups with alpha, repeating image patterns
and FreeType glyphs into owned window images. Its workers use real native
pthreads; pixman includes the original AArch64 NEON implementation.

This is graphics infrastructure for a proposed custom CHOS WebCore port. Current
WPE uses Skia and explicitly disables Cairo; see the verified
[WebCore integration boundary](WEBCORE_PORT_BOUNDARY.md). This checkpoint does not
implement WebCore, HTML/CSS/DOM,
Canvas, page navigation or the modern browser. Cairo's low-level glyph API does
not shape text; the separate [HarfBuzz/ICU backend](NATIVE_TEXT_RENDERING.md)
qualifies selected shaping cases. Stage 3 remains incomplete at version 0.30.0.

## Original sources and selected platform

[The dependency manifest](dependencies/native-graphics.json) pins every original
implementation/build/license file. Archives were authenticated through Debian's
signed InRelease, its Sources digest and the original archive digests. Official
release-listing failures are retained in the evidence rather than bypassing TLS
or signature checks. Builds are offline and reject inventory changes.

Cairo retains its original LGPL 2.1/MPL 1.1 alternatives and per-file notices;
pixman retains its MIT-style notices. **This software uses FreeType**, under
the retained [FreeType License](../third_party/freetype/docs/FTL.TXT).
The unmodified Noto font asset retains its OFL notice and copyright metadata.

The selected profile enables software image, recording and observer surfaces
and explicit FreeType faces. Fontconfig, PNG decoding/encoding, PostScript/PDF/SVG
output, display-server bindings and GPU acceleration are not supplied by this
profile. Full upstream tests, color-font behavior and hostile-font isolation
remain separate work. Archive compilation does not certify every Cairo API.

Two explicit, reviewable patches apply to a fresh build copy; the vendored
original files remain byte-identical:

- [PostScript spool gating](../ports/graphics/cairo-postscript-spool.patch) compiles
  the private temporary-file helper only when its sole PostScript caller is
  enabled. The current profile disables that backend. No temporary-file API or
  successful file operation is fabricated.
- [Bounded debug formatting](../ports/graphics/cairo-bounded-debug.patch) uses the
  existing snprintf interface and the actual remaining debug-buffer capacity.

The native toolchain links real executable probes with the complete CRT and
runtime. Meson groups static libraries itself: a prototype's nested group flags
made thread/atomic probes fail. The corrected build requires genuine pthread
and C11 atomic support and never selects Cairo's no-mutex mode. Native package
lookup is restricted to explicitly verified target prefixes. A host library
cannot satisfy a target dependency.

## Native libc prerequisites

`strdup` and `strndup` return independently allocated, terminated copies through
the existing allocator. Bounded copies do not require a terminator within their
maximum input length. Allocation failures preserve the allocator's error result;
overflow is rejected. A real static recursive-mutex initializer uses the existing
recursive backend, including owner checks and nesting.

`rand`/`srand` provide a deterministic **noncryptographic** musl-compatible sequence
with atomic per-process state, avoiding a C data race between workers. Cairo uses
rand to choose a cache entry. TLS, credentials and security tokens continue to
require the existing real OS entropy service. This API is not an entropy fallback.

The alloca header exposes the compiler's actual stack allocation builtin. Storage
belongs to the current function and is subject to stack limits/guards. It cannot
be freed or retained after return. It does not add a heap or stack-growth service.

## Verification boundary

The native stdlib fixture checks ownership and actual inaccessible-page boundaries:
a nonterminated input ends immediately before a PROT_NONE page; a zero-length
copy begins on that page. It checks seeded replay and the exact multiset of 6000
values produced by three real workers for four seeds, static recursive locking
and aligned compiler stack storage. Separate host tests inject allocation failure
without interposing the host/sanitizer ABI.

The Cairo fixture checks exact clipping, integer source-over alpha, row padding
and canaries, recording bounds, matrix round trips and real error results. Three
workers render through separate FreeType faces and Cairo's shared internal caches.
All workers finish before cache teardown and caller-owned FreeType face release.
The host reference runs address, undefined-behavior and leak checks independently
from native execution.

In live guests, the runner compares every pixel of three 512x256 frames with that
reference, requires physical Space redraws, normal process exits and exact cold
physical-page, committed-quota, engine-owner and futex cleanup. It also runs the
stdlib guard/thread fixture in each cycle. Results, exact hashes and retained
investigation failures are in [the validation record](validation/stage3-native-cairo.json).
QEMU execution does not establish new Fusion, Pi 5 or laptop support.

Redraw measurements include emulator scheduling, key delivery and complete tiled
image upload. They are not browser FPS or page-navigation benchmarks. The GUI
transport remains the existing copied opaque-image interface; its fixed buffer
cost and production optimization work are documented in
[the surface contract](NATIVE_PIXEL_SURFACES.md).

## Reproduction

Use a complete current SDK and a verified native font build. The font library may
have been compiled with the preceding compatible SDK; its exact compilation SDK
and archive bytes are recorded separately from the client's SDK. Recovered old
SDKs and previously qualified images are never modified in place.

```sh
source /workspace/chos-tools/activate
make CC=clang LD=ld.lld -j4 native-sdk
python3 tools/build_native_graphics.py --native-sdk build/native-sdk --font-build /path/to/native-font-build --out /new/native-graphics
python3 tools/build_native_graphics.py --host --sanitize --font-build /path/to/sanitized-host-font-build --out /new/host-graphics
mkdir /new/host-frames
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /new/host-graphics/application/native_cairo third_party/text-fixtures/fonts/NotoSans-Regular.ttf /new/host-frames
```

Provide Meson, Ninja and CMake in PATH or use their explicit tool arguments.
The prepared environment uses Meson 1.12.1; its verified wheel is recorded in
the dependency manifest. Build the stdlib probe with
`tests/native_stdlib/CMakeLists.txt` against the same SDK, stage the graphics ELF
with `tools/prepare_native_engine.py`, then build and freeze `engine-test-images`.
Run `tests/qemu_native_cairo.py` with explicit `--image-root`, `--application`,
`--stdlib`, `--reference`, a fresh `--out` and `--mode direct` or `--mode uefi`.
Never rebuild the frozen qualification inputs during the guest matrix.

The diagnostic executable occupies `/BIN/JSCVM.ELF` only on the separate
diagnostic image. Its window is **CHOS Native Cairo**; Space changes the drawing
and Escape/close exits. That executable is a Cairo client, not the JavaScript
standards runner. Diagnostic media have no installer payload. Normal installation
media and previous downloads remain separate. A production browser still needs
full engine/font selection/image/XML/storage/platform integration and its security,
wireless and independent hardware acceptance gates.

# Native WebCore integration boundary

The existing native runtime runs JavaScriptCore and separate text, Cairo and
image-codec consumers. It does **not** build or run WebCore, construct a DOM,
perform CSS layout or paint a web page. The full modern browser remains open.
This document records a source inspection and the next executable integration
boundary; none of the proposed CHOS WebCore files or targets below is implemented.

## Pinned source and graphics choice

The inspected checkout is WebKit 2.54.2, commit
`32717fabcb9dcad9aa00a54ef1f23e41ee64e849`. Paths below are relative to that
upstream tree, held locally at `/workspace/chos-reference/webkit-2.54.2`.
Its sparse checkout currently contains JavaScriptCore, WTF, WebCore, CMake and
Tools/CMake. `Source/ThirdParty/skia` and `Tools/TestWebKitAPI` exist in the pinned
Git tree but are outside the current sparse checkout; their absent working-tree
directories do not mean upstream lacks them.

The current **WPE** port uses Skia:

- `Source/cmake/OptionsWPE.cmake` defaults `USE_SKIA` to `ON` and explicitly
  exposes `USE_CAIRO` as `FALSE`.
- `Source/WebCore/PlatformWPE.cmake` includes `platform/Skia.cmake`, GLib source
  lists, Soup, GStreamer, GCrypt and the WPE platform adapters.
- OptionsWPE requires target GLib/GioUnix/Thread/Module, Soup3, Epoxy, LibGcrypt,
  Tasn1, XkbCommon, LibXml2, SQLite and WebP, in addition to the libraries already
  being ported. Native MbedTLS tests do not supply Soup's implementation or its
  network/process integration.

Consequently, the existing standalone Cairo checkpoint is useful infrastructure
for a **custom CHOS software WebCore port**. It is not evidence that current WPE
accepts Cairo, or that adding codecs completes a WPE browser link. Keeping the
stock WPE path would require its actual Skia and GLib/Soup/platform dependencies.

Cairo remains an upstream-supported WebCore backend for other ports. Reuse the
actual portable implementation rather than writing a new renderer:

| Upstream file | Reusable implementation and limits |
|---|---|
| `Source/WebCore/platform/Cairo.cmake` | Selects `platform/SourcesCairo.txt`, Cairo headers and `Cairo::Cairo` |
| `Source/WebCore/platform/SourcesCairo.txt` | Real GraphicsContext, paths, gradients, image buffers and native images; the entire list still needs target compilation and linking |
| `Source/WebCore/platform/FreeType.cmake` | Selects FreeType/HarfBuzz font implementation, but also unconditionally requires Fontconfig |
| `Source/WebCore/PlatformPlayStation.cmake` | Demonstrates current Cairo/FreeType selection, but also imports Curl/OpenSSL, EGL/libwpe, Unix shared memory and PlayStation behavior; it cannot be inherited wholesale |
| `Source/WebCore/platform/ImageDecoders.cmake` | Selects original PNG/JPEG/BMP/GIF/ICO/WebP decoders and the Cairo image-backing adapter; requires target WebP demux as well as PNG/JPEG |

The upstream FreeType backend is not satisfied by linking standalone FreeType
and HarfBuzz archives. `FontCustomPlatformDataFreeType.cpp` itself uses Fontconfig
patterns and defaults, in addition to FontCache/FontSetCache. Supply real target
Fontconfig support, or implement and qualify an explicit native font-selection
backend. A successful fixture with a chosen font file does not implement family
matching, fallback or downloadable web fonts for WebCore.

## Next executable milestone

The smallest useful next integration target is an **original WebCore image
decoder feeding its Cairo backing store in an isolated CHOS window**. This tests
the actual engine adapter between the already ported libraries, instead of
another independent codec consumer. It remains a component milestone, not a page
renderer or a replacement for the final browser requirements.

Start with the original PNG/JPEG decoder classes, `ScalableImageDecoderFrame`,
their actual shared-buffer/color/geometry dependencies and
`platform/image-decoders/cairo/ImageBackingStoreCairo.cpp`. The latter exposes
decoded premultiplied pixels as a real Cairo image surface and retains the owning
`DataSegment` through Cairo user data. Compile the original dependency closure;
do not fabricate successful WebCore methods to satisfy a link.

For an initial PNG/JPEG-only component, construct the concrete decoder directly.
`ScalableImageDecoder::create` in `ScalableImageDecoder.cpp` references BMP, GIF,
ICO and WebP decoders unconditionally on non-Cocoa platforms. Claiming that full
factory works therefore requires its full selected codec closure, including
WebP. Optional AVIF/JPEG XL have explicit upstream feature gates.

Acceptance should use real incremental `setData`/frame decoding, Cairo surface
ownership and native window presentation. Compare complete output pixels, close
or cancel while partial input is held, repeat under workers, and require the same
cold process/page/engine/futex cleanup checks as existing native fixtures. Preserve
the codec corruption and allocation-failure tests at this new API boundary.
The PNG fixture's fatal-warning policy is not automatically inherited: upstream
`PNGImageDecoder.cpp` installs its own warning and error callbacks and contains
additional animation parsing paths. Those policies require separate review and
tests; standalone decoder results do not certify them.

`platform/graphics/ImageBackingStore.h::setSize` uses one contiguous
`Vector<uint8_t>` of `width × height × 4` bytes. `ScalableImageDecoder::setData`
also makes encoded input contiguous. The native PNG fixture's separate-row
strategy does not lift CHOS's current single-allocation bound for these original
engine operations. A small first image test must be labeled accordingly, and
larger contiguous engine buffers must be implemented and qualified before
claiming ordinary browser-size image support.

## Native compiler checkpoint

The [reusable image probe](../tools/probe_native_webcore_images.py) now compiles
four original WebCore implementation units into actual AArch64 objects:
`ScalableImageDecoderFrame.cpp`, `PNGImageDecoder.cpp`, `JPEGImageDecoder.cpp`
and `ImageBackingStoreCairo.cpp`. [The compile report](validation/stage3-webcore-image-compile.json)
retains exact commands, source/object hashes, ELF headers and unresolved symbols.
The probe verifies the pinned upstream commit and unchanged WebCore/JSC source,
the complete native SDK, 2,116 published patched WTF/bmalloc source files and all
installed target dependency files. Host dependency prefixes are rejected.
Exported header aliases point to original headers rather than synthesizing APIs.

This is **compilation only**. Objects have unresolved symbols, have not been
linked together and have not executed. The explicit release/Cairo/FreeType flags
are a compiler probe, not a generated full-engine configuration or evidence that
its ABI matches the existing JSCOnly archives. No page or decoder output is
claimed at this boundary. Full dependency closure, consistent feature generation,
linking and actual decoder/window execution are the next implementation steps.

For reproducible compiler checks, provide verified target build prefixes and the
published native engine source/header kit. Every output directory must be fresh:

```sh
python3 tools/probe_native_webcore_images.py --out build/webcore-image-probe \
  --webkit-source /path/to/pinned/webkit-2.54.2 \
  --native-sdk build/native-sdk --engine-kit /path/to/published/engine-kit \
  --graphics-prefix /path/to/native-graphics/prefix \
  --font-prefix /path/to/native-fonts/prefix \
  --png-prefix /path/to/native-png/prefix --jpeg-prefix /path/to/native-jpeg/prefix
```

## Route from the component to real page painting

Create an explicit `CHOS` port identity rather than selecting WPE or PlayStation
and suppressing their failed dependency checks. The required build seams are:

1. Add CHOS to `Source/cmake/WebKitCommon.cmake`'s `ALL_PORTS` and provide
   `Source/cmake/OptionsCHOS.cmake`. The existing `JSCOnly` options forcibly set
   `ENABLE_WEBCORE`, `ENABLE_WEBKIT` and `ENABLE_WEBKIT_LEGACY` to `OFF`; a cache
   override cannot turn the current JSCOnly build into a WebCore port.
2. Provide `Source/WebCore/PlatformCHOS.cmake` and
   `Source/WebCore/PAL/pal/PlatformCHOS.cmake`. The upstream
   `WEBKIT_INCLUDE_CONFIG_FILES_IF_EXISTS` macro selects `Platform${PORT}.cmake`;
   neither CHOS platform file exists yet. Use the actual native SDK toolchain,
   not the old compiler-only preflight that produces static probe archives.
3. Build the real `WebCore` and `PAL` targets with the existing native WTF/JSC
   platform work, target-only dependency lookup and original generated sources.
   WebCore's common CMake target also links LibXml2, SQLite and Zlib. Existing
   native PNG/JPEG dependencies do not provide those missing bindings. Rebuild
   dependent targets under the same feature configuration; do not assume that
   separately configured JSCOnly archives have the required platform ABI.
4. Implement the required CHOS font selection, screen, theme, event, clock,
   resource/network and graphics adapters. PAL's PlayStation file uses an
   OpenSSL digest backend; reusing CHOS's reviewed crypto dependency requires a
   real PAL implementation and vectors, not an assumed library substitution.
5. Construct a real `Page`/`LocalFrame`, load local HTML through a
   `FrameLoadRequest` and `SubstituteData`, invoke
   `LocalFrameView::updateLayoutAndStyleIfNeededRecursive`, then paint through
   `LocalFrameView::paintContents(GraphicsContext&, ...)` and
   `GraphicsContextCairo` into an owned native window image. Test layout changes,
   text, image loading and real JavaScript DOM updates, with responsive input.

Upstream `loader/EmptyClients.cpp::pageConfigurationWithEmptyClients` can reduce
the bootstrap client surface for an explicitly local diagnostic, but it supplies
dummy storage and empty networking/editor/browser clients and sandbox flags.
It is not a functional browser configuration or a secure production policy.
Replace those services with real implementations before claiming navigation,
forms, downloads, storage, permissions or browser security.

The full modern browser still requires actual privileged-service separation,
TLS/trust/time/update integration and the revised browser features and platform
tests. Native Pi 5 wireless remains a separate mandatory implementation. Neither
an image component nor a first local page diagnostic completes Stage 3 or
authorizes version 0.50.0.

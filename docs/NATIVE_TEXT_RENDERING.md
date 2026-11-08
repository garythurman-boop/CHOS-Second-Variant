# Native font shaping and rasterization

CHOS builds genuine FreeType 2.15.0 and HarfBuzz 15.1.0 for its AArch64 user ABI.
A reusable user-process backend shapes UTF-8 with HarfBuzz and ICU 79, rasterizes
scalable outlines with FreeType, and draws into owned opaque pixel surfaces.
An isolated diagnostic evaluates Unicode text in the original JavaScriptCore
engine and displays the resulting text in a native window.

This supplies font-library and software-text prerequisites. WebCore, HTML/CSS/DOM,
browser navigation, font fallback and a production font sandbox remain unfinished.
Existing desktop applications retain their existing typography. Stage 3 remains
incomplete and the development version remains 0.30.0.

## Dependencies and runtime

[The source manifest](dependencies/native-fonts.json) pins upstream archives and
every selected original file. The builder checks the complete inventory and never
uses host libraries for native lookup. FreeType has 44 archive members; HarfBuzz
has 69 core members and one ICU adapter. Optional external PNG/Brotli/bzip2/HVF
decoders and HarfBuzz GPU/vector/raster/subset helpers are unqualified. FreeType's
internal gzip decoder remains enabled.

**This software uses FreeType.** FreeType is used under its original
[FreeType License](../third_party/freetype/docs/FTL.TXT). Sources and notices are
retained, along with HarfBuzz's [license](../third_party/harfbuzz/COPYING).
Five original test fonts retain OFL 1.1 notices and copyright metadata in
[the fixture manifest](dependencies/text-fixtures.json).

FreeType requires real nonlocal recovery. Native libc now includes unchanged
musl 1.2.5 AArch64 setjmp/longjmp assembly, pinned in
[its manifest](dependencies/musl-setjmp.json). The 176-byte buffer preserves the
required callee registers, SP and lower halves of d8..d15. Independent native
assembly checks actual register restoration, deep frames, nested aliases,
zero/negative/extreme return values and three separate threads. Signal-mask APIs,
cross-thread jumps and C++ unwinding are not provided.

HarfBuzz's optional mapped-file optimization is explicitly disabled by
`ports/text/harfbuzz_native.h`: CHOS has anonymous mappings but no POSIX stat/fstat
interface. Its original stdio/snapshot loader remains available. Real pthread
mutexes remain enabled; this does not provide full pthread cancellation.

## Ownership and limits

[`chos/text.h`](../userland/text/include/chos/text.h) exposes the C API. Opening
a font copies its bytes; callers may release or reuse their input immediately.
Each handle owns its FreeType library/face, HarfBuzz font and mutex. Concurrent
drawing through a handle is serialized. Callers must finish/join all uses before
closing it; a closed handle cannot be reused.

Limits are eight live handles, four MiB per font, 128-pixel font height, 4096 UTF-8
bytes and 4096 output glyphs per draw. Surfaces are at most 1024x768 with explicit
stride/capacity checks. The renderer uses 64-bit coordinates, clips gray/monochrome
coverage and preserves padding. Ink is opaque. Invalid UTF-8 follows HarfBuzz's
replacement-character policy; bounded language hints and automatic/LTR/RTL
direction are available. Font failures retain error results; partial staging
pixels must not be presented as a completed frame.

These limits supplement process quotas. They do not provide a CPU deadline for
hostile font parsing or a production content sandbox. Full bidi paragraph layout,
line breaking, font fallback/cache, color/SVG/bitmap fonts and WOFF2 remain work.

## Verification and diagnostic

The **CHOS Native Shaped Text** window displays Latin ligatures, joined RTL Arabic
and Devanagari at 24, 32 and 16 pixels. Space changes size; Escape/close exits.
The JavaScript variant normalizes the strings in a real VM under a five-second
engine CPU watchdog, copies actual results, collects and releases the VM. There
is no native-text fallback on JavaScript failure. This is not HTML/CSS layout.

Default diagnostic mode uses compiled original licensed font assets, allowing
boot-media testing without installed font files. `--filesystem` instead requires
all five `/FONTS/F0.TTF` through `F4.TTF` files through real native snapshot streams;
missing files fail without fallback. The NVMe fixture mounts only its disposable
disk through the real read-only Diagnostics control and requires an unchanged disk
hash. Neither mode implies automatic font discovery or a system font installation.

The fixture executes all 64 cases from the complete original HarfBuzz Arabic
normalization corpus, comparing exact glyph names, clusters and positions.
The runner compares all 131072 framebuffer pixels for each of three frames
against a separately built host reference. Host Unicode functions come from
HarfBuzz; native functions come from ICU. These selected fixtures do not qualify
every script/font format. Other checks cover malformed inputs, buffer reuse,
handle limits, clipping/stride canaries, extreme coordinates, shared/independent
native workers, physical keys, normal exit and exact cold physical-page,
committed-quota, engine-owner and futex cleanup.

See [the actual UEFI frame](validation/stage3-native-text-uefi.png) and
[the validation record](validation/stage3-native-text.json) for exact hashes,
guest profiles, retained failures and limits. Host ASan/UBSan/leak checks are
separate from native execution. New Fusion/Pi 5/laptop qualification is NOT RUN.

## Reproduction

Use the prepared compiler/CMake/Ninja tools, fresh output directories and a complete
current native SDK. Never rebuild target inputs during live guest acceptance.

```sh
source /workspace/chos-tools/activate
make CC=clang LD=ld.lld -j4 native-sdk
python3 tools/build_native_text.py --native-sdk build/native-sdk --out /new/native-text
python3 tools/build_native_text.py --host --sanitize --out /new/host-text
mkdir /new/host-frames
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /new/host-text/application/native_text third_party/text-fixtures/fonts /new/host-frames
```

For actual JavaScript text, add `--engine-kit /path/to/extracted/a89db07-handoff`
to the native builder. It verifies all 10645 original payload files and all 535
engine objects. The new SDK is separate; recovered engine/SDK inputs stay unchanged.

```sh
python3 tools/prepare_native_engine.py --application /new/native-text/application/native_text
make CC=clang LD=ld.lld -j4 engine-test-images
python3 tests/qemu_native_text.py --mode direct --runs 3 --application /new/native-text/application/native_text --reference /new/host-frames --out /new/direct-evidence
python3 tests/qemu_native_text.py --mode uefi --runs 3 --application /new/native-text/application/native_text --reference /new/host-frames --out /new/uefi-evidence
```

Use `--with-javascript` for the engine-linked executable, `--embedded` to require
compiled assets and `--nvme` with UEFI for read-only NVMe loading. `--image-root`
selects frozen images. Diagnostic ROMFS replaces `/BIN/JSCVM.ELF` and has no
installer; normal installation media and earlier diagnostics remain separate.

The stock WebCore FreeType font cache also requires Cairo and Fontconfig. These
archives do not supply those bindings. Full graphics/image/XML/storage/platform
integration, browser features, production trust/time/update policy and native
Pi 5 wireless remain open software work, alongside hardware qualification.

# Native Cairo Fontconfig checkpoint

Original Cairo 1.18.4 now builds and executes its actual Fontconfig/FreeType
pattern backend on native AArch64 CHOS. This adds the
`cairo_ft_font_options_substitute` and
`cairo_ft_font_face_create_for_pattern` APIs needed by the CHOS WebCore port.
It is a headless component test: no WebCore page, owned GUI window or modern
browser is established by this checkpoint. Stage 3 remains incomplete at 0.30.0.

The [graphics builder](../tools/build_native_graphics.py) accepts an optional
`--fontconfig-build` directory. It checks that build's source and installed-file
inventories, real FreeType/XML dependencies and host/native profile before using
it. Cairo and Fontconfig must use the same actual FreeType archive. Native package
search remains confined to the immutable SDK and explicitly supplied prefixes.
Meson enables the real Fontconfig backend and retains actual pthread/C11 atomic
checks. Omitting the option retains the earlier explicit FreeType-face profile.

Original Cairo/pixman files and licenses remain pinned in the existing
[graphics manifest](dependencies/native-graphics.json). The build copies Cairo
before applying explicit patches with zero fuzz. A complete audit of both final
native and host source copies checks all 495 Cairo files against the original
inventory plus the exact three patches. Vendored originals are unchanged.

The [new CHOS-only patch](../ports/graphics/cairo-chos-font-file.patch) handles
two actual platform contracts. Cairo's POSIX `access(filename, R_OK)` guard is
replaced by a genuine read-only `fopen`/`fclose` check; nonexistent files still
produce `CAIRO_STATUS_FILE_NOT_FOUND`. Native CHOS has four process-owned file
descriptors, so its cached open FreeType-face budget is two instead of ten.
Original Cairo eviction closes real unused faces. Other hosts retain the original
POSIX check and cache budget. This is not a new POSIX API or a capacity bypass.

Fontconfig still requires an explicitly installed application catalogue; no
default directory scan or disk cache is supplied. The fixture loads real CHFS
font paths, installs XML alias rules and creates the global catalogue before Cairo
uses it. It compares selected Noto Sans, Noto Naskh Arabic and Noto Sans Devanagari
faces with separately created FreeType memory faces backed by the exact same
font bytes. Reference faces retain their bytes and FreeType library through a
real Cairo user-data destruction callback.

The [exact validation report](validation/stage3-native-cairo-fontconfig.json)
records SDK, source, dependency, archive, executable and execution evidence.
The final native executable is 2,801,368 bytes, SHA-256
`763139b46f01695e0eba9df652bf419bd48d588a676357c3a2ad058c8214a1d4`.

- Each process performs 120 complete comparisons of 256 by 112 ARGB buffers:
  3,440,640 pixels and eleven text/font metrics per comparison. Buffer guards,
  actual ink and nonzero cmap glyph indices are checked. The Fontconfig path and
  explicit memory-face path both render through original Cairo/FreeType; this
  verifies integration rather than an independent rasterizer's correctness.
- Tests cover XML `sans-serif` substitution, lazy family matching, explicit
  charset fallback, retained faces after both caller patterns are destroyed,
  selected antialias/hint options, missing-file refusal and three actual pthread
  workers. After selecting three distinct fonts, two additional real files must
  open simultaneously, checking descriptor headroom and native cache eviction.
- Three complete catalogue/render/worker/cache teardown rounds occur inside
  every process. The native retained heap stays at seven allocations and 184,320
  mapped bytes across all three rounds. Individual native owners have not all
  been attributed; this is not a zero-heap claim.
- Direct and isolated UEFI each pass three process lifetimes with exit zero and
  exact cold restoration: 110,116 / 494,486 physical pages, released engine/frame
  ownership and no queued futex waiters. Normal CRT teardown and process reaping
  release the retained native state.
- The corresponding separately compiled host profile passes ASan, UBSan and
  LeakSanitizer with leak detection enabled, with the same 120 comparisons and
  900 cmap glyphs. The Arabic/Devanagari cases test glyph selection; Cairo's toy
  text API does not establish complex-script shaping.

Failures remain recorded. The first native run exhausted descriptors while
cached Cairo file faces and file-backed reference faces were retained; a separate
diagnostic reproduced `EMFILE` before Arabic face creation. Reference faces now
use immutable memory bytes, and the production cache follows the native budget.
An earlier fixture also requested an unsupported 192 KiB worker stack; it now
uses the actual 128 KiB contract. A subsequent zero-heap assertion was an invalid
oracle for retained native state and was replaced with measured stable rounds
plus exact process cleanup. Stale Fontconfig builder-input hashes correctly
refused older reports; current FC5 reports verify their inputs, and the native
FC archive/executable are byte-identical to the runtime-qualified FC4 versions.

Larger simultaneously locked font sets or competing descriptor users can still
fail explicitly. Long-running lifecycle tests, the complete upstream rendering
corpus, hostile-font security, full WebCore's generated ABI, page rendering and
physical Fusion/Pi 5/laptop qualification remain open.

Reproduce using fresh output directories and the prepared tools:

```sh
source /workspace/chos-tools/activate
export PATH=/workspace/chos-tools/build-python/cmake/data/bin:/workspace/chos-tools/build-python/bin:$PATH
python3 tools/build_native_graphics.py \
  --out /workspace/chos-evidence/UNIQUE-CAIRO-FONTCONFIG \
  --native-sdk /workspace/chos-evidence/webcore-link-20261112/memory/sdk7 \
  --font-build /workspace/chos-evidence/native-fonts-20261107/final-jsc-text \
  --fontconfig-build /workspace/chos-evidence/native-fontconfig-20261113/final-native5
python3 tests/native_cairo_fontconfig/qemu.py \
  --out /workspace/chos-evidence/UNIQUE-CAIRO-FONTCONFIG-DIRECT \
  --application /workspace/chos-evidence/UNIQUE-CAIRO-FONTCONFIG/application/native_cairo_fontconfig \
  --image-root /workspace/chos-evidence/native-cairo-20261108/clean-cairo-images
```

Repeat the guest command with `--mode uefi` and a new output directory. For the
host, use `--host --sanitize`, `final-host-text` and `final-host5` dependency
builds; run the resulting executable with the repository's
`third_party/text-fixtures/fonts` directory as its sole argument and
`ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1`.

# Local WebCore page diagnostic (unfinished)

The complete local-page executable links and launches in CHOS. The guest fixture checks each step through page creation, layout and paint. See the current Stage 3 report for the latest passing step.

It follows the original internal headless `Page` workflow used by `SVGImage`,
loads only a trusted embedded HTML document, sizes a real frame view, and uses
the original parser, CSS layout, JavaScript DOM bindings and Cairo graphics.
The sandbox restriction is explicitly relaxed only for this fixture's own
document so JavaScript can execute. No network or production browser client is
supplied by the upstream empty clients.

The intended checks are actual DOM rectangles, complete flat-color CSS pixels,
font glyph coverage, and changed geometry, transform, color and text after a
JavaScript mutation. Fontconfig must receive the existing five font files at
`/FONTS/F0.TTF` through `/FONTS/F4.TTF` before the engine starts.
Owned-window presentation, input, lifecycle qualification and the full browser
remain future work until this basic page target builds and passes.

Build `CHOSLocalPage` using the full WebCore configuration. Keep the unstripped
executable to symbolize guest faults. Stage the loadable image as follows.

```sh
llvm-strip --strip-all -o "$out/local-page.elf" "$out/build/bin/CHOSLocalPage"
python3 tools/prepare_native_engine.py --application "$out/local-page.elf" --out "$out/images"
make CC=clang LD=ld.lld ENGINE_TEST_DIR="$out/images" engine-test-images
for mode in direct uefi; do
  python3 tests/native_webcore_network_values/qemu.py --fixture local-page --embedded \
    --icu-packages "$icu_assets" --mode "$mode" --image-root "$out/images" \
    --application "$out/images/native-engine.elf" --out "$out/page-$mode"
done
```

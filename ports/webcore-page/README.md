# Native WebCore local-page target

This port builds WebCore, PAL, JavaScriptCore, WTF and bmalloc with one set of
generated CHOS headers. The local-page diagnostic uses WebCore's real frame,
HTML parser, CSS layout, JavaScript DOM bindings and Cairo paint path. The page
executable now links and launches in the guest. The smaller
`CHOSNetworkValues` target builds and runs on CHOS with the same generated
configuration. [Runtime results](../../docs/validation/stage3-webcore-page-values.json)

The software configuration uses the system allocator, ICU, LibXml2, SQLite,
zlib, Fontconfig, FreeType, HarfBuzz, Cairo/pixman, PNG, JPEG and WebP/demux.
Its platform sources supply a GUI screen snapshot, font selection, generic
control painting, a software scrollbar, public suffix lookup, request/response
values, certificate metadata, PAL digests and key ownership. The DNS queue uses
the CHOS resolver and WebCore's timer. Its timeout, cancellation and callback
ordering still need guest tests. The diagnostic uses an embedded local
document and a software renderer. GPU, video, JIT and the separate WebKit
application/process layers are disabled in this configuration.

## Build and reproduce

Use the WebKit 2.54.2 checkout at commit
`32717fabcb9dcad9aa00a54ef1f23e41ee64e849`, the published native engine
source kit, the verified CHOS SDK and verified target dependency prefixes.
The configure driver checks the SDK, the kit manifest, the upstream blobs and
each prefix's installed-file hashes. Start with a new output directory. Each
prefix below names the installed prefix containing its build receipt.

```sh
python3 tools/configure_native_webcore_page.py --out "$out" \
  --webkit-source "$webkit" --engine-kit "$kit" --native-sdk "$sdk" \
  --prefix "$xml" --prefix "$sqlite" --prefix "$zlib" --prefix "$cairo_png" \
  --prefix "$fonts" --prefix "$png" --prefix "$jpeg" \
  --prefix "$fontconfig" --prefix "$webp"
cmake --build "$out/build" --target CHOSNetworkValues --parallel 4
python3 tools/prepare_native_engine.py --application "$out/build/bin/CHOSNetworkValues" \
  --out "$out/values-images"
make -j4 CC=clang LD=ld.lld ENGINE_TEST_DIR="$out/values-images" engine-test-images
for mode in direct uefi; do
  python3 tests/native_webcore_network_values/qemu.py --mode "$mode" --embedded \
    --application "$out/values-images/native-engine.elf" --image-root "$out/values-images" \
    --out "$out/values-$mode"
done
cmake --build "$out/build" --target CHOSLocalPage --parallel 4
```

The relocated cloud Ruby needs both its standard-library and architecture
extension directories. Pass each as `--ruby-library-dir` and select the
interpreter with `--ruby`. The build also needs host gperf, CMake and Ninja.
Each configure report includes the selected interpreter, library directories,
SDK manifest and dependency receipts.

The resource fixture exercises redirects, isolated copies, body metadata,
catching stale state when objects are reused, private error descriptions,
certificate metadata encoding and WebCore's public suffix API. Its diagnostic
kernel embeds the ELF to accommodate the ICU data. Production browser
navigation and the local page's geometry and pixel checks remain open.
[Local page fixture](../../tests/native_webcore_page/README.md)

For the local page, build the nine CHFS files from the verified ICU data. Place
the resulting `ICU0.DAT` through `ICU8.DAT` in the guest's `/ICU` directory.
The loader checks each file's size and SHA-256 before registering any with ICU.

```sh
python3 tools/prepare_webcore_icu_data.py --source build/icu-data --out "$icu_assets"
```

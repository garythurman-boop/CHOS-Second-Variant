# Native WebCore clipboard

The local-page port uses a process-local clipboard. Two live pasteboard handles
share a revision and item sequence. WebCore's private staging pasteboard keeps
its own strings and buffers. Readers see a snapshot if a callback rewrites the
clipboard during a paste. The editor uses the upstream selection and paste
paths and copies font properties from the current selection.

Build `CHOSClipboardValues` in the configured page tree and stage the ELF with
`tools/prepare_native_engine.py`. Build diagnostic media with
`make CC=clang LD=ld.lld ENGINE_TEST_DIR="$images" engine-test-images`. Run
`tests/native_webcore_network_values/qemu.py` with `--fixture clipboard`,
`--embedded`, `--image-root "$images"`, `--application "$images/native-engine.elf"`,
a fresh `--out`, and each of `--mode direct` and `--mode uefi`.

Both boot paths passed the checks for origin-restricted custom types, private
binary staging, reader reentry, indexed content, revision changes and keyboard
modifier transitions. The guest returns to its original free-page count
after each process exits. Editor selection-to-DOM paste, desktop integration
and cross-process copy/paste still require testing or implementation.
[Validation report](../../docs/validation/stage3-webcore-clipboard.json)

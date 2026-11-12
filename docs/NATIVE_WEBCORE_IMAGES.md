# Original WebCore image execution

The original WebCore PNG/JPEG decoders now link and run in isolated CHOS
applications, using actual SharedBuffer/DataSegment storage and Cairo backing
surfaces. The consumer presents those decoded pixels through owned CHOS window
images. This advances step 1 of [the accepted roadmap](STAGE3_EXECUTION_ROADMAP.md).
It does not construct a Page/DOM, perform CSS layout or implement the modern
browser. Version remains 0.30.0.

## Component and reproducible source

[The source inventory](dependencies/native-webcore-images.json) pins 95 original
WebKit 2.54.2 files, including eleven implementation units, their required headers
and original license texts. File SHA256, sizes and pinned upstream Git blob IDs
are checked offline. This is a curated component, not the complete WebCore tree.
Original LGPL/BSD notices remain intact. The separately built native WTF/bmalloc
archive and headers come from the verified published engine source/relink kit.
The real target SDK, graphics, font and codec inventories are checked before and
after the build. Host dependency prefixes cannot satisfy this target.

[The explicit component patch](../ports/webcore-images/native-component.patch)
applies only to a fresh source copy and carries modification notices. It omits
three unsupported definitions: full image-format autodetection and SharedBuffer's
two shared-memory IPC conversions. The consumer constructs the real PNG/JPEG
classes directly. No successful replacement methods or shared-memory handles
are fabricated. The original in-process storage/decoder/ownership code executes.
The complete browser must supply its full selected factory and IPC services.

Every component and consumer unit uses one
[recorded profile](../ports/webcore-images/source-list.json). In particular,
`HAVE_MMAP=1` selects the existing real native WTF MmapSpan-backed mapping layout.
The earlier compiler probe omitted that flag and expressly did not establish
ABI compatibility. Actual linking and execution now check the selected component
profile against its native dependencies. This does not certify a complete
configured WebCore/PAL port or all engine APIs.

[The runtime validation report](validation/stage3-webcore-image-runtime.json) records
actual build, reference, guest and regression evidence.

## Checks and boundary

The selected tests use a mathematically generated transparent 96×64 PNG and the
original IJG 227×149 JPEG. This software is based in part on the work of the
Independent JPEG Group; its [original notices](../third_party/libjpeg-turbo/README.ijg)
are retained. PNG reference samples and premultiplication are calculated directly.
JPEG reference samples come from the already qualified generic upstream decoder
and must match the pinned complete-output SHA256. Cairo and WebCore do not render
the reference frames.

The actual consumer exercises full and cumulative fragmented input, malformed
signatures, incomplete-input cancellation and retained surfaces after their
decoder/input objects are destroyed. It checks premultiplied BGRA output, frame
count/completion and Cairo surface status. Actual PNG/JPEG results are composed
into three different 512×256 window images. All 131,072 pixels of each visible
frame are compared exactly with independent integer source-over calculations.
Physical emulated keyboard input triggers redraw and close.

Direct and UEFI QEMU each run three process/window lifecycles. The consumer
returns to its warm process-global allocation baseline after releasing image
resources and resetting actual Cairo caches. It retains six WTF process-global
allocations in one 64 KiB heap region until process teardown; this is reported,
not presented as zero live allocations before return. The guest checks complete
cold physical-page restoration, normal exit 0, zero committed frame limits,
released engine ownership and zero futex waiters after every process exits.

This is selected image-component execution. It does not establish APNG/animation,
ICC/gamma management, EXIF display orientation, all color spaces, every malformed
image, production hostile-content security or full WebCore conformance. The
consumer explicitly selects ignored color profiles. Its tested small images do
not lift the ordinary allocation bound for larger engine backing stores. Large
contiguous engine allocations are separate work within roadmap step 1.
New Fusion, physical Pi 5 and laptop execution have not been performed here.

## Reproduce

Use the prepared Clang/LLD environment, a verified native SDK and the published
engine source/relink kit. Each output directory must be fresh. Target prefix
paths refer to completed builds from the earlier text/Cairo/PNG/JPEG checkpoints.

```sh
python3 tools/verify_webcore_image_sources.py
python3 tools/build_native_webcore_images.py --out build/webcore-images \
  --native-sdk build/native-sdk --engine-kit /path/to/published/engine-kit \
  --graphics-prefix /path/to/native-graphics/prefix \
  --font-prefix /path/to/native-fonts/prefix \
  --png-prefix /path/to/native-png/prefix --jpeg-prefix /path/to/native-jpeg/prefix \
  --jpeg-reference-build /path/to/generic-host-jpeg
python3 tests/qemu_webcore_images.py --mode direct --image-root /path/to/frozen-images --application build/webcore-images/native_webcore_images.elf --reference build/webcore-images/cases --out build/webcore-images-direct
python3 tests/qemu_webcore_images.py --mode uefi --image-root /path/to/frozen-images --application build/webcore-images/native_webcore_images.elf --reference build/webcore-images/cases --out build/webcore-images-uefi
```

The frozen image directory contains matching `chos.elf`, `uefi-kernel.elf` and
`chos.iso` inputs. The runner freezes the application and reference frames and
uses only disposable disks and firmware variables. An external WebKit checkout
is no longer needed to build this component. Successful build reports retain
`executed: false`; actual execution is evidenced separately by guest results.

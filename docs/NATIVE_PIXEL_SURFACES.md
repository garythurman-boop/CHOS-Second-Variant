# Native owned pixel surfaces and JavaScript image data

A real isolated CHOS application can now submit complete software images to its
own desktop window. The pinned original JavaScriptCore engine also computes
actual `Uint32Array` pixels, copies them through this service, and displays the
result. Physical Space causes another JavaScript evaluation and image update;
Escape closes the application and releases the engine and image state.

This is a rendering prerequisite. It does not implement WebCore, HTML/CSS layout,
Canvas, DOM, page navigation or the revised modern browser. The HTTP preview
remains recovery functionality. Stage 3 remains incomplete at version 0.30.0.
The current evidence is in [the validation record](validation/stage3-native-pixel-surfaces.json).
See the [actual UEFI framebuffer preview](validation/stage3-native-pixels-uefi.png)
and [Fusion test checklist](NATIVE_PIXEL_FUSION_TESTING.md).

## Transport and ownership

`include/chos/gui_surface.h` adds four commands under the existing GUI_SUBMIT11
permission. The original `chos_gui_message_t` and GUI_POLL_EVENT12 remain exactly
184 bytes; previously published applications keep their original event buffers.
The new separate submission envelope is at most 552 bytes. This GUI ownership boundary is not the future browser content sandbox.
No framebuffer address,
user pointer or writable shared mapping crosses this interface.

1. BEGIN validates the caller's actual process/window ownership, dimensions and
   client-space origin. It returns a positive server-generated frame ID.
2. TILE copies up to 128 XRGB8888 pixels into an inactive owned buffer. Rows have
   independent tail tiles. Reserved fields, exact envelope sizes, frame IDs,
   indexes and pixel counts are checked before copying. Duplicate tiles replace
   prior data without increasing coverage.
3. PRESENT accepts only complete tile coverage and atomically changes the visible
   buffer metadata. An incomplete frame returns EAGAIN; a stale ID is rejected.
   Uploading an incomplete replacement leaves the previous complete image visible.
4. DROP, CLEAR, window destruction and process exit retire visible/pending IDs.
   A later owner must upload every tile before any previous storage can be shown.

The syscall copies and validates the entire user envelope before service mutation;
a valid header/first pixel followed by an unmapped page cannot partly upload an
image. The service validates the copied header again. The kernel derives the
caller from its address space; an application's supplied window ID grants no
additional authority. Reduced GUI rights are rejected by the existing capability
boundary. This is the current single-CPU service, not an SMP GUI qualification.

A spinlock protects short metadata/tile operations. The compositor pins a front
buffer while drawing with interrupts available; a retired pinned buffer cannot
be overwritten by another frame or owner. BEGIN returns bounded backpressure
when the next buffer remains pinned. No multi-megabyte copy or erase happens
with this lock held. Successful PRESENT/DROP request repaint; BEGIN, TILE and
incomplete PRESENT do not redraw unchanged controls.

Opaque blitting clips to both framebuffer and client area, uses existing desktop
scaling, handles padded strides, and keeps images out of title bars, borders and
taskbar. Images sit below retained text/rectangles/carets. This is software copying,
with neither GPU acceleration nor zero-copy claims.

## Resource bounds and measured scope

There are four fixed double-buffer stores, each with a 1024x768 maximum. Pixel
storage costs **24 MiB of kernel BSS even before an application opens**; coverage,
IDs and pins add small bounded metadata. This deliberate initial bound avoids
allocation inside GUI submission. A later production renderer should measure
and improve allocation/copying and damage tracking before browser performance
qualification. The service supports one image per application window.

Native and JavaScript fixtures each open/redraw/close three times in direct 512 MiB
and UEFI 2 GiB QEMU. Tests sample actual framebuffer RGB values across source rows
and tile boundaries, require real process exits and exact physical-page baseline,
and verify no frame IDs, reader pins, engine lease, committed quota or futex
waiters remain. Separate real checked-Wasm/asynchronous/watchdog work executes
while a C pixel client accepts physical Space and uploads another complete image.
The measured response includes emulator/debugger/input overhead; it is not FPS
or a browser navigation benchmark. See the validation record for measured runs.

The new independent host blitter oracle covers 2000 cases, scales1..4, extreme
coordinates, empty/partial clips and stride canaries. Store tests cover maximum
coverage, replacement and read-pin retirement, including 200 producer frames with
an actual second host thread. GUI tests inspect real retained images, partial
upload behavior, clipping, owner reuse and suppression of incomplete-frame
repaints. Full host and AddressSanitizer/UndefinedBehaviorSanitizer suites pass.
New physical Fusion/Pi5/laptop acceptance is NOT RUN; QEMU is not their substitute.

## Reproduction

Use the prepared tools and existing checkout, preserving previous evidence. Build
normal media and the small C fixture before starting guests:

```sh
source /workspace/chos-tools/activate
make CC=clang LD=ld.lld -j4 all uefi-iso install-disk-image build/qemu-smoke-disk.img build/userland/pixel_demo.chcx
make CC=clang LD=ld.lld -j1 test test-sanitize
```

Extract the exact original GUI CHCX from the published a89db07 kernel for the
legacy test. Its SHA-256 must be
`2feae4321604c0ab6e9f2b3ef1082a5b41dcf4c136be47b28824a0bd08dd0133`.
The native runner refuses a different baseline, including an accidental recompile.

```sh
python3 tests/qemu_pixel_surfaces.py --mode direct --runs 3 --legacy /path/to/original/gui_demo.chcx --out /new/evidence/direct
python3 tests/qemu_pixel_surfaces.py --mode uefi --runs 3 --legacy /path/to/original/gui_demo.chcx --out /new/evidence/uefi
```

For the real JavaScript fixture, extract the published a89db07 complete handoff
with its regular archives, 53 separate objects,full ICU and native SDK. The builder
pins the published manifest, verifies all 10645 payload files, verifies the complete
1210-file SDK and independently verifies all 535 original engine object bytes.
It compiles only the new client, leaves the recovered SDK/engine immutable, links
through the original static closure and rejects unresolved symbols.

```sh
python3 tools/build_jsc_pixel_surface.py --kit /path/to/extracted/handoff --out /new/jsc-pixel-build
python3 tools/prepare_native_engine.py --application /new/jsc-pixel-build/jsc_pixel_surface.elf
make CC=clang LD=ld.lld -j4 engine-test-images
python3 tests/qemu_pixel_surfaces.py --mode direct --runs 3 --javascript /new/jsc-pixel-build/jsc_pixel_surface.elf --legacy /path/to/original/gui_demo.chcx --out /new/evidence/js-direct
python3 tests/qemu_pixel_surfaces.py --mode uefi --runs 3 --javascript /new/jsc-pixel-build/jsc_pixel_surface.elf --legacy /path/to/original/gui_demo.chcx --out /new/evidence/js-uefi
```

The JavaScript client uses the real typed-array API and a five-second engine CPU
watchdog. It protects the actual result through the upload, makes no intervening
engine API calls while borrowing its byte pointer, and performs collection after
presentation. Evaluation failure cannot select a native fallback image generator.
The C fixture independently tests unmapped tails, foreign windows and reduced
permissions; those negative cases are not claimed from the JavaScript fixture.

The diagnostic images replace `/BIN/JSCVM.ELF` only in the separate engine-test
ROMFS and contain no installer payload. Preserve the previous standards diagnostic
before replacing it. `qemu_pixel_surfaces.py --engine --image-root /frozen/standards-images`
exercises engine coexistence; `qemu_native_jsc_suite.py --image-root /frozen/standards-images`
keeps the original selected standards suite available independently. Never rebuild
kernels, target libraries or SDK inputs during a live acceptance matrix.

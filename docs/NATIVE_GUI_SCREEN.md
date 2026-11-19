# Native desktop screen information

Isolated applications can now query the actual desktop dimensions, available
work area, pixel format and desktop scale. This supplies a real system interface
for WebCore's screen backend; it does not establish WebCore page rendering.

`CHOS_SYS_GUI_SCREEN_INFO` (83) returns the fixed 64-byte version-1
`chos_gui_screen_info_t`. It shares the existing GUI_SUBMIT permission class,
which a parent can remove from a child. Applications need no existing window.
The wrapper `chos_gui_screen_info` requests the exact size and zero flags.
Unavailable desktop state returns `EAGAIN`; an older kernel returns `ENOSYS`.

The snapshot contains physical-pixel desktop and work-area rectangles, the latter
excluding the current bottom taskbar. The software surface stores numeric
`0xXXRRGGBB` pixels, with 32 storage bits and 24 color bits. Desktop scale is the
actual integer GUI ratio; it is not a physical monitor DPI measurement or a
claim about browser raster resolution. No framebuffer address, stride, kernel
pointer or mutable display handle is exposed. Multiple monitors and HDR are
outside this interface's current scope.

Mode replacement and the snapshot use one short IRQ-safe lock. The reader
allocates nothing and cannot sleep while holding it. Invalid size or flags fail
before copying; the whole output range must be writable. The address-space
copy validates all destination pages under its own lock before any store, so a
range crossing a protected page cannot partially overwrite user memory.
An invalid replacement mode preserves the previous desktop snapshot.

The [validation report](validation/stage3-native-gui-screen.json) records exact
source, SDK8, executable, frozen kernel/ISO and result hashes. Host GUI tests
check 800×600, a real mocked mode transition to 1024×768, 1920×1080 at scale 2,
2560×1920 at scale 3, taskbar exclusion and failed-initialization preservation.
Focused address/undefined-behavior/leak checks and both complete regression
suites pass.

Direct and isolated UEFI QEMU each pass three native process lifetimes. The
returned dimensions match independent QMP screenshot dimensions. Each process
also performs 192 queries across three pthread workers, checks exact output
guards, and rejects zero/short/large sizes, nonzero flags, null/read-only/wrapping
and cross-guard pointers. A real child with GUI permission removed receives
`EPERM` without modifying output. Native heap allocations and mappings are zero
before return; physical pages, process cleanup, frame reservations, engine owner
and queued futexes return to their cold baseline after every run.

The pause fixture's compile-time API checks now allow additive syscalls while
still pinning its existing resume call to 82. Its runtime behavior is unchanged.
New Fusion, Pi 5 and laptop testing remains unrun. Stage 3 remains incomplete at
development version 0.30.0.

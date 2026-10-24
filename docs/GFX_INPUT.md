# Graphics, desktop and input subsystem (0.9.3-dev)

## Display path

`kernel/arch/aarch64/virtio_gpu.c` discovers a modern VirtIO-GPU device by scanning the QEMU `virt` MMIO aperture. It negotiates `VIRTIO_F_VERSION_1`, establishes control queue 0, creates a 2D resource, attaches a framebuffer backing entry, and connects the resource to scanout 0. Each present operation cleans framebuffer data for DMA, submits `TRANSFER_TO_HOST_2D`, then `RESOURCE_FLUSH`.

The driver uses bounded polling and marks itself unavailable after a control-queue timeout. It advertises 800×600, 1024×768 and 1280×720 by creating one 2D resource per mode against a shared maximum-sized backing buffer. The initial/recovery mode is 800×600. A completed VirtIO-GPU error reply does not quarantine the queue, so rejected mode commands can leave the currently active scanout usable; partial resource creation is cleaned up with RESOURCE_UNREF. Live QEMU mode changes still need acceptance testing. The driver assumes the platform's RAM physical and kernel virtual addresses are identical under the current QEMU `virt` mapping, and does not support blobs/3D, multiple scanouts or hotplug.

## Software renderer

`kernel/gfx/gfx_core.c` implements drawing against a `chos_surface_t`: clear, pixel, rectangle, outline, horizontal/vertical lines, vertical gradient, ASCII text and bevel borders. Drawing can be constrained by an explicitly set clip rectangle; `chos_gfx_reset_clip()` restores normal framebuffer bounds. The clip state is associated with the surface pointer and intended for the current single-threaded immediate-mode GUI renderer. `clear` intentionally ignores the active clip so it remains a whole-surface operation.

The Console retains the original fixed-width 8×12 DejaVu Sans Mono-derived glyphs. General UI text uses compact, antialiased proportional DejaVu Sans regular/bold bitmaps generated at build-development time and embedded as 4-bit coverage data. The TTF files are not distributed. License and attribution are in `docs/DEJAVU_FONT_LICENSE.txt`; `tools/gen_ui_font.py` documents how to regenerate the embedded glyph table from locally installed licensed fonts. Colors are ARGB values to the renderer and map to BGRA bytes on little-endian AArch64.

## Window manager

`include/chos/window_manager.h` and `kernel/gfx/window_manager.c` provide a bounded seven-window model. Each view has a stable ID, title, geometry, z-order, open/minimized flags. The model supports activation, focus, z-order, topmost overlap hit-testing, position clamping, close, minimize/restore, and z-order renormalization before the counter can wrap. The taskbar displays currently open windows, including minimized windows, and activates/restores them on click.

The current GUI retains one implementation per built-in view in the kernel: Welcome, Console, File Manager, About, Diagnostics, Settings and Text Viewer. Text Viewer wraps long lines by glyph width, supports Up/Down, Page Up/Page Down, Home/End and mouse-wheel scrolling, and explicitly reports empty, oversized and unreadable files. Windows can be moved and independently focused/minimized/closed; client layout dimensions remain fixed and the UI does not yet expose general resize controls. This is not yet an IPC-based or general user-space window manager. The next architectural milestone should specify a narrow GUI client API before moving apps to EL0.

The style direction is a clean CHOS-owned blue desktop, readable bitmap typography, consistent spacing and original icons. Supplied desktop images are styling references, not pixel-perfect targets. Avoid third-party product logos. Keep 800×600 available as the recovery mode.

## Graphical Console and process integration

The Console accepts keyboard input for a bounded command line, supports backspace, Enter, eight-command history, line wrapping and scrollback. Its compact command set includes:

- `help`, `clear` / `cls`, `pwd`, `dir` / `ls`, `cd PATH`
- `type PATH` / `cat PATH`, `run PATH` / `spawn PATH`
- `tasklist` / `ps`, `status PID`, `sysinfo`, `version`, `echo TEXT`, `exit`

`run`/`spawn` use the scheduler-owned asynchronous `process_spawn_path()` API and print the queued PID. The graphical boot attempts to enter the persistent scheduled runtime when the GUI and ARM Generic Timer are ready. That lets the serial shell continue polling keyboard/pointer queues while the scheduler runs EL0 programs. If graphics or the timer is unavailable, the kernel remains usable through its serial shell.

EL0 `WRITE` output is appended to the GUI Console whenever that Console window remains open, even while minimized or not focused. Closing Console restores serial output. There is currently only one graphical terminal buffer, not a per-process terminal/session API. Use `tasklist` and `status PID` to inspect application states. The serial shell remains the full diagnostic/recovery shell.

The legacy kernel Console's `cd` changes the path prefix used for path lookups. In 0.12.0-dev, CHFS supports explicit and implicit directory paths; the EL0 Console uses absolute paths and `dir PATH` to list direct children. Individual read syscalls are limited to 256 bytes and clients should read larger files in chunks.

## File Manager

The EL0 File Manager merges CHFS entries with unique embedded ROMFS defaults, supports path-based directory browsing, opens text files via `/BIN/TEXTVIEW.CHX`, and launches `.CHX` applications through the kernel runtime spawn service. CHFS supports file create/replace, delete, rename, explicit directories and empty-directory removal through bounded EL0 syscalls; the GUI currently emphasizes browsing/open/run rather than providing a full file-operation dialog for every mutation. Storage currently uses CHFS v3 with a 511-entry catalogue and contiguous extents; there is no full journal or block allocation map yet.

## Input and interaction

`kernel/arch/aarch64/virtio_input.c` scans the QEMU `virt` MMIO slots for modern VirtIO-input devices and polls completions from the event/status queues. It dispatches key transitions, relative mouse deltas and absolute tablet positions to the GUI. There is no input IRQ path yet. Relative wheel events are forwarded to Text Viewer for vertical scrolling.

- `Tab` and arrow keys navigate desktop selection; `Enter` / `Space` opens a selected desktop item.
- `C`, `F`, `D`, `S`, `A` activate Console, File Manager, Diagnostics, Settings and About from non-Console views.
- In Console, Escape returns to Welcome; history uses Up/Down; Ctrl+L clears and Ctrl+U clears the current input.
- In File Manager, Up/Down selects entries and Enter opens the selected item.
- Drag a title bar to move a window. `_` minimizes; `x` closes. Taskbar buttons focus or restore open views. Escape minimizes non-Console views or dismisses the Start menu.

## Tests and limitations

`tests/test_gfx.c` covers framebuffer bounds, stride, drawing primitives, clip rectangles, gradients, variable-width metrics, bold glyphs and antialiased text coverage. `tests/test_window_manager.c` covers open/activate, overlap hit-testing, position bounds, minimize/restore, close, capacity and z-order renormalization. `tests/test_gui.c` uses stubs for GPU/input/storage/process APIs to cover composition, navigation, window/taskbar interactions, wallpaper, command dispatch, async launch requests, Console output retention, File Manager Run and display-failure fallback.

Run `make test` and `make test-sanitize` for host-side checks. These tests do not emulate VirtIO queues, DMA/cache coherence, framebuffer scanout, actual keyboard/tablet devices or guest exception-return/scheduler behavior. A live `make run-gui CC=clang LD=ld.lld` test on the development Mac is still required before treating the GPU/input and scheduled EL0-to-GUI paths as runtime-validated. Keep PL011 serial attached for recovery.

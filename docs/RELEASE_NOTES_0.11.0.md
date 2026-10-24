# CHOS 0.11.0-dev release notes

## Summary

This is a development milestone for the EL0 GUI application architecture. It introduces a dedicated VirtIO-GPU cursor queue path and moves the graphical Console and File Manager into the embedded user application set. The GUI syscall interface remains experimental and is not yet a stable third-party ABI.

## Changes

- Hardware cursor path uses the VirtIO-GPU cursorq and a 64×64 ARGB device resource generated from the original monochrome pointer bitmap. The driver disables the hardware path on timeout or rejection and falls back to software rendering.
- `/BIN/CONSOLE.CHX` is launched by the desktop when available. Its commands are `help`, `dir`, `sysinfo`, `type /PATH`, `run /BIN/APP.CHX`, `clear` and `exit`.
- A bounded 4096-byte output stream routes child-process `write()` output into the active user-space Console. `CHOS_SYS_CONSOLE_READ` (15) is restricted to the Console owner PID; a full ring discards oldest output rather than blocking.
- `CHOS_SYS_SPAWN_ARGS` (16) exposes simple space-separated arguments to EL0 apps via `chos_spawn_args()`.
- `/BIN/FILEMAN.CHX` lists merged persistent CHFS and embedded ROMFS paths. Enter launches `.CHX` programs or opens text-like files with Text Viewer; Up/Down select and F5 refreshes the catalog.
- `/BIN/TEXTVIEW.CHX` accepts an absolute file path as argv[1]; without one, it displays `/DOCS/README.TXT`.
- ROMFS catalog capacity and Makefile/CHCX embedding have been extended for the additional application.

## Verification performed

- Clean Clang/LLD AArch64 build with `-Werror`.
- Full host regression suite.
- ASan/UBSan host suite.
- Userland and CHCX package verification.

## Known limitations / manual acceptance

- QEMU is not installed in the build environment. Hardware cursor behavior and actual GPU presentation must be validated on the development Mac; the host suites do not prove the cursor no longer flickers on a real VirtIO-GPU device.
- GUI supports at most four owned application windows and 32 retained drawing operations per window.
- File Manager follows the current flat CHFS namespace and does not provide hierarchical directories.
- Console output is a single active-session stream; if more than 4096 bytes are queued without being read, the oldest bytes are discarded.
- A user-space launch fallback to the kernel-resident Console/File Manager remains available if an embedded app cannot be spawned.

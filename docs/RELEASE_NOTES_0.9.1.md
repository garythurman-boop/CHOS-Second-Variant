# CHOS 0.9.1-dev release notes

## Summary

This iteration moves the graphical desktop from a single foreground view to a bounded multi-window model while retaining the existing kernel, storage and EL0 application foundations.

## Highlights

- Seven built-in windows with focus, z-order, overlap hit-testing, movement bounds, minimize/restore, close and taskbar restore.
- Per-window renderer clipping and tests that ensure drawing outside a view's bounds is suppressed.
- Graphical Console launches validated `.CHX` programs asynchronously through the scheduler-owned process API when scheduled runtime is enabled.
- Console process commands: `tasklist` / `ps`, and `status PID`.
- EL0 `WRITE` output is retained by the graphical Console whenever that Console remains open, including while minimized or behind another view.
- File Manager opens/activates Console before launching `.CHX`; using Run with a non-`.CHX` entry produces a visible message.
- GUI boot attempts to start the scheduled runtime when the GPU/GUI and Generic Timer are both ready. The serial shell remains available and is the fallback when graphics or the timer is unavailable.
- Updated roadmap and subsystem documentation for this release and the next development path.

## Build and host validation

The following commands passed from a clean build in the development container:

```sh
make clean
make CC=clang LD=ld.lld -j4
make CC=clang LD=ld.lld test
make CC=clang LD=ld.lld test-sanitize
make CC=clang LD=ld.lld test-userland
```

The kernel is linked as a freestanding AArch64 ELF for Cortex-A72 with `-Wall -Wextra -Werror`. Host regression tests include framebuffer clipping, GUI/window behavior, memory allocators, scheduler policy, process image validation, CHFS, ROMFS, and host image-tool workflows. ASan/UBSan and C userland compilation/packing both passed.

## Important verification boundary

`qemu-system-aarch64` is not installed in the build environment, so no live guest boot was run here. Actual VirtIO-GPU queue/DMA presentation, keyboard/tablet event behavior, scheduled EL0 execution during GUI polling, and live output routing must still be verified on the development Mac with:

```sh
make run-gui CC=clang LD=ld.lld
```

Keep the PL011 serial console attached. The GUI still uses seven built-in kernel-resident views, fixed-size client layouts and a fixed 800×600 framebuffer; it does not yet provide a user-space GUI application ABI, inter-process messaging service, resizable windows, real CHFS directories, user-space `malloc`, or application termination controls. Closing a built-in window does not cancel an already-running EL0 process.

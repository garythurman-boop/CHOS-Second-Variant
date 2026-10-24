# CHOS 0.9.3-dev release notes

## Features

- A minimum three-second splash, held until GUI/device initialization reports complete. The splash is silent in this release; an original optional chime remains planned for 0.9.4-dev.
- Display selection for 800×600, 1024×768 and 1280×720, with 800×600 as the recovery mode. Only these explicitly listed modes are exposed; this is not arbitrary-resolution support.
- Confirmed Restart and Shutdown controls in the Start menu.
- Bold, optically centered Start button text.

## Power behavior caveat

Restart is currently a kernel software restart that branches back to the CHOS boot entry; it is not a hardware or firmware reset request. Shutdown masks exceptions and halts the CPU. The current QEMU `virt` boot path does not have a verified system-off service, so close the QEMU VM from the host after selecting Shutdown. Do not treat either power action as accepted until it is exercised on the development Mac.

## Build and host verification

Run from the project root:

```sh
make clean
make CC=clang LD=ld.lld
make test CC=clang LD=ld.lld
make test-sanitize CC=clang LD=ld.lld
make test-userland CC=clang LD=ld.lld
make run-gui CC=clang LD=ld.lld
```

The freestanding kernel build, host regression tests, ASan/UBSan suite, and CHCX userland packaging pass in the current build environment. This environment does not provide `qemu-system-aarch64`, so real graphical boot, scanout mode changes, restart and shutdown remain to be verified on a machine with QEMU installed.

## Graphical acceptance checklist

1. Confirm the original CHOS splash remains visible for at least three seconds and only transitions after initialization is complete.
2. In Settings, select each listed mode, check full-screen redraw and pointer alignment, and return to 800×600.
3. Use Start → Restart and confirm CHOS returns to its boot sequence.
4. Use Start → Shutdown, confirm the CPU stops and close the VM from the host.
5. Confirm the Start button text is bold and visually centered.

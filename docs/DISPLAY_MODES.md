# CHOS display modes (0.9.3-dev)

CHOS currently advertises a small fixed mode list:

| Mode | Role |
|---|---|
| 800×600 | Default and recovery fallback |
| 1024×768 | Higher-resolution 4:3 mode |
| 1280×720 | Higher-resolution 16:9 mode |

The VirtIO-GPU backend reserves a framebuffer large enough for the maximum supported mode and allocates a 2D resource per mode. The selected mode sets the active surface dimensions and stride; rendering and transfer/flush operations cover only that active pixel region. Desktop window positions and pointer bounds are updated when a mode change succeeds.

Only the listed modes are exposed. This is not arbitrary-resolution support, and host-side GUI tests do not prove that QEMU will accept every scanout size. The development Mac acceptance procedure must select all modes, resize the QEMU display, verify full-screen redraw and input alignment, then return to 800×600. If a GPU command explicitly rejects a mode, the previous mode should remain active and Settings should display an error. Transport timeout still quarantines the GPU driver and falls back to serial recovery.

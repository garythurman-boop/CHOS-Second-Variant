# CHOS replaceable PNG assets

The GUI assets are embedded at build time. Replace these PNGs with artwork of
your choice, keep the file name (or update the corresponding mapping in
`kernel/gfx/gui.c`), and rebuild. No PNG image decoder is needed in the kernel
for the bundled assets.

## Icons

`assets/icons/*.png` are placeholder icons for the desktop and built-in views.
Use non-interlaced, 8-bit PNG with grayscale, RGB, indexed, grayscale+alpha,
or RGBA pixels. Maximum supported icon dimensions are 128x128. Transparent
pixels are preserved. Keep a consistent visual size and weight across icons.

Current names include `computer.png`, `console.png`, `file-manager.png`,
`text-viewer.png`, `diagnostics.png`, `system-monitor.png`, `settings.png`,
`display.png`, `network.png`, `help.png`, `shutdown.png`, and `restart.png`.

## Cursor

`assets/cursor/default.png` is a transparent 16×24 RGBA bitmap rendered at the pointer location. Keep the file at 64×64 pixels or smaller, preserve alpha transparency, and place the hotspot at the tip in the upper-left corner. The GUI scales the pointer with the desktop at 1×, 2×, or 3×, depending on display mode. Replace this PNG and rebuild to change the cursor; the cursor is embedded with the same bounded converter as the icons.

## Wallpaper

`assets/wallpaper/custom.png` is the built-in custom wallpaper shown when the
Settings view switches from the default solid teal background to `Custom PNG`.
Replace this file with your own PNG (maximum 1024x1024 in this build) and run
`make assets` or a normal build. The GUI scales it with a center-cropped
**cover** rule: aspect ratio is preserved and any overflow is cropped so the
screen is fully filled. The wallpaper choice is stored in `/CFG/CHOS.CFG`.

This stage supports the replaceable embedded PNG. It does not yet include an
arbitrary runtime file picker/decoder for any PNG copied to the disk; that
requires a separately bounded runtime PNG decoder and file-selection UI.

## Build-time validation

The Python asset generator uses only the standard library. It checks PNG chunk
CRCs, supported color formats, dimensions, decoded byte count, and decompression
limits. Run `python3 tests/test_png_to_header.py` to exercise the converter's
valid-image, invalid-CRC, and oversized-decompression cases.

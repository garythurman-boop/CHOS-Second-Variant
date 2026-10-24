# CHOS 0.9.2-dev release notes

## Proportional UI typography

The graphical UI now uses embedded antialiased DejaVu Sans regular and bold bitmap glyphs with variable advances and four-bit coverage. The Console deliberately retains the fixed-width 8×12 DejaVu Sans Mono-derived glyphs so command output remains aligned. Font data is generated from locally installed licensed DejaVu fonts by `tools/gen_ui_font.py`; original TTF files are not part of the archive. Attribution and license terms are in `docs/DEJAVU_FONT_LICENSE.txt`.

## Welcome splash

Graphical initialization now presents a short, original CHOS welcome splash, with a blue gradient, branded panel, startup text and a moving activity indicator. The splash is silent in 0.9.2 and transitions after approximately 95 Generic Timer ticks. A keypress can dismiss it; when no timer is available, the next GUI pump goes directly to the desktop.

## Text Viewer

Text Viewer wraps lines using measured proportional glyph widths and displays up to 21 visual rows at a time. Up/Down scroll a line, Page Up/Page Down move most of a viewport, Home/End jump to either end, and mouse-wheel events scroll vertically. Empty files are reported as empty; failed reads, unavailable embedded data and oversized files have explicit error states. Text viewing remains read-only and capped at 1023 file bytes in this milestone.

## Deferred work

The framebuffer remains fixed at 800×600. Selectable resolution modes are planned for 0.9.3-dev; the audio driver and optional original startup chime are planned for 0.9.4-dev. The GUI remains kernel-resident; a documented user-space GUI application interface is still planned for 0.10.0-dev.

## Verification

- Clean AArch64 kernel build with `-Wall -Wextra -Werror`: passed.
- `make test`: passed, including GUI splash and text-viewer scrolling regressions.
- `make test-sanitize`: passed.
- Live graphical QEMU boot and device interaction: must still be checked on the development Mac.

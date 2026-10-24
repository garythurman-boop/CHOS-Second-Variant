# CHOS GUI visual direction (0.30.0)

## Target aesthetic

Use an original CHOS interpretation of late-1990s/early-2000s desktop systems: Windows 9x/2000-era neutral gray controls, 1-pixel 3-D bevels, navy active title bars, a teal classic desktop option, dense sans-serif labels, compact spacing, clear focus states, and a legible Start menu/taskbar. The current default desktop is solid teal; the original CHOS Horizon landscape remains an optional wallpaper. Do not use Microsoft logos, proprietary wallpaper, or redistributed Microsoft fonts/assets.

## Palette and depth

- Main control face: `#C0C0C0`; highlight: `#FFFFFF`; shadow: `#808080`; deepest edge: `#000000`.
- Active title bar / selected row: classic navy `#0A246A`; secondary blue `#3A6EA5`.
- Classic desktop: `#008080`.
- Body copy: black on light panels; white text only on dark active/selected surfaces.
- Buttons and window edges should communicate raised, pressed, disabled, focused, and active state through consistent bevels and contrast, not decorative gradients everywhere.

## Typography

- Use embedded DejaVu Sans regular and bold bitmap rasterizations for general labels, buttons, title bars and dialogs.
- Retain the fixed-width DejaVu Sans Mono-derived 8×12 font for Console output and aligned diagnostics.
- Measure proportional text rather than centering by character count. Keep labels short, hit targets generous, and line heights consistent.
- The original Tahoma font is not bundled. Keep a compact, readable sans-serif aesthetic without runtime TrueType parsing.

## Behavior before ornament

1. Every rendered button and menu row should have a real action, accurate hitbox, visible focus and pressed/selected state.
2. Keyboard navigation must work across desktop, Start menu, dialogs, and applications; Escape should dismiss transient UI where expected.
3. Taskbar/window title truncation must be deterministic and readable at every supported scale.
4. Window move/resize/minimize/maximize/close, focus order, z-order, redraw and off-screen clipping must stay correct under load.
5. Error dialogs must explain what failed and preserve user data. Installer/format-related UI must never imply a disk write happened unless it did and verified.
6. Display scaling must preserve geometry, legibility and hitboxes. 800×600 remains the conservative fallback when a detected mode is unsupported.

## Startup and assets

Use an original CHOS splash with short truthful status lines. Keep the three-second minimum only if startup is still progressing; do not hide failures behind animation. The optional welcome chime must be gracefully absent on soundless hosts. Wallpaper loading should be bounded and preserve aspect ratio, with safe fallback to the solid teal desktop.

## Acceptance checks

- Run `make test-sanitize CC=clang LD=ld.lld -j1` and GUI/window-manager tests.
- Review actual screenshots from 800×600, 1024×768, 1280×800 and one high-DPI mode on real QEMU output.
- Verify mouse and keyboard focus/hover/pressed states live; pixels/unit tests alone do not prove the UI is usable.

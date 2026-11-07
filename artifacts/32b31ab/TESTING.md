# Fusion test checklist: native JavaScript pixel checkpoint

CHOS remains 0.30.0. This is a development rendering prerequisite; the modern
HTML/CSS browser and revised Stage 3 are not complete. Automated evidence covers
QEMU. This diagnostic's independent Fusion execution is NOT RUN.

Use a temporary or cloned ARM64 Fusion VM, 1 CPU, 2 GB RAM, with its network adapter
connected. The user target is Fusion 26H1u1 / macOS 27 / M1 MacBook Air. Preserve valuable
VM disks and verify SHA256SUMS before testing.

The normal `CHOS-Fusion-SOURCE.iso` is installation media. The separate
`CHOS-Native-Pixels-SOURCE.iso` in the handoff is a **diagnostic with no installer**.
Select its CD/DVD in the firmware boot menu if an installed disk boots first.

1. Open the desktop Console and run `run /bin/jscvm.elf` without suite arguments.
   This image's executable is the new pixel client, not the older standards runner.
2. Require `[ OK ] actual JavaScript typed-array pixel presentation passed` and
   a window titled **CHOS JavaScript Pixel Surface**, showing a colored checker
   gradient. Every color is computed by the original native JavaScript engine.
3. With that window focused, press Space several times. Each press must change
   the colors and print `[PIXELS] actual JavaScript Uint32Array frame presented`.
   Test mouse motion and the advancing clock during the work.
4. Press Escape or the title-bar close button. Require
   `[ OK ] actual JavaScript pixel VM cleaned up`; `tasklist` should show normal
   process exit. Repeat opening/changing/closing the window three times.
5. Run `netstatus` and check example.com in the **separate HTTP recovery browser**.
   Test held Backspace and blinking carets in Console/browser/editor, typing `i`
   without Setup opening, Console history, and display settings.
6. With the **normal installation ISO**, test installation, automatic writable
   storage, a persistent file after disk-only restart, actual restart, and shutdown.
   Shutdown must show its status window for at least five seconds and power off.

If the engine cannot initialize, run `rng-test`, `clock`, `bootlog`, `drivers`,
`pci net`, `netstatus` and `sysinfo`; retain exact output and the failing command.
Real entropy and clock services are required. Unavailable services must fail
explicitly; no bypass is supplied. Report a frozen/blank image, corrupt colors,
failed redraw, unexpected process exit, unresponsive input or retained resources.

The previous a89db07 diagnostic and its five selected Test262/Wasm/async/watchdog
commands remain separately downloadable and immutable. They are not commands
for this new pixel executable. WebCore, DOM/HTML/CSS/Canvas, full browser features,
native Pi5 wireless, production browser policy and remaining hardware acceptance
remain open. These software-rendered copied frames do not establish browser FPS,
site compatibility or physical wireless support.

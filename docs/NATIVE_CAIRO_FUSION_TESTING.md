# Fusion test: native Cairo graphics

This development milestone remains version 0.30.0. Stage 3 is incomplete. The
native Cairo diagnostic is a real software renderer test, not a modern browser.
Automated evidence is QEMU; new Fusion execution is NOT RUN.

Use a disposable or cloned ARM64 VM with one CPU and 2 GB RAM. The user target
is Fusion 26H1u1, macOS 27 and an M1 MacBook Air. Keep the network adapter connected
and verify the published SHA256SUMS before booting.

The normal **CHOS-Fusion-SOURCE.iso** is installation media. The separate
**CHOS-Native-Cairo-SOURCE.iso** is diagnostic media with no installer payload.
Select the CD/DVD from firmware if an installed disk starts first. The handoff
includes actual source, SDK, archives, application objects and a verified relink
recipe; it does not rely on a host rendering service.

1. Boot the Cairo diagnostic, open Console and enter `run /bin/jscvm.elf`.
   This image replaces that diagnostic slot with the Cairo client; it does not
   contain the previous Test262 or JavaScript-pixel command-line program.
2. Require the **CHOS Native Cairo** window, showing a gradient, clipped dashed
   curve, translucent transformed shapes, a repeating pattern and native text.
   Compare with the supplied preview. Console diagnostics must report the
   analytic checks and native workers passing, with no FAIL or USER FAULT.
3. Focus the window and press Space twice. The curve dashes and transformed
   shapes must change. Move the pointer and check that the desktop clock advances.
4. Press Escape or the title-bar close button. Require the application/font-cache
   cleanup message and normal exit in `tasklist`. Open, redraw and close three
   times. A freeze, blank/corrupt frame, unexpected exit or retained window fails.
5. Check `netstatus` and example.com in the separate HTTP recovery browser. Test
   held Backspace, blinking carets, typing `i` without Setup opening, Console
   scrolling, display settings and pointer/keyboard operation.
6. Use the **normal installation ISO** to check installation on a disposable disk,
   automatic writable storage, saved data after disk-only restart, actual restart
   and shutdown. Shutdown must show its status window for at least five seconds
   and power off. Diagnostic media must never be used as installation media.

If a step fails, retain its exact command/output and collect `bootlog`, `drivers`,
`pci net`, `clock`, `rng-test`, `netstatus` and `sysinfo`. The drawing's frame hashes
are diagnostics, not proof that Fusion has passed a full browser or security test.
Full WebCore/HTML/CSS/DOM, production browser policy, native Pi 5 Wi-Fi and the
remaining revised Stage 3 gates are still open. Previous download directories
remain available independently.

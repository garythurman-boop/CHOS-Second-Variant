# Fusion runtime test milestone cc29ea8

This is a development build, not Stage 3 completion or version 0.50.0.
Source: `cc29ea83a73e3cb2d6b753645360fd769f921890`.

The build adds native buffered read streams and process-owned file seeking,
and carries the intervening native C/C++/thread/runtime and dual-stack work.
The browser remains the HTTP recovery preview; a modern engine and production
browser TLS are not yet integrated. Pi 5 Wi-Fi is not ready.

1. Boot the ISO in Fusion using the existing working VM configuration. Check
   mouse, keyboard, clock and resolution settings.
2. In CHOS Console enter `run /BIN/STDIO.ELF`. The test exercises real threads,
   separate processes, file snapshots and permission checks. Expect the final
   line: `[ OK ] native buffered read streams,seek ownership and permission boundaries passed`.
   Run it three times. It creates then removes `/DOCUMENTS/CHOS-IO-TEST.TMP`.
   It refuses an existing file; inspect any file left after failure before
   removing it to retry.
3. Check `netstatus` and open `http://example.com/` in the recovery browser.
   Check held Backspace and blinking carets in Console, browser and text editor.
4. Save a small document and a setting, restart from CHOS, and verify persistence.
   Boot without the ISO if installed. Check CHOS shutdown powers off the VM after
   its shutdown window.

Capture failure output plus `bootlog`, `drivers`, `pci` and `netstatus` where
relevant. New Fusion validation is pending. QEMU clean ISO/native-runtime/GUI,
typing and installed-image checks passed; candidate e1000e/VMXNET3/VirtIO,
capability, host and sanitizer regressions also passed. Exact evidence and open
Stage 3 gates are in the repository's docs/validation/stage3-native-stdio*.json
and docs/STAGE3_CURRENT_RESULTS.md.

Verify SHA256SUMS before use; decompress .gz images/kernels before use.

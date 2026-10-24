# CHOS 0.20.2-dev release notes

## Fix: blank browser content after HTTP page load

The user-provided QEMU screenshots showed the browser title changing to “Page loaded” while the page and status text stayed blank. The root cause was the GUI app window's bounded drawing-operation buffer: it held only 32 operations, while one complete browser frame can require about 65 operations for six beveled controls, URL/status text, page lines, and the footer. Draw calls beyond the limit returned `-ENOMEM`, which the browser's rendering path ignored.

Changes in this build:

- Increased the bounded per-window GUI draw-operation budget from 32 to 80. With four app clients, this adds a small fixed amount of kernel memory and keeps the limit bounded.
- Added a GUI regression test submitting 70 text-draw operations to one app window. All 70 must be accepted and rendered without exhausting the operation buffer.
- The browser skips empty text rows, reducing unnecessary draw operations.
- Corrected the content viewport/footer coordinates to stay within the logical client area; previously the background rectangle and footer were beyond the accepted vertical bounds.
- Kept HTTP text-preview behavior and HTTPS fail-closed behavior unchanged.

## Validation

Passed in this environment:

- Clean AArch64 kernel build with Clang/LLD and `-Werror`.
- `make test-gui`, including the new 70-operation regression.
- `make test-browser-core`.
- `make test-sanitize` (full ASan/UBSan target suite).
- `make test-userland CC=clang LD=ld.lld`, including browser CHCX packaging and API compile probes.
- UEFI ISO structural test and generation (`make test-uefi-iso`, `make uefi-iso`).
- CHCX packer, app stack reservation, and system-app synchronization tests.

The aggregate `make test` command exceeded the command time limit during `tests/test_mkchfs.py`. One isolated run of all seven CHFS image-tool tests passed in about 30 seconds; a subsequent repeat was slow enough to hit the timeout before finishing. This filesystem test variance is recorded rather than reported as an aggregate pass. A live QEMU browser check after this drawing-budget fix still needs to be performed on the user's Mac.

## Browser scope and limitations

The browser is an HTTP text preview, not a full browser engine. It does not execute JavaScript, apply CSS layout, render images, provide interactive links/forms, automatically follow redirects, or support HTTPS. HTTPS remains rejected until a maintained TLS/X.509 implementation, certificate trust store, trusted-time source, and secure entropy are integrated and validated. The experimental UEFI ISO is boot-image groundwork, not a verified graphical installer.

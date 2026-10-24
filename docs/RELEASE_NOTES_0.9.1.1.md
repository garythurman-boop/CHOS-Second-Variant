# CHOS 0.9.1.1-dev release notes

## File Manager text-viewer fix

The File Manager previously loaded the selected text file and changed the active view identifier to Text Viewer, but did not register a viewer window with the window manager. As the compositor draws registered windows only, the user could remain looking at the File Manager while the active-view query claimed the Text Viewer was open.

The fix opens Text Viewer through `gui_open_window(VIEW_FILE_VIEWER)`, ensuring the viewer is registered, activated, rendered, and closable with Escape. If no window slot is available, File Manager remains active. A GUI regression test now verifies the viewer can be closed and focus returns to File Manager.

## Verification

- Clean AArch64 build with `-Wall -Wextra -Werror`: passed.
- `make test`: passed.
- `make test-sanitize`: passed.
- `make test-userland`: passed.
- Live QEMU graphical acceptance: not run in this build environment; rerun on the development Mac.

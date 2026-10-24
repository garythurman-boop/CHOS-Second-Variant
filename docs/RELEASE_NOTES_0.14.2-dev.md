# CHOS 0.14.2-dev — Release notes

## Desktop and Start menu

- The vertical CHOS wordmark now uses bold DejaVu Sans bitmap glyphs with a modest size increase and wider tracking. It still reads upward from the bottom of the navy-to-black Start-menu strip.
- Start-menu item selection follows mouse hover or keyboard Up/Down movement and stays highlighted when the pointer leaves the menu. Enter activates and Escape closes.
- The desktop now uses a CHFS-backed icon model with an 8-column by 6-row grid. Dragging snaps icons to a grid cell; dropping into an occupied cell swaps positions. Icon positions are saved in `/CFG/DESKTOP.CFG` and loaded again on subsequent desktop refreshes.
- Added the desktop icon titled `Trash` and a Trash window. Items can be restored to `/DESKTOP`, permanently deleted, or removed with Empty Trash; destructive operations require confirmation.
- Right-clicking the desktop opens Arrange Icons, Refresh, New, and Settings. Context menus on icons provide Open, Rename, Move to Trash, or Trash-specific actions.
- New Folder creates a real CHFS directory. New Text Document creates a `.TXT` file and opens it in the text editor, which supports text entry, backspace, Ctrl+S, and save-on-close. If saving fails, the editor stays open so edits are not silently discarded.
- Desktop folder icons launch the user-space File Manager with the selected folder as its starting path.

## Filesystem changes

- Added `chfs_rename_directory()` and a storage wrapper. The operation updates a directory subtree's path entries in one metadata commit while preserving data extents and rejecting invalid descendant moves and destination collisions.
- Added host regression coverage for nested-directory moves and file-content preservation.
- Desktop icon positions are stored in the existing CHFS volume; the implementation does not introduce a separate host-side desktop database.

## Existing 0.14.x networking groundwork

This release retains the tested Ethernet/IPv4/UDP/ARP/ICMP packet primitives and DHCP/DNS payload builders/parsers added in 0.14.0/0.14.1. Live DHCP lease acquisition/renewal, full interface configuration, an ARP cache, DNS retry/resolution service, TCP sockets, TLS/X.509 verification, and a web browser are not complete and are not advertised as functional.

## Verification

- Clean AArch64 Clang/LLD build with `-Werror` passed.
- The C host regression executables passed, including GUI, CHFS, window-manager, network packet/service, and exception-context tests.
- The ASan/UBSan suite passed.
- Userland CHCX packaging and verification passed.
- The PNG conversion, CHCX package, stack-page, CHFS image-tool, and system-app synchronization Python tests passed individually.
- The combined `make test` invocation reached the slow CHFS image-tool script and hit the environment's five-minute command limit; that script completed successfully when rerun independently.
- QEMU is not installed in this build environment, so live visual/input acceptance must be performed on the development Mac.

## Build and run

```sh
make clean
make CC=clang LD=ld.lld
make test CC=clang LD=ld.lld
make test-sanitize CC=clang LD=ld.lld
make test-userland CC=clang LD=ld.lld
make run-gui CC=clang LD=ld.lld
```

Run commands individually. To preserve files, copy the existing `chos-disk.img` to the new release directory before launching CHOS. Do not run `make disk-image` unless you intend to create a fresh disk image.

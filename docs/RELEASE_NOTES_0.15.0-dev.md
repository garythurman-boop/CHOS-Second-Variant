# CHOS 0.15.0-dev — desktop, File Explorer and CHFS v2

## Goals and visual direction

CHOS retains the Windows 9x-inspired visual language—beveled controls, compact spacing, a navy-to-black Start sidebar and original CHOS iconography—while adding the navigability and consistent file behavior expected from a more capable desktop OS without importing a heavyweight shell.

## Desktop and Start menu

- Increased the vertical CHOS sidebar branding and use an individual glyph cell for each letter, giving C–H, H–O and O–S uniform pitch instead of relying on rotated string kerning. The lettering is bold and bottom-anchored, reading upward in the existing gradient sidebar.
- Retained sticky Start-menu hover/keyboard selection and taskbar full-title hover tooltips.
- Added a Documents shortcut and persistent 8×6 grid positions. Desktop file/folder icons refer to actual filesystem paths; dropping a file or folder on another folder updates CHFS instead of changing only the visual position.
- Initialize missing `/DESKTOP`, `/DOCUMENTS`, `/TRASH` and `/CFG` directories without formatting the disk or deleting existing user data.

## File Explorer

- The File Manager app is now a user-space `CHOS File Explorer` with Desktop, Documents, System (`/BIN`) and Trash shortcuts, current-path display, Back/Forward/Up/Refresh, List and compact Icons views, directories-first/name sorting, row selection, keyboard navigation, scrolling, and session-local navigation history.
- Supports creating folders and text documents, opening files, renaming regular files and directory subtrees, moving items to Trash, restoring/deleting Trash items, context actions, and file/folder drag-and-drop to directory rows or quick-location shortcuts.
- Rejects destination collisions and moves a directory into neither itself nor one of its descendants. Protected system root directories cannot be renamed from user space.
- Increased CHCX supported user stacks from 1–4 pages to 1–8 pages consistently in the kernel validator and `tools/mkchcx.py`. File Explorer is packaged with an eight-page (32 KiB) stack, which addresses its larger directory/catalog and GUI-call stack needs. A regression asserts that the loader accepts eight pages and rejects nine.

## CHFS v2 and v1 migration

- CHFS v2 expands the total persistent file/directory record count from 23 to 63 without changing the 48-byte path field, so paths remain limited to 47 visible characters.
- Two eight-sector checksummed directory copies occupy the final 16 sectors of the image. File data is allocated from LBA 7 to the start of that reserved metadata area.
- The kernel still validates and mounts legacy v1 images. During mount it attempts an in-place migration: old entries are validated first; regular-file extents overlapping the proposed metadata tail are relocated to free lower sectors; v1 metadata copies are committed before those old sectors are reused; both v2 directory copies are written/flushed; and the v2 superblock is committed last.
- If migration encounters an I/O problem or insufficient safe contiguous space, it does not format the volume; it attempts to leave the filesystem usable as v1. Back up important images before first booting a new kernel because old kernels do not understand v2 and should not be used after an image has migrated.
- `tools/mkchfs.py` creates v2 volumes by default and can list/read/update v1 volumes while preserving their original v1 layout. Migration tests put a legacy file in the future v2 metadata tail, check relocation/content, create 40 directory entries, and remount successfully.

## Storage and runtime limits

- CHFS has 63 total persistent record slots in v2 (files and explicit directories combined), not 63 per directory.
- Paths remain at most 47 visible characters. File data is stored as contiguous extents, files are limited to 1 MiB and each EL0 write-file syscall to 64 KiB. Streaming writes, in-place file growth, and multi-extent files are not implemented.
- Metadata alternation and the carefully ordered migration reduce risk but are not a journal. Arbitrary power loss, simultaneous metadata corruption, device failures, and all migration interruption points still require additional fault-injection testing.
- Trash restore currently targets Desktop rather than retaining each item's original location. Explorer history is session-local. Multi-select, search/filter, recursive copy, richer properties and persistent navigation history remain future work.

## Network status

Packet-level Ethernet/IPv4/UDP, ARP, ICMP, DHCP and DNS helpers are not a complete network stack. Integrated DHCP lease/configuration, ARP cache, DNS retry/resolver, user-facing sockets, TCP, TLS/X.509 verification and browser support remain unfinished; no general Internet connectivity is claimed.

## Validation status

- Clean freestanding AArch64 Clang/LLD build with `-Werror` passed, both in the working tree and from a fresh extraction of the packaged source ZIP. The fresh build produced a statically linked AArch64 ELF and the File Explorer CHCX package verified with an eight-page stack.
- Every C/GUI host regression in the aggregate `make test` run passed, including the process-image check for eight-page stacks and the CHFS v1 migration/fallback regression. The aggregate command then exceeded this environment's five-minute command limit while running the slower Python CHFS image-tool tests; the six `tests/test_mkchfs.py` tests passed when run separately.
- All seven `tests/test_mkchcx.py` cases, application stack metadata checks, PNG conversion checks, IRQ `SP_EL0` contract checks, and both system-app synchronization tests passed separately.
- The full ASan/UBSan suite passed, including CHFS migration with no-space fallback. `make test-userland` passed from both the working tree and fresh extracted source.
- This packaging environment does not have QEMU installed, so graphical mouse behavior, File Explorer opening, and real VirtIO block-device migration need live testing on a host with QEMU. Compile/host tests cannot fully establish runtime behavior.

## Build on macOS / Apple Silicon

```sh
make clean
make CC=clang LD=ld.lld
make test CC=clang LD=ld.lld
make test-sanitize CC=clang LD=ld.lld
make test-userland CC=clang LD=ld.lld
make run-gui CC=clang LD=ld.lld
```

Run individually and keep a backup of your disk image. Normal run targets preserve existing user data and synchronize bundled apps. Do not run `make disk-image` unless you intentionally want to recreate the filesystem.

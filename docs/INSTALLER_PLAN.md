# CHOS installer groundwork

## What's present

`include/chos/installer.h` and `kernel/core/installer_plan.c` define a pure, host-testable planner for a 512-byte logical-sector GPT layout:

- 1 MiB partition alignment.
- 64 MiB FAT32 EFI System Partition beginning at LBA 2048.
- CHOS data partition consuming the remaining usable GPT range, with a 64 MiB minimum.
- Disk identity, sector count, and sector size stored in the plan.
- Plan geometry validation and a cookie over immutable plan geometry.
- A mandatory exact destructive confirmation (`ERASE CHOS TARGET`) that must match the currently selected disk identity.
- Monotonic installation stages; the confirmation stage cannot be entered through the generic transition API.

These functions calculate/validate plans only. They do not open a disk, write GPT sectors, format a partition, copy EFI files, initialize CHFS, or display a GUI.

## Design constraints for the actual installer

1. Drive discovery must return a stable opaque identity and validated logical-sector geometry. A display name or disk index alone is not a stable target identity.
2. Present model, capacity, partition map, and the identity of the selected target. Revalidate the identity and capacity immediately before any write.
3. Never write automatically at boot and never autoformat an unknown disk. Require explicit selection and a second destructive confirmation.
4. Keep the planner pure. Put block I/O and filesystem operations in separate testable layers. Do not treat `plan_cookie` as a cryptographic MAC; it is an accidental-mutation check only.
5. Bound all LBA arithmetic, validate GPT primary and backup headers/entry-array CRCs, preserve backup metadata correctly, and flush writes before advancing installation stages.
6. Make writes idempotent where possible; persist enough stage information to recover from power loss. A failed write must not advance progress or claim success.
7. Verify EFI files and data filesystem after writing. Do not report installation complete until all verification steps pass.
8. The UI must be usable from keyboard and mouse, announce the destination and data-loss consequence, show detailed error causes, and support abort before the destructive point.
9. Implement an integration test against disposable in-memory block devices first. Only move to QEMU/VMware disks after corruption/failure injection tests pass.

## What is not implemented

No graphical installer, partition editor, block-device write path, image deployment flow, recovery mode, first-run wizard, or confirmed installed-disk boot is included in this snapshot. `make install-disk-image` is host tooling to build a prepared image, not an in-OS installer.

## Stage 2 implementation supersedes the groundwork-only description

The graphical wizard, bounded deployment engine, live NVMe identity checks, GPT/
FAT32/CHFS writes and readback verification are now implemented. See
[the current lifecycle and validation report](STAGE2_VALIDATION.md) for supported
targets, safeguards, actual ISO-to-installed-disk evidence, faults and limits.

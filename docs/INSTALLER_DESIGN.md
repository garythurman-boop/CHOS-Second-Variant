# CHOS 0.18.x UEFI boot and installer status

## Implemented experimental boot-image groundwork

- `boot/uefi/bootaa64.c` builds as an ARM64 PE32+ EFI application.
- The loader opens `\EFI\CHOS\CHOS.ELF` from the ESP, validates ELF64 little-endian AArch64 headers and bounded PT_LOAD ranges, reserves the static kernel range, copies/zeros segments, finds a validated FDT through the EFI configuration table (with a validated QEMU-virt fallback), exits boot services, flushes instruction/data caches, disables EL1 translation/cache bits and branches to the kernel entry with the FDT in x0.
- `tools/mkuefi_iso.py` creates an ISO9660 primary volume, El Torito EFI boot catalog, FAT16 ESP, and the removable-media path `\EFI\BOOT\BOOTAA64.EFI`.
- `tests/test_mkuefi_iso.py` checks the FAT directory/file chains, PE machine/subsystem fields, ISO descriptor sequence and boot catalog checksum.

## Not yet complete or runtime verified

This session does not have `qemu-system-aarch64`, EDK2 firmware, `xorriso`, or FAT image utilities, so the generated ISO has **not** been booted under firmware. Structural validation and successful build are not equivalent to bootability.

This image is a live kernel boot image, not an installer. The kernel links at `0x40200000` and the current platform supports QEMU `virt` MMIO addresses. Installation still requires a partition scheme (GPT with an EFI System Partition and CHFS data partition), GPT-aware block-device discovery and data partition offsets, a UI for target-disk choice and explicit destructive confirmation, a writer/verifier for the EFI boot files and CHFS data, and a tested reboot from the installed disk. Existing raw CHFS images must not be reformatted implicitly.

## How to build and test when UEFI firmware is available

```sh
make clean
make CC=clang LD=ld.lld
make test-uefi-iso
make uefi-iso
make run-uefi UEFI_FIRMWARE=/path/to/edk2-aarch64-code.fd
```

After runtime boot works, test both an empty data disk and an existing disk copy, validate that the FDT reaches the kernel correctly, run all boot diagnostics, and only then start destructive-install work.


## 0.19.0-dev update

An experimental AArch64 UEFI PE/COFF loader and ISO9660/El Torito image with a FAT16 EFI System Partition are now produced by `make uefi-iso`. Structural tests cover the ISO boot catalog and EFI files. Live UEFI/firmware boot is not verified in this environment. There is still no graphical disk installer, GPT partition creation, safe formatting workflow, copy-to-installed-system flow, recovery tool, or verified boot-from-installed-disk path. Treat the image as experimental and back up disks.

# CHOS 0.18.0-dev release notes

## Summary

This is an experimental AArch64 UEFI boot-image milestone. It adds an EFI application and a reproducible pure-Python ISO builder while preserving the known working direct QEMU kernel launch. It is not yet a full installer, and live UEFI boot has not been verified because UEFI firmware and QEMU are unavailable in the build environment.

## Changes

- `boot/uefi/bootaa64.c`: PE32+ ARM64 UEFI application that reads `\EFI\CHOS\CHOS.ELF` from the EFI System Partition, validates ELF64/AArch64 PT_LOAD ranges, reserves/copies/zeros the kernel image, finds a validated FDT, obtains the EFI memory map, exits boot services, synchronizes instruction/data caches, disables EL1 MMU/cache bits, and branches to the kernel with the FDT in x0.
- `tools/mkuefi_iso.py`: builds an ISO9660 image, primary volume descriptor, El Torito boot record/catalog with EFI platform identifier, and a 32 MiB FAT16 ESP containing `EFI/BOOT/BOOTAA64.EFI` and `EFI/CHOS/CHOS.ELF`.
- `tests/test_mkuefi_iso.py`: checks FAT directory chains, kernel/EFI file content, PE/COFF architecture/subsystem fields, ISO descriptors and El Torito checksum.
- Make targets: `uefi-loader`, `uefi-iso`, `test-uefi-iso`, and `run-uefi UEFI_FIRMWARE=...`.
- Kernel startup banner now identifies 0.18.0-dev.

## Build and validation

```sh
make clean
make CC=clang LD=ld.lld
make test-uefi-iso
make test-sanitize
make test-userland CC=clang LD=ld.lld
make uefi-iso
```

The freestanding AArch64 kernel build and ARM64 PE/COFF EFI application build pass with `-Werror`. ISO generation completes and structural validation passes. The complete host regression suite and sanitizer suite must still be run after this change in the target environment; the UEFI image has not had live firmware testing here.

When a matching QEMU/EDK2 installation is available:

```sh
make run-uefi UEFI_FIRMWARE=/path/to/edk2-aarch64-code.fd
```

## Known limitations

- Boot from ISO has not been verified live. The custom EFI handoff, FDT pointer, execution level, memory map, and device setup need QEMU/EDK2 validation.
- No installer UI or destructive disk-writing path is included.
- No GPT partition parser or CHFS partition offset support exists yet; the current disk storage model uses a raw CHFS volume.
- TCP state management/socket API, TLS/X.509 verification, and browser remain incomplete.
- The successfully tested live DHCP/DNS/ICMP path from 0.17.5-dev is retained.

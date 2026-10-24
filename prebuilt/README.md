# CHOS 0.30.0 prebuilt artifacts

These files were generated from the source at the root of this package with Clang/LLD.

- `CHOS-0.30.0.iso`: ARM64 EFI ISO structure validated; no live VM boot was possible in the build environment.
- `CHOS-0.30.0-installed.img`: experimental 256 MiB GPT/FAT32/CHFS image. It is generated media, not proof of a complete installation or boot-from-disk flow. Use only in a disposable VM.
- `CHOS-0.30.0-kernel-direct.elf`: direct QEMU kernel (`ET_EXEC`).
- `CHOS-0.30.0-kernel-uefi.elf`: relocatable UEFI kernel (`ET_DYN`).
- `CHOS-0.30.0-BOOTAA64.EFI`: AArch64 EFI application.

See `../docs/VALIDATION_0.30.0.md` for exact test results and known limitations.

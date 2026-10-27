# CHOS build cc29ea8

Source commit: `cc29ea83a73e3cb2d6b753645360fd769f921890`.

- [CHOS-Fusion-cc29ea8.iso](CHOS-Fusion-cc29ea8.iso?raw=1)
- [CHOS-cc29ea8-prepared.img.gz](CHOS-cc29ea8-prepared.img.gz?raw=1)
- [CHOS-cc29ea8-kernel-direct.elf.gz](CHOS-cc29ea8-kernel-direct.elf.gz?raw=1)
- [CHOS-cc29ea8-kernel-uefi.elf.gz](CHOS-cc29ea8-kernel-uefi.elf.gz?raw=1)
- [BOOTAA64.EFI](BOOTAA64.EFI?raw=1)
- [CHOS-cc29ea8-source.zip](CHOS-cc29ea8-source.zip?raw=1)

Verify downloads with [SHA256SUMS](SHA256SUMS). Decompress `.gz` files before use.
The prepared image is a distribution image, not a disk modified by acceptance tests.

QEMU validation and target limitations are recorded in the source reports and optional handoff bundle. Actual Fusion validation of this build remains pending. Native NVMe is admitted through explicit read-only discovery or validated installed-boot discovery; destructive installation requires confirmation.

This is an interim runtime milestone. Follow the [Fusion test checklist](TESTING.md).
Stage 3 and version 0.50.0 remain incomplete.

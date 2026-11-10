# CHOS build 8cdb3fb


This is a verified **native Cairo graphics development milestone**, version 0.30.0. Stage 3 is incomplete. The Cairo diagnostic is not a modern browser and contains no installer. Normal Fusion installation media are separate.

- [Native Cairo diagnostic ISO](CHOS-Native-Cairo-8cdb3fb.iso?raw=1)
- [Fusion test checklist](TESTING.md)
- [Actual guest preview](CAIRO-PREVIEW.png)
- [Clean-media qualification and relink evidence](QUALIFICATION.json)

Source commit: `8cdb3fb2fb8217bbda8d9d3104be1ff11efe2694`.

- [CHOS-Fusion-8cdb3fb.iso](CHOS-Fusion-8cdb3fb.iso?raw=1)
- [CHOS-8cdb3fb-prepared.img.gz](CHOS-8cdb3fb-prepared.img.gz?raw=1)
- [CHOS-8cdb3fb-kernel-direct.elf.gz](CHOS-8cdb3fb-kernel-direct.elf.gz?raw=1)
- [CHOS-8cdb3fb-kernel-uefi.elf.gz](CHOS-8cdb3fb-kernel-uefi.elf.gz?raw=1)
- [BOOTAA64.EFI](BOOTAA64.EFI?raw=1)
- [CHOS-8cdb3fb-source.zip](CHOS-8cdb3fb-source.zip?raw=1)
- [CHOS-Fusion-8cdb3fb-Handoff.zip](CHOS-Fusion-8cdb3fb-Handoff.zip?raw=1)

Verify downloads with [SHA256SUMS](SHA256SUMS). Decompress `.gz` files before use.
The prepared image is a distribution image, not a disk modified by acceptance tests.

QEMU validation and target limitations are recorded in the source reports and optional handoff bundle. Actual Fusion validation of this build remains pending. Native NVMe is admitted through explicit read-only discovery or validated installed-boot discovery; destructive installation requires confirmation.

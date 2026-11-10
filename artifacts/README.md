# Current CHOS build downloads

Tested source: `8cdb3fb2fb8217bbda8d9d3104be1ff11efe2694`.
Publication tag: `build-8cdb3fb`.

- [Native Cairo diagnostic ISO — no installer](8cdb3fb/CHOS-Native-Cairo-8cdb3fb.iso?raw=1)
- [Fusion graphics checklist](8cdb3fb/TESTING.md)
- [Bootable ARM64 ISO](8cdb3fb/CHOS-Fusion-8cdb3fb.iso?raw=1)
- [Complete source/build/test handoff](8cdb3fb/CHOS-Fusion-8cdb3fb-Handoff.zip?raw=1)
- [Prepared disk image (gzip)](8cdb3fb/CHOS-8cdb3fb-prepared.img.gz?raw=1)
- [Source archive](8cdb3fb/CHOS-8cdb3fb-source.zip?raw=1)
- [Direct kernel (gzip)](8cdb3fb/CHOS-8cdb3fb-kernel-direct.elf.gz?raw=1)
- [UEFI kernel (gzip)](8cdb3fb/CHOS-8cdb3fb-kernel-uefi.elf.gz?raw=1)
- [EFI loader](8cdb3fb/BOOTAA64.EFI?raw=1)
- [Checksums](8cdb3fb/SHA256SUMS?raw=1)
- [Manifest](8cdb3fb/manifest.json?raw=1)

This is a native software graphics prerequisite, not the modern browser. Stage 3 remains incomplete at 0.30.0. Previous downloads, including the JavaScript diagnostic in [32b31ab](32b31ab/README.md), remain available.

Source is pushed each iteration. Binary downloads are published for selected verified milestones that are useful for user testing. Stage 3 completion remains separately gated.

See [publication instructions](../docs/ARTIFACT_PUBLICATION.md) and [installation guide](../docs/INSTALL_GUIDE.md) and [validation report](../docs/STAGE2_VALIDATION.md). Actual Fusion validation of this build remains pending. NVMe discovery is read-only; destructive installation requires explicit target selection and confirmation.

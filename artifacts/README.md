# Current CHOS build downloads

Tested source: `a89db07f60345bae56676eacfbefc7cf08f5973e`.
Publication tag: `build-a89db07`.

- [Bootable ARM64 ISO](a89db07/CHOS-Fusion-a89db07.iso?raw=1)
- [Complete source/build/test handoff](a89db07/CHOS-Fusion-a89db07-Handoff.zip?raw=1)
- [Prepared disk image (gzip)](a89db07/CHOS-a89db07-prepared.img.gz?raw=1)
- [Source archive](a89db07/CHOS-a89db07-source.zip?raw=1)
- [Direct kernel (gzip)](a89db07/CHOS-a89db07-kernel-direct.elf.gz?raw=1)
- [UEFI kernel (gzip)](a89db07/CHOS-a89db07-kernel-uefi.elf.gz?raw=1)
- [EFI loader](a89db07/BOOTAA64.EFI?raw=1)
- [Checksums](a89db07/SHA256SUMS?raw=1)
- [Manifest](a89db07/manifest.json?raw=1)

Source is pushed each iteration. Binary downloads are published for selected verified milestones that are useful for user testing. Stage 3 completion remains separately gated.

See [publication instructions](../docs/ARTIFACT_PUBLICATION.md) and [installation guide](../docs/INSTALL_GUIDE.md) and [validation report](../docs/STAGE2_VALIDATION.md). Actual Fusion validation of this build remains pending. NVMe discovery is read-only; destructive installation requires explicit target selection and confirmation.

This checkpoint provides a [separate native engine diagnostic ISO](a89db07/CHOS-Native-Engine-a89db07.iso?raw=1), with [Fusion instructions](a89db07/TESTING.md), [exact-binary validation](a89db07/VALIDATION.json) and [build/relinking records](a89db07/ENGINE-REBUILD.json). The normal ISO is installation media. Modern browser rendering and full Stage 3 acceptance remain open.

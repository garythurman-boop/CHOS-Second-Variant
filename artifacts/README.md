# Current CHOS build downloads

Tested source: `cc29ea83a73e3cb2d6b753645360fd769f921890`.
Publication tag: `build-cc29ea8`.

- [Bootable ARM64 ISO](cc29ea8/CHOS-Fusion-cc29ea8.iso?raw=1)
- [Prepared disk image (gzip)](cc29ea8/CHOS-cc29ea8-prepared.img.gz?raw=1)
- [Source archive](cc29ea8/CHOS-cc29ea8-source.zip?raw=1)
- [Direct kernel (gzip)](cc29ea8/CHOS-cc29ea8-kernel-direct.elf.gz?raw=1)
- [UEFI kernel (gzip)](cc29ea8/CHOS-cc29ea8-kernel-uefi.elf.gz?raw=1)
- [EFI loader](cc29ea8/BOOTAA64.EFI?raw=1)
- [Checksums](cc29ea8/SHA256SUMS?raw=1)
- [Manifest](cc29ea8/manifest.json?raw=1)

Source is pushed each iteration. Binary downloads are published for selected verified milestones that are useful for user testing. Stage 3 completion remains separately gated.

See [publication instructions](../docs/ARTIFACT_PUBLICATION.md) and [installation guide](../docs/INSTALL_GUIDE.md) and [validation report](../docs/STAGE2_VALIDATION.md). Actual Fusion validation of this build remains pending. NVMe discovery is read-only; destructive installation requires explicit target selection and confirmation.

**Interim runtime test build; Stage 3 remains incomplete.**
Follow the [Fusion checklist](cc29ea8/TESTING.md) and report its final runtime test output.

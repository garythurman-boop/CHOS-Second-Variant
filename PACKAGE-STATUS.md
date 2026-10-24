# CHOS Stage 1 development status

Current source and validation supersede the original 0.30.0 prebuilt handoff. See
[the current report](docs/STAGE1_FINAL_REPORT.md), [driver status](docs/DRIVER_STATUS.md),
[hardware matrix](docs/HARDWARE_SUPPORT.md) and [test checklist](docs/STAGE1_TEST_CHECKLIST.md).
`prebuilt/` remains the original historical baseline; rebuild current main or use a
revision-labelled delivery package with its SHA-256 manifest.

Clean direct/UEFI/prepared builds, aggregate host/sanitizer checks and live QEMU
MMIO/PCI VirtIO plus native USB profiles pass. This includes hub/tablet input,
persistence, network/entropy, failure containment and explicitly enabled disposable
NVMe testing. Standard builds disable NVMe probing. No physical disk is used by the
automated live tests.

Cross-platform Stage 1 remains open: the corrected ISO needs Fusion retesting, and
Pi 5/laptop tests require their hardware. General IOMMU ownership, generic UEFI EL0
process execution, native vmxnet3, TLS/HTTPS and an on-device installer are not
established. HTTPS continues to fail closed without peer verification.

Original baseline details and development failures remain in
[historical validation](docs/STAGE1_VALIDATION.md) and the
[Fusion follow-up](docs/STAGE1_FUSION_FOLLOWUP.md). CHOS is not an acronym.

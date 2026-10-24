# CHOS development instructions

Use the existing checkout. Do not create a Git worktree unless the user requests one.
Read docs/STAGE1_VALIDATION.md, docs/DRIVER_STATUS.md and docs/HARDWARE_SUPPORT.md
before changing boot, driver, DMA, interrupt or input contracts.

## Delivery after each iteration

The user explicitly requests that source be pushed to GitHub after every iteration.
The repository is https://github.com/garythurman-boop/CHOS-Second-Variant, branch main.
Commit coherent changes with the applicable verification and push main after each
completed iteration; confirm the remote SHA with git ls-remote. Do not force push,
rewrite published history or overwrite concurrent remote changes. If another change
arrives remotely, integrate it while preserving user work. Report any push failure.
Keep test results and support claims accurate; retain known failures with a diagnosis.
Do not publish credentials, generated disposable disks or cache directories.

## Build and verification

On the prepared cloud machine:
  source /workspace/chos-tools/activate
  make CC=clang LD=ld.lld -j4 all uefi-iso install-disk-image build/qemu-smoke-disk.img build/userland/rng_test.chcx
  make test CC=clang LD=ld.lld -j1
  make test-sanitize CC=clang LD=ld.lld -j1
  make qemu-smoke CC=clang LD=ld.lld

Use tests/qemu_platform.py with a fresh output directory for live acceptance checks.
It snapshots kernels/ISOs, records hashes and uses only disposable disks and firmware
variable copies. Preserve baseline evidence outside build before make clean. Run checks
appropriate to the change; documentation-only iterations need link/command review and
git diff --check, not a redundant kernel rebuild.

## Stage 1 scope

Stage 1 is incomplete. Native IOMMU ownership,
PCI VirtIO support in the ACPI-only UEFI runtime, and target-specific networking are
not completed. Some gaps can be implemented/tested in QEMU; do not describe all
remaining work as blocked solely by unavailable physical hardware.
QEMU evidence does not establish VMware, Pi 5 or laptop support. Do not enable
experimental NVMe probing on valuable media. Never disable TLS, signature or artifact
checksum verification to make setup pass. CHOS is not an acronym.

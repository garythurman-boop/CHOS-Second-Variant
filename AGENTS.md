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
Do not publish credentials, per-test mutable disposable disks or cache directories.
The official prepared distribution image may be published compressed.

### Binary artifact cadence

The user requests ISO files and related downloads on GitHub now and after every
SECOND future completed implementation iteration. Source still goes to main after
every iteration. The current publication is the baseline: the next iteration is
source-only, the following one publishes artifacts, and that alternation continues.
Do not count bookkeeping commits or individual commits inside one iteration.

Use artifacts/publication.json as the persisted counter and latest-build pointer.
After committing an implementation iteration, run python3 tools/publish_artifacts.py
--iteration --source-ref <that-source-commit> once; duplicate calls for the same
source commit do not advance it. On artifact iterations, rebuild the committed
source cleanly first and validate the applicable build, then run the helper. It
refuses dirty/mismatched kernels and updates the counter only after staging succeeds.
The --publish-now option handles an explicit request outside the normal cadence.

Commit the counter and any artifacts as a separate bookkeeping commit and push
main. When artifacts were staged, tag that publication commit build-<source-sha7>
and push the new tag without replacing an existing tag. Confirm the remote SHA,
tag and published files. Staging files locally is not publication. GitHub Releases
is preferred when its authenticated API is available; the current environment
blocks that API, so artifacts/<source-sha7>/ contains versioned repository downloads.
Keep each tracked file below GitHub's size limit; gzip prepared disks and kernels.
Do not rebuild the kernel merely for publication-only documentation/tool changes.
See docs/ARTIFACT_PUBLICATION.md for commands and release verification.

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

Stage 1 is incomplete. Native IOMMU ownership and target-specific networking are
not completed. PCI VirtIO block/network/RNG in ACPI-only UEFI now have QEMU evidence;
UEFI EL0/process runtime is still outside the validated profile. Some gaps can be implemented/tested in QEMU; do not describe all
remaining work as blocked solely by unavailable physical hardware.
QEMU evidence does not establish VMware, Pi 5 or laptop support. Do not enable
experimental NVMe probing on valuable media. Never disable TLS, signature or artifact
checksum verification to make setup pass. CHOS is not an acronym.

## Revised Stage 3

The user explicitly supersedes the Word document's Stage 3 with
`docs/STAGE3_REVISED_PROMPT.md`. Continue the modern browser/network-manager/Wi-Fi
objective without asking for another approval. The old text-preview browser is
recovery functionality, never the revised browser deliverable. Track actual engine,
TLS, isolation, wireless, IPv6 and platform gates in STAGE3_ARCHITECTURE.md and
BROWSER_ENGINE_PORT.md; do not claim the full stage from Ethernet/manager progress.
The user confirms 107ef51 power/storage/Console changes work in Fusion, but its
actual NIC is Intel 8086:10d3. The user now confirms Fusion Ethernet/example.com work after 54b9731. Keep this
user-reported evidence distinct from automated QEMU tests. The selected wireless
target is Pi 5 CYW43455 over SDIO; do not regress Ethernet while adding it.

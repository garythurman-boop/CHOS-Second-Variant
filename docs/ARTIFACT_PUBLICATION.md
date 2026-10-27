# GitHub build publication

The user now authorizes interim binaries when a critical milestone is ready for
user testing. This supersedes the earlier Stage 3 binary hold and automatic
every-other-iteration cadence. Source is still pushed after each verified iteration.
`manual_milestones` in `artifacts/publication.json` keeps ordinary `--iteration`
source-only; `--publish-now` selects a tested milestone explicitly. Existing
version directories and tags remain immutable. Every interim build needs a concrete
Fusion test checklist, recorded QEMU evidence, and explicit outstanding gates.
Version 0.50.0 remains reserved for complete revised Stage 3 acceptance.

[Download the current build](../artifacts/README.md). Machine-readable state and the
latest source/tag/directory are in `artifacts/publication.json`. Binaries identify
their tested source commit, which may precede documentation/publication commits.
The historical `prebuilt/` directory remains the original 0.30.0 baseline.

## Normal workflow

1. Complete the implementation and applicable tests, commit its source, and retain
   that full commit as the iteration's source reference. Continue pushing source
   after each iteration.
2. Run `python3 tools/publish_artifacts.py --iteration --source-ref SOURCE_COMMIT`
   once per completed iteration. Manual milestone mode records source work without
   automatically staging artifacts. Repeating the same source ref is idempotent.
3. For an explicitly selected user-test milestone, build the committed source cleanly first,
   then use --publish-now. The helper
   checks both embedded kernel build IDs and that the ISO contains the same EFI
   loader/kernel being staged. Use `--handoff PATH.zip` to include a prepared
   source/test-evidence handoff whose README identifies the same full source commit.
4. Review the files/counter, commit them, and push main. Tag the publication commit
   `build-SOURCE_SHA7` and push that tag. Do not replace a published tag or overwrite
   a version directory. Verify the remote SHA/tag and asset hashes before linking it.

An explicit publication outside this cadence uses `--publish-now`, resetting the
counter. The helper only stages files: it does not build, run tests, commit, tag or
push. Publication failures must be reported; a local staging success alone is not
an upload. If staging fails, the cadence counter is not advanced.

## Downloads and integrity

Each version directory contains the standalone ARM64 ISO, gzip-compressed prepared
image and direct/UEFI kernels, `BOOTAA64.EFI`, a source archive, optional complete
handoff, `manifest.json` and `SHA256SUMS`. The manifest gives both compressed and
uncompressed hashes/sizes for gzip assets. Check downloads before use:

```sh
sha256sum -c SHA256SUMS
gzip -dk CHOS-SHA7-prepared.img.gz
```

The prepared image is the official generated distribution image. Per-test disks,
firmware-variable copies and caches are excluded. Source ZIP exports omit the
`artifacts/` tree, so binary history does not recursively enter later source bundles.
Tagged publication URLs are immutable; README's current-build links are maintained
with each artifact publication.

GitHub's Releases API (`api.github.com`) is blocked by the current environment's
network proxy. Existing authenticated HTTPS Git push works, so the first publication
uses versioned files in this repository. A tracked file is capped at 95 MiB by the
helper to stay below GitHub's limit; larger future assets need an available Releases
or LFS upload path. Authentication secrets must never enter the artifacts or source.

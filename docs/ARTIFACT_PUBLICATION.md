# GitHub build publication

The latest user instruction places new binary publications on hold until revised
Stage 3 is complete and thoroughly tested. This supersedes the cadence below.
Source pushes and local build/testing continue. The active release_hold in
artifacts/publication.json prevents the helper from staging even with --publish-now.
Do not clear it to publish a partial milestone. Existing downloads remain available.

Source goes to GitHub after every completed iteration. The user's updated preference
publishes ISO and related binary files after every other future implementation
iteration. Build `2ed3e6d` was the initial publication. The latest pointer and
iteration counter are persisted below; after each artifact publication the next
iteration is source-only and the following one publishes artifacts. Bookkeeping
commits do not count as separate implementation iterations.

[Download the current build](../artifacts/README.md). Machine-readable state and the
latest source/tag/directory are in `artifacts/publication.json`. Binaries identify
their tested source commit, which may precede documentation/publication commits.
The historical `prebuilt/` directory remains the original 0.30.0 baseline.

## Normal workflow

1. Complete the implementation and applicable tests, commit its source, and retain
   that full commit as the iteration's source reference. Continue pushing source
   after each iteration.
2. Run `python3 tools/publish_artifacts.py --iteration --source-ref SOURCE_COMMIT`
   once per completed iteration. A source-only iteration updates the counter;
   the next iteration stages artifacts. Repeating the same source ref is idempotent.
3. When artifacts are due, build the committed source cleanly first. The helper
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

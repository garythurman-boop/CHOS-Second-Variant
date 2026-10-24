# CHOS 0.20.1-dev release notes

## Purpose

This is a focused build-fix release based on 0.20.0-dev. The user’s clean build failed in `userland/examples/browser.c` because three static globals were assigned but never read, which Clang correctly rejected under `-Werror`.

## Changes

- Removed the unused `request_queued`, `connection_polls`, and `page_http_status` variables and their assignments. Browser navigation continues to use its `browser_phase_t` state machine and the user-visible `status_line`.
- Updated runtime/build version labels and the UEFI ISO output filename to 0.20.1-dev.
- No networking behavior, TCP semantics, or HTTPS behavior was changed. HTTPS remains deliberately blocked because a maintained TLS/X.509 implementation and verified trust store are not integrated.

## Validation status

This release must pass a clean AArch64 build, browser-core tests, userland app packaging, TCP/network tests, and ASan/UBSan. The UEFI ISO is experimental; structural validation does not prove firmware boot. The aggregate `make test` can exceed the available command runtime during the Python CHFS image-tool stage; report any separately run checks accurately rather than implying the aggregate finished.

## Build

```sh
make clean
make CC=clang LD=ld.lld
make test-browser-core
make test-userland CC=clang LD=ld.lld
make test-sanitize
make test-uefi-iso
make uefi-iso
```

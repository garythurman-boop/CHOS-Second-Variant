# CHOS 0.21.1-dev validation record

Date: 2026-10-23

## Verified in this build environment

- Clean AArch64 build of `build/chos.elf` using Clang/LLD and `-Werror`.
- UEFI ARM64 PE/COFF loader built and packaged into the ISO.
- Browser CHCX package verified at 57,344 bytes virtual size, under the 65,536-byte image limit.
- `make test`: passed all configured host regression tests, packaging tests, ISO validation and local browser fixture tests.
- `make test-sanitize`: passed the configured AddressSanitizer and UndefinedBehaviorSanitizer tests.
- `make test-userland userland-image`: passed userland application packaging and syscall API compile probes.
- `make test-uefi-iso`: passed ISO9660, El Torito EFI section, FAT16 ESP and AArch64 EFI executable structure checks.
- The deterministic HTTP fixture was exercised locally for the supported redirect status codes, loop route, relative-link pages and misleading `Content-Type` response.

## Not verified here

- The rebuilt ISO has not been booted in a live QEMU or VMware Fusion window by this build environment. The user must verify the actual VM boot and browser behavior. The ISO structure being valid is not proof of firmware boot compatibility.
- Live browser tests with the rebuilt application are still required for mouse-link activation, Tab/Enter focus, repeated navigation, redirects and delayed TCP shutdown.
- Full CSS layout, JavaScript, actual image rendering, TLS/HTTPS, forms and downloads remain unsupported by design.

# CHOS 0.17.2-dev — Portable host-test build fix

## Fix

The `tests/test_platform_boot.c` regression test contained `store_be32`, which was only used inside a Linux-only `mmap` test branch. On macOS, that branch is not compiled, so Clang with `-Werror` rejected the unused static helper. The helper is now guarded by the same `__linux__ && MAP_FIXED_NOREPLACE` condition as its call site.

## Validation performed

- Clean freestanding AArch64 build with Clang/LLD and `-Wall -Wextra -Werror`: passed.
- Userland applications compiled, linked, packed and CHCX-verified: passed.
- Complete ASan/UBSan suite: passed.
- Explicit non-Linux conditional-compilation test of `test_platform_boot.c`: passed with `-Werror`.
- Networking regression, including DHCP, ARP, DNS, ICMP echo and bounded timeout: passed.
- All C regression binaries passed during `make test`; the aggregate command hit its execution limit at the Python CHFS image-tool stage. `tests/test_mkchfs.py -v` passed all seven tests when run separately.
- CHCX packager, userland stack-page checks and system-app synchronization tests: passed separately.

No kernel networking behavior changed in this hotfix. No compiler warnings or checks were disabled. Live QEMU runtime verification is not claimed.

# CHOS 0.17.1-dev — Build-fix release

## Fixed

The 0.17.0-dev kernel build failed on macOS Clang when `-Werror` promoted two `-Wunused-but-set-global` diagnostics in `kernel/net/net_stack.c` to errors. `lease_started` and `rebind_deadline` were assigned during lease setup/reset but never read. They have been removed, preserving the existing renewal/retry/lease-expiry behavior. No warnings were suppressed and no compiler flags were weakened.

## Validation performed

- Clean freestanding AArch64 build with Clang/LLD and `-Wall -Wextra -Werror`: passed.
- `make test-net-stack`: passed (DHCP lease/renewal, ARP learning, DNS A lookup, ICMP echo matching, and bounded ping timeout).
- `make test-sanitize`: passed.
- `make test-userland CC=clang LD=ld.lld`: passed; all CHCX apps packed and verified.
- `python3 tests/test_mkchfs.py -v`: all 7 tests passed independently, including v1/v2 compatibility and v3 400-directory capacity.
- The aggregate `make test` command did not complete within the environment's five-minute command limit; it was terminated while running `tests/test_mkchfs.py` after printing two progress dots. That same seven-test Python suite passed independently in about 30 seconds. I therefore do not report the aggregate `make test` command as a pass.
- Live QEMU boot/runtime validation was not performed in this environment.

## Scope

This patch corrects the build failure only. It does not add new networking functionality. CHOS's network service remains polled; a TCP state machine and socket API, TLS/X.509 verification, HTTPS, the EFI loader/installer, and browser remain unfinished.

## Build on macOS

```sh
make clean
make CC=clang LD=ld.lld
make test-net-stack
make test-sanitize
make test-userland CC=clang LD=ld.lld
make run-gui CC=clang LD=ld.lld
```

Run commands one at a time so a failed step does not get hidden by a later command. `make run-gui` requires QEMU to be installed. Preserve and back up existing disk images before booting a kernel that can migrate the disk format.

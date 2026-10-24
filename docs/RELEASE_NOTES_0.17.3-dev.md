# CHOS 0.17.3-dev — DHCP packet padding fix

## Symptom addressed

In QEMU user-mode networking, CHOS reported a VirtIO link up and repeated TX completions but zero RX frames. DHCP remained in `discovering`, so DNS and ping correctly refused to start without an IPv4 configuration.

## Likely protocol-level cause

The emitted DHCP Discover and Request payloads were only 259 and 271 bytes. A number of DHCP/BOOTP implementations historically expect a minimum 300-byte message. CHOS now pads these messages to 300 bytes using DHCP PAD (option code 0) bytes before a final END (option code 255). Renewal/rebinding DHCPREQUEST packets use the same minimum.

This is a plausible fix for the reported behavior, not a claim of live confirmation. After booting this build, check `netinfo` and `netstatus`: RX should increment and DHCP should transition from `discovering` to `requesting` and then `bound`. If RX remains at zero, the VirtIO RX queue path needs additional runtime diagnostics.

## Validation

- DHCP Discover and Request builders now assert exactly 300-byte output and correct PAD/END placement.
- `make test-net-services` and `make test-net-stack` passed.
- ASan/UBSan suite passed after the packet changes.
- A fresh freestanding AArch64 build and live QEMU DHCP test are being distinguished from host-only tests; live QEMU verification is not available in this environment.

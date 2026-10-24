# CHOS 0.19.0-dev release notes

## Summary

This development snapshot consolidates the CHFS v3 and live-validated L2/L3 networking work from 0.16/0.17, the experimental UEFI boot-image generator from 0.18, and a new bounded non-blocking outbound TCP client. The original CHOS desktop, File Explorer, CHFS migration, and network diagnostics remain.

## New TCP functionality

- Four fixed TCP client socket slots; no dynamic allocation.
- Non-blocking connect, handshake processing, sequence/ACK validation, bounded retransmission and timeout.
- Bounded 512-byte MSS, 2048-byte receive buffer per socket, one outstanding segment per socket.
- In-order payload receive, `tcpsend` / `tcprecv` diagnostics, RST error reporting, FIN close and TIME-WAIT.
- Network-service integration through IPv4, ARP, VirtIO-net and the shared TCP checksum parser/builder.
- Serial-shell commands: `tcpconnect IPv4 port`, `tcpstatus`, `tcpsend HANDLE text`, `tcprecv HANDLE`, `tcpclose HANDLE`. Escapes `\r`, `\n`, `\t` are expanded by `tcpsend`.
- EL0 syscall ABI IDs 24–29 provide `chos_net_tcp_open/connect/status/send/recv/close` wrappers for process-owned sockets. Status/send/receive buffers are bounded and validated against the calling address space; sockets are automatically released when a process exits.
- Tests cover socket-owner isolation, handle cleanup/reuse, the userland wrapper compile contract, and host TCP state-machine behavior.
- Host regression tests for handshake, retransmission, payload exchange, graceful shutdown, timeout and local-address loss; an integration test exercises TCP after mock DHCP/ARP configuration.

## Boot-image groundwork

`make uefi-iso` builds `build/chos-0.19.0-dev.iso`: ISO9660/El Torito plus FAT16 ESP. This is structurally tested but has not been booted in live UEFI firmware in this environment. It is not a graphical installer and does not create an installable GPT/ESP layout on a target disk.

## Validation performed

- Clean AArch64 build using Clang/LLD and `-Werror`.
- Host TCP state-machine and integrated network-stack tests.
- ASan/UBSan host regression suite.
- `make test-sanitize` passes, including the TCP ownership and stack integration tests.
- Userland packaging and the new freestanding TCP API compile probe pass.
- All seven `tests/test_mkchfs.py` tests pass independently, as do the CHCX packer tests, stack reservation tests, and system-app synchronization tests.
- UEFI image builder and structural tests pass. The aggregate `make test` exceeds this environment's five-minute command limit in the slow CHFS image-tool stage; those Python tests pass independently, so the aggregate target is not reported as a complete pass.

The development environment does not provide a live QEMU/UEFI firmware test, so the new TCP commands should still be validated on the user's QEMU setup. DHCP, ARP, DNS and ICMP were confirmed in live QEMU before this change.

## Known limitations

- Outbound TCP client only; no listening/accept API, full congestion control or advanced out-of-order reassembly. The new process-owned syscall API is still development-grade and lacks broad live-external-server validation.
- TLS, X.509 certificate verification, HTTPS and a browser are not implemented. Do not use for sensitive data.
- UEFI boot path is experimental; no graphical installer or verified installed-disk boot.
- User-provided live QEMU results confirmed DHCP binding, ARP replies, DNS A resolution and ICMP echo to the gateway in 0.17.5-dev. Live TCP and live UEFI firmware boot are still unverified.

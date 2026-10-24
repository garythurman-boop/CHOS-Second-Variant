# CHOS 0.19.1-dev release notes

## TCP CLOSE-WAIT correction

A TCP peer FIN closes the peer-to-local sending direction but does not prevent the local client from transmitting. CHOS now accepts application sends in `CHOS_TCP_CLOSE_WAIT` in addition to `CHOS_TCP_ESTABLISHED`. The client can finish sending queued application data before calling `tcpclose` to send its own FIN.

`tcpsend` reports a more specific diagnostic when a send is deferred due to an outstanding segment, zero remote window, or a non-sendable socket state.

## Validation

- Clean AArch64 Clang/LLD build with `-Werror`.
- Focused TCP client and network-stack host regressions pass, including peer-FIN/CLOSE-WAIT send and local FIN transition.
- Targeted ASan/UBSan TCP and network-stack tests pass.
- Userland build and TCP syscall compile probe pass.
- Live external TCP still requires QEMU verification; some external servers may close idle connections, so issue the request immediately after the handshake is established.

TLS/X.509, HTTPS and browser support remain unfinished.

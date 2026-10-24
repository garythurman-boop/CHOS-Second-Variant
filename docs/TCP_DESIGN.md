# CHOS TCP client design — 0.20.2-dev

## Scope

`chos/net_tcp.h` exposes a bounded, non-blocking **outbound client**. It is intended for incremental bring-up and testing, not as a complete or security-reviewed TCP/IP stack.

- Up to four socket records, no dynamic allocation.
- Ephemeral ports begin at 49152; a caller may also request a fixed local port.
- One outstanding segment per socket; MSS 512 bytes; 2048-byte receive ring per socket.
- SYN/SYN-ACK/ACK handshake, sequence and acknowledgement tracking, advertised receive window, retransmission with bounded exponential backoff, bounded timeout, RST handling, in-order receive, FIN close and TIME-WAIT.
- Output callback routes through current IPv4 configuration and ARP lookup before building a checksummed TCP/IPv4 frame.
- Input arrives only after Ethernet, IPv4, TCP bounds and TCP pseudo-header checksum validation.
- `chos_tcp_poll()` runs from the existing network service poll loop; it does not busy-wait.

## Deliberate limitations

No passive open/listen/accept, simultaneous open, SACK, urgent data, advanced congestion control, sophisticated out-of-order reassembly or zero-window persist are provided. Only one unacknowledged segment may be outstanding per socket. A limited process-owned syscall API is now available as IDs 24–29, with `chos_net_tcp_*` wrappers in `chos/user.h`. Each socket handle belongs to its creating process and is released when that process exits. Applications must check return values and poll state; `-EAGAIN` indicates that a non-blocking operation is not ready. Do not use this client for sensitive information or assume production-grade Internet interoperability.

A user-provided live QEMU run confirmed the basic external TCP/HTTP path, including an HTTP 200 response from `104.20.23.154:80`. This is not a claim of complete TCP compliance; the client remains bounded and development-grade. TLS is not implemented. Before HTTPS support, CHOS needs a maintained TLS/crypto library, reliable time, cryptographic entropy, a trust store, fail-closed certificate-chain and hostname verification, plus a more fully validated TCP socket service. The code is not a substitute for a maintained transport stack; a future release should evaluate a portable existing stack if licensing and OS adaptation fit.

## Manual serial-shell test

After DHCP binds:

```text
dnslookup example.com
netstatus
# Use the resolved IPv4 address below.
tcpconnect 93.184.216.34 80
tcpstatus
# repeat tcpstatus until established
tcpsend 1 GET / HTTP/1.0\\r\\nHost: example.com\\r\\nConnection: close\\r\\n\\r\\n
tcpstatus
tcprecv 1
tcpclose 1
```

DNS output is asynchronous, as is TCP connect. These commands depend on the resolved address, outbound routing, and the target accepting TCP connections. Port 80 may be filtered on some networks. Always inspect `tcpstatus` and use `netinfo` / a PCAP if it fails.

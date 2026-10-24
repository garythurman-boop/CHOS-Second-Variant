# CHOS networking API model — 0.12.0-dev

This document records the application-facing interface shape to guide the later networking implementation. **0.12.0-dev does not implement networking.** There is no Ethernet driver, IP stack, DHCP/DNS implementation, TCP stack, TLS support, socket syscall or working socket descriptor yet.

## Design constraints

- Keep networking outside the privileged GUI and do not expose kernel pointers to applications.
- Use fixed-width structures with explicit version and size fields.
- Treat addresses and ports as network-order byte sequences to avoid ambiguous host-endian conversions.
- Bound sockets per process, packet/stream buffers, and individual I/O requests.
- Return the same negative-errno convention used by existing CHOS syscalls.
- Keep transport/driver details behind a service interface so QEMU, VMware Fusion and Raspberry Pi drivers can vary independently.
- Do not implement cryptography in CHOS from scratch. HTTPS should use a maintained TLS library after time, entropy and certificate validation services are available.

## Header contract

`include/chos/net.h` defines `chos_socket_t`, IPv4 address and socket-address layouts, a socket state enumeration, and a versioned socket-information record. Static assertions pin the structure sizes. These definitions are a design draft only, not callable services and not a stable public ABI.

## Planned implementation stages

The 0.13.x networking milestone should first introduce the device abstraction and one QEMU Ethernet backend, then ARP/IPv4/UDP, DHCP/DNS, TCP, and a syscall-based socket service. Before browser work, HTTPS must validate certificate chains and hostnames, have trustworthy time and cryptographic entropy, and provide tests for expired, untrusted and hostname-mismatched certificates. The browser's rendering engine and networking code should execute in user space with bounded resources.

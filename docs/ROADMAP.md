# CHOS roadmap — combined 0.16–0.20 effort

## Delivered groundwork in 0.16.0/0.17.0-dev

### Filesystem and platform stability
- CHFS v3 with redundant 64-sector catalogues and 511 combined file/directory records.
- Guarded migration of legacy v1 volumes through v2 to v3, with safe fallback when data relocation cannot proceed.
- Tests for capacity, migration, cross-directory moves, checksums, remount and failed migration.
- Host image-tool validation optimized to keep repeated catalogue operations practical.
- Allocator scratch storage moved off the kernel task stack.

Known remaining filesystem limits: the path field is still 48 bytes (up to 47 visible characters), file extents are contiguous and up to 4 MiB, file writes from EL0 are capped at 64 KiB per syscall, and there is no full journal or block allocation map. A future CHFS v4 should add allocation metadata, more robust transaction records, streaming file growth, and tests that can recover from injected block-write failures without overstating power-loss guarantees.

### Networking groundwork
- Non-blocking DHCPv4 offer/request/ACK, client/server validation, lease renewal/expiry, retry/backoff and rebinding.
- ARP cache and request/reply behavior.
- Asynchronous DNS A-query path with validation and retries.
- ICMPv4 echo and bounded `ping` retries.
- Checksummed TCP segment parser/builder primitives.

The current network service is polled in the kernel runtime loop. The limited TCP client is exposed to EL0 through process-owned non-blocking syscalls, but it is not a complete standards-compliant transport stack. The user confirmed live QEMU DHCP binding, DNS A resolution, ICMP echo, and a successful external TCP/HTTP exchange returning `HTTP/1.1 200 OK`. TLS/X.509 remain unimplemented.

## 0.18.x — Experimental UEFI image groundwork (structurally validated; live boot pending)

Prerequisites before calling an ISO installable:
1. Design a UEFI/EFI AArch64 boot path, including kernel ELF loading, memory-map cleanup and an explicit device-tree handoff instead of depending only on the QEMU `-kernel` memory placement.
2. Build a reproducible EFI system partition and ISO9660/El Torito image.
3. Design the target-disk format and installer storage API. A whole-disk CHFS volume is not yet partitioned and cannot simply become a standard EFI-bootable installed disk.
4. Add an installer interface, safe disk-selection/confirmation flow, progress/error handling and post-install first boot.
5. Validate in QEMU/VMware; don't release an untested ISO as bootable.

The current build environment does not include QEMU, xorriso/GRUB ISO tools, FAT-image tools or working package-repository network access. The existing kernel is QEMU-virt-specific and reads a fixed DTB location. These are real gaps, not just a packaging task.

## 0.19.x — Bounded outbound TCP client (basic live external HTTP confirmed)

- Added four fixed socket slots, ephemeral local ports, SYN/SYN-ACK/ACK handshake, sequence and ACK tracking, bounded retransmission and timeout, send/receive ring buffers, RST handling, FIN close and TIME-WAIT.
- Connected TCP to the existing polled network service through ARP and checksummed IPv4/TCP builders/parsers.
- Added serial shell commands `tcpconnect`, `tcpstatus`, `tcpsend`, `tcprecv`, and `tcpclose`.
- Host tests cover the handshake, retransmission, payload send/receive, graceful close, timeout, address loss, and integration through the mock network adapter.
- Added process-owned EL0 syscalls 24–29 and `chos_net_tcp_*` wrappers for open/connect/status/send/receive/close, with checked user buffers and cleanup on process exit.
- Added ownership-isolation/release tests plus a compile-only test of the userland API.
- User confirmed live external HTTP over TCP in QEMU: connection to `104.20.23.154:80`, a 56-byte request, and an HTTP `200 OK` response. This validates the basic end-to-end client path, not comprehensive TCP standards compliance.
- Remaining: passive open/listen/accept, robust out-of-order handling, full congestion/window control, broad adverse-network testing, and timeout/reset testing against external services.

## 0.19.x — Secure transport (still not implemented)

- Evaluate a maintained TLS/cryptographic library such as [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) for portability, licensing, code size, configuration, and freestanding integration; no TLS library is currently vendored or linked.
- Establish a trustworthy clock, a cryptographically secure entropy source, and a maintained CA trust store.
- Verify X.509 chains, expiry, key usage, and hostname matches. Invalid certificates must fail closed.
- Budget for a system TLS service or larger application image: the current browser image is deliberately kept below the 64 KiB user-image limit.
- Do not treat TCP success or an HTTP response as HTTPS support.

## 0.20.0-dev — CHOS Web Browser preview (initial implementation)

- Added an original CHOS-branded user app to ROMFS and disk synchronization, launched through the Start menu.
- Implemented HTTP GET over the process-owned TCP API, async DNS, bounded response buffering, status/header parsing, chunked-transfer decoding, safe text extraction, history, bookmarks and scrolling.
- The 0.20.2-dev drawing-budget fix makes browser status/content rows visible in the GUI.
- HTTPS is intentionally rejected; no downgrade occurs.



## 0.24.0-dev — firmware boot-information and framebuffer foundation

- Removes the constrained 1–2 GiB UEFI kernel allocation policy and searches conventional-memory ranges from the firmware map (subject to the current 48-bit physical-address ceiling and identity-addressable early kernel design).
- Passes a versioned boot-info structure containing the final EFI memory map, kernel physical span/entry, optional device tree and ACPI RSDP, and GOP framebuffer mode.
- Adds a separate UEFI kernel entry path and a framebuffer diagnostic console. The console reports memory totals/ranges and validates basic ACPI table signatures/checksums for discovery of MADT/APIC, GTDT, and MCFG.
- Keeps regular QEMU direct boot as `ET_EXEC`, with a separate UEFI `ET_DYN` image. Boot-mode/entry-symbol tests guard this separation.
- **Not full VMware support:** the UEFI path ends in a safe diagnostic halt. Production PMM/MMU and drivers remain QEMU `virt` specific; UEFI memory-map integration, ACPI interrupt/timer use, and VMware input/storage/network drivers remain later work.

## 0.24.0-dev — UEFI loader relocation and boot diagnostics

- Replace the fixed 0x40200000 kernel allocation with a bounded relocatable AArch64 PIE load and checked `R_AARCH64_RELATIVE` fixups.
- Validate that ELF PT_DYNAMIC points at the same file-backed bytes described by PT_LOAD, and validate the kernel/EFI architecture before ISO packaging.
- Emit distinct diagnostics for unreadable kernel file, malformed ELF, allocation status/range, relocation failure, and missing firmware FDT.
- Keep the existing QEMU desktop/network/browser behavior as a regression baseline.
- Remaining hard blocker: the current kernel platform is still QEMU `virt`-specific. A VMware Fusion desktop requires ACPI/SystemReady discovery and platform drivers for its timer/interrupt, console/display, input, storage, and network hardware. A successful EFI loader or kernel allocation alone is not counted as VMware compatibility.

## 0.21.1-dev — Browser/TCP stabilization and EFI catalog compatibility

- Bound browser close/redirect waits and added a process-owned abort path that releases sockets without waiting indefinitely for the peer.
- Added FIN-WAIT-2 expiry to prevent a remote peer from leaking sockets by withholding its FIN.
- Canonicalized addresses before saving history or resolving links; added line hit-target fallback, Tab/Enter link focus and improved semantic text preview styling.
- Added a deterministic local HTTP fixture for redirect, relative-link, HTML-sniffing, and slow-close checks.
- Changed the ISO El Torito catalog to a conventional explicit EFI platform section and expanded structural regression validation.
- Acceptance still requires live QEMU navigation and VMware Fusion boot checks; the ISO builder's structural test alone cannot prove firmware compatibility.

## 0.21.0-dev — Browser navigation and safe text rendering

- Added bounded 301/302/303/307/308 redirect handling, relative URL resolution and loop limits; redirects to HTTPS fail closed.
- Added strict Location/Content-Length parsing, duplicate length conflict rejection, and response completion based on declared body length when available.
- HTML anchors now have bounded text spans with clickable target resolution; headings and list items receive semantic text markers; HTML comments are stripped; image alt text becomes a readable placeholder.
- JavaScript remains inert, CSS layout and actual image display remain unsupported, and HTTPS still requires a maintained TLS/X.509 implementation with clock, entropy and trust store support.

## Validation and release policy

Every stage must build with Clang/LLD and `-Werror`, pass host regressions and ASan/UBSan, verify the release ZIP from a fresh extraction, and document tests that did not complete. ISO/GUI/browser work also needs live runtime tests on a host with QEMU and the required firmware/image tools. This environment currently lacks those runtime/image tools, so later milestones remain explicitly pending rather than being marked done without evidence.


## 0.17.4-dev network debug note

Live QEMU testing still reports zero RX frames even after DHCP packet padding. Use `make run-gui-nettrace` and the `netinfo` raw RX queue diagnostics to distinguish packets not returned by the QEMU backend from RX queue completion faults. Do not treat the mock network tests as proof of working live DHCP.


## 0.17.5-dev network framing fix

The captured QEMU packet showed a modern VirtIO-net header-length mismatch: the driver negotiated `VIRTIO_F_VERSION_1` but prepended/stripped only the 10-byte legacy header. The modern `virtio_net_hdr_v1` is 12 bytes, including `num_buffers`; this shifted the packet by two bytes and made the Ethernet EtherType appear as `0x4500`. The driver now uses the 12-byte shared ABI for TX and RX. Verify on the user's QEMU host that the PCAP shows broadcast destination `ff:ff:ff:ff:ff:ff`, source MAC `52:54:00:12:34:56`, EtherType `0x0800`, followed by DHCP IPv4/UDP. Then confirm RX used index advances and DHCP binds.

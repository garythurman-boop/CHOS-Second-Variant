# IPv6 control-plane integration

The initial control-plane source iteration added real IPv6 DAD, SLAAC, router/neighbor discovery, MLD and
ICMPv6 on the selected Ethernet backend. The later source follow-up below adds TCP6 sockets and AAAA over IPv6.
IPv6 browser integration, DHCPv6, multiple active interfaces, a manual IPv6
configuration utility and revised Stage 3 as a whole remain incomplete.

The upstream lwIP 2.2.1 release is pinned to commit
`77dcd25a72509eb83f72b033d219b1d40cd8eb95`. Its vendored subset contains only the compiled source/header dependency closure
and is unmodified;
`docs/dependencies/lwip.json` records each file hash and `tools/verify_lwip.py`
checks exact inventory. BSD notices remain in each source and COPYING. CHOS's
port/configuration and adapter are separate. Builds require no dependency download.
Review upstream security advisories and run host/sanitizer/guest regressions before
updating this pin. This integration does not provide TLS or cryptographic entropy.

## Ownership and resource bounds

The original IPv4 DHCP/ARP/DNS/TCP stack retains its implementation and socket API.
One shared Ethernet receive owner dispatches EtherType 86DD to lwIP and IPv4/ARP
to the existing handlers, with the same eight-frame per-poll budget. IPv6 timers
run in the existing poll loop, never timer IRQ callbacks. An IRQ-save lock guards
the NO_SYS core; callbacks never yield or touch user pointers. Kernel memory is
fixed in the initial control-plane baseline: 32 KiB lwIP heap (64 KiB in
the TCP6 follow-up below), 24 512-byte pool buffers, eight neighbors/destinations,
four prefixes, three routers and four queued-neighbor records. No unbounded user
packet queues or new DMA ownership are introduced.

Intel 82574L and VMXNET3 enable multicast reception for ND/MLD while keeping
unicast promiscuous mode off. Permanent DMA storage, bus translation checks,
identity-DMA/MMU/cache restrictions, controller matching and timeout quarantine
remain. VirtIO stays supported. A successful IPv6 test does not certify another
PCI controller or a physical Pi/laptop.

The interface derives a link-local EUI-64 address from its real MAC, performs DAD
and accepts router-advertised autonomous prefixes through lwIP. Tentative and
duplicate addresses are distinguished from usable addresses. Link loss or the
existing administrative Disconnect invalidates all addresses, cached routes and
pending echo; reconnection restarts link-local DAD/SLAAC. IPv4 and IPv6 obey the
same administrative enable state. Independent IPv6 profile controls and privacy/
temporary addresses remain open.

The port's architectural-counter timing jitter is used only for protocol timing.
It is not an entropy provider and must never seed TLS, wireless keys or secure
tokens. Crypto/TLS remain unavailable until their actual prerequisites are met.

## User interfaces

Network Manager shows a real preferred SLAAC address or the absence of a usable
address. `net6status` reports actual addresses/states/router/echo status in the
graphical and recovery consoles. `ping6 IPv6-literal` starts one bounded echo;
only a matching, correctly checksummed reply with expected payload completes it.
Unanswered requests time out after five seconds; link loss cancels them.

EL0 syscall 37 returns a pointer-free 128-byte read-only status record, validated
for owning PID, writable output range and capacity. Syscall 38 copies a bounded
literal before requesting the same OS-owned echo operation. It cannot reconfigure
routes/adapters or inject arbitrary packets. A real EL0 policy test validates the
new status buffer and rejected requests alongside protected network profiles.
The separate EL0 Console also uses the window server's caret service.

## Verification

Host and ASan/UBSan tests compile the actual vendored core and exercise DAD/SLAAC,
valid/invalid RA hop limits/checksums, prefix/router handling, duplicate-address
refusal, echo checksum/payload/peer matching, timeout, administrative/link cleanup,
and malformed/truncated option/length traffic. The IPv4 host fixture isolates its
original stack; it is not offered as proof of IPv6 traffic.

`tests/qemu_ipv6.py` uses disposable direct/UEFI guests and actual Ethernet queues.
It requires on-wire RS/RA/NS/NA/echo request/reply packet types, a preferred address,
an IPv6 router echo, retained IPv4 DHCP/echo and fresh SLAAC after link reconnect.
Tests cover Intel, VMXNET3 and VirtIO separately. Results identify the actual run;
no public IPv6 website, Fusion IPv6 or Pi Wi-Fi pass is implied by this fixture.

The prior d91d135 ISO is the published typing build; this follow-up is source-only
and remains unchanged under the latest full-Stage-3 binary publication hold. Full modern engine, TLS, browser
runtime/isolation, Pi 5 wireless and remaining IPv6/application gates stay open.

## Owner-bound TCP6 and AAAA follow-up (source, local testing)

The new follow-up extends this control-plane baseline with a separate TCP6 client
and AAAA resolver. The original IPv4 engine and syscall numbers are unchanged.
It still does not provide a modern browser, TLS, DHCPv6, multiple simultaneously
active interfaces or a manual IPv6 properties page. Binary publication is held by
the latest user instruction until the full revised Stage 3 is satisfied.

The same pinned upstream closure now includes the original TCP sources. Separate
CHOS adapters expose four process-owned nonblocking TCP6 sockets, with 512-byte
copies, a 2048-byte receive ring per socket, a 2048-byte lwIP send bound, five-second
connect/close deadlines and bounded retransmission. The lwIP heap is now 64 KiB;
24 pool buffers remain. Raw callbacks run under the network lock and copy only
kernel-owned memory. The application supplies a 16-byte unicast peer address;
there is no raw packet or device/configuration syscall. Invalid, multicast and
IPv4-mapped destinations are refused. Hardware-backed entropy supplies the initial
sequence number before connect; absent entropy emits no SYN and leaves the socket
closed. It never falls back to the architectural counter. IPv4 behavior is retained.

Syscalls 39–45 provide open/connect/status/send/receive/graceful-close/abort. Status
is a pointer-free, versioned 88-byte record. All user ranges, capacities and owning
PIDs are checked before consuming receive data or copying output. EOF, reset,
network loss and timeout are distinguished from pending reads. Deferred process
cleanup aborts and releases every owned TCP6 socket. Completed FIN exchanges are
collected before lwIP can free their PCBs asynchronously, preventing stale kernel
pointers; pending close is bounded. There is no listener/server API in this profile.

Syscalls 46–47 provide owner-bound asynchronous AAAA lookup and a 320-byte status
record. The service learns one recursive DNS server from an actual Router
Advertisement RDNSS option; the source must be link-local, hop limit 255 and its
ICMPv6 checksum and entire option chain valid. Lifetimes, withdrawal and link loss
are honored, with a conservative 24-hour maximum lifetime. It never assumes the
QEMU router address is a DNS server. QEMU's default slirp RA used here has no RDNSS,
so DNS6 correctly reports no server in that profile until a real advertisement is
provided. Manual IPv6 DNS settings and DHCPv6 remain open.

Four owners may hold one query each, with 512-byte DNS datagrams, an actual-entropy
transaction ID and source port, three retries and a seven-second deadline. Source
address/port, UDP checksum, query ID/name/type/class and complete response bounds
are checked. An answer must belong to the expected owner or a maximum four-CNAME
chain, with minimum chain TTL. An unrelated AAAA record cannot complete a query.
Malformed/oversized/truncated responses are ignored until a bounded timeout; a
matching negative RCODE is reported. Link/admin loss, expired or changed DNS server
cancels outstanding work. Process exit removes the owner and UDP PCB. This is
ordinary unauthenticated DNS, not DNSSEC, a certificate decision or secure TLS.

Host tests exercise the actual lwIP TCP6/UDP6 paths and malformed inputs under
ASan/UBSan. `tests/qemu_tcp6.py` uses real EL0 syscalls and VirtIO queues to transfer
6144 patterned bytes from a host IPv6-loopback server, checks cross-process denial
and graceful FIN, then repeats after intentionally leaving all socket slots for
the reaper. `--missing-entropy` requires zero TCP6 SYNs. `tests/qemu_dns6.py` uses a
loopback-only socket-backed Ethernet peer for actual RA/RDNSS, ND and UDP6 DNS;
it injects wrong IDs/peers/owners/checksums/truncations carrying a distinguishable
untrusted address and verifies a subsequent correct AAAA answer, NXDOMAIN, actual
retransmission timeout and zero-lifetime RDNSS withdrawal cancellation. These fixtures
are distinguishable from host parser tests and from public DNS/website tests.
Actual run results, including any failures and reruns, are recorded separately.
Neither fixture establishes new Fusion/Pi/laptop runtime support.

The final local direct profiles `tcp6-final3`, `tcp6-final-missing2`, `dns6-final2`
and `dns6-final-missing2` all pass against the same kernel SHA-256
`e2e792fab18454c8573fb13638327ba118581d10505a36c6d9202fca0006a52f`.
Their evidence/snapshot/application hashes and retained earlier runner failure are
recorded in [the transport validation record](validation/stage3-ipv6-transport.json).
The DNS fixture's invalid packets carry `2001:db8:1::99`, while only the valid answer
carries `2001:db8:1::7`; accepting an invalid response would fail the actual EL0
assertion. Final no-RNG profiles emit zero TCP6 SYNs and zero DNS6 datagrams. These
are local test inputs; no binary build was published.

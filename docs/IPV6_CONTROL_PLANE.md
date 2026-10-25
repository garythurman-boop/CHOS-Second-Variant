# IPv6 control-plane integration

This source iteration adds real IPv6 DAD, SLAAC, router/neighbor discovery, MLD and
ICMPv6 on the selected Ethernet backend. It does **not** complete IPv6 browser
support, TCP6 sockets, DNS AAAA/resolution over IPv6, DHCPv6, multiple active
interfaces, a manual IPv6 configuration utility or revised Stage 3 as a whole.

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
fixed: 32 KiB lwIP heap, 24 512-byte pool buffers, eight neighbors/destinations,
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
under the alternate binary publication cadence. Full modern engine, TLS, browser
runtime/isolation, Pi 5 wireless and remaining IPv6/application gates stay open.

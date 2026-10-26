# Native secure transport prerequisite

`userland/tls` cross-compiles an actual MbedTLS 3.6.7 LTS client for the CHOS
native ELF ABI. It uses the existing process-owned IPv4 TCP API, allocation,
clock and entropy services. It runs at EL0, outside the kernel GUI execution path.
It is an experimental transport component, not a modern browser or an enabled
HTTPS feature in the recovery preview. No new application control advertises it.

## Protocol and security contract

The profile supports TLS1.2 and TLS1.3 with certificate-authenticated ephemeral
exchange, ECDSA/RSA certificates, AES128-GCM/SHA256 and AES256-GCM/SHA384, with
P256/P384. TLS1.3 PSK,0RTT and session resumption are not enabled.
There is no plaintext retry, insecure verification mode, PSK bypass or caller
callback that can override certificate validation. A nonempty ASCII hostname is
mandatory; MbedTLS checks the chain, hostname, dates and server purpose. SNI uses
that same hostname and ALPN advertises HTTP/1.1. No application bytes are sent
before a verified handshake. Normal full-size TLS records remain supported.

`chos_tls_create_ex` classifies argument, clock, memory, CA, entropy, library and
transport initialization failures. It parses a caller-selected DER trust anchor,
requires a usable UTC clock and monotonic deadline source, and seeds an upstream
HMAC-DRBG through actual CHOS RANDOM_BYTES before opening a socket. Temporary
entropy unavailability fails initialization with an explicit error. Callers may
retry that condition within a fixed deadline; the native fixture allows100 attempts
separated by one scheduler tick. There is no timing/random fallback.
The native VirtIO source or the finite firmware-seeded budget retains its existing
trust and exhaustion limits. Prediction resistance and production reseeding are
not established by this component.

UTC before 2025 or outside the runtime's supported calendar is refused. Existing
UEFI/PL031 clock availability is checked on public I/O operations; it is **not an
authenticated time service**. Test time comes from explicitly configured virtual
RTC/firmware. Production time trust/provisioning remains an open Stage3 gate.

The caller supplies the root for this experimental API. This is **not a system CA
trust-store policy or updater**. The tests generate isolated, ephemeral roots;
none is installed into CHOS or trusted for real sites. Protected root storage,
authenticated updates/revocation policy and browser integration must precede
production HTTPS claims. WPE's Soup/GnuTLS integration also remains unresolved;
this native adapter does not pretend that libSoup already uses MbedTLS.

## I/O, limits and lifetime

The single-threaded context exposes connect/handshake progression, read and write.
WANT_READ/WANT_WRITE return AGAIN; callers schedule a retry instead of spinning.
OS transfers are capped at512 bytes and TLS plaintext calls at16KiB. Partial writes
are supported; an AGAIN write must retry the identical pointer and length, as
required by MbedTLS. A15-second connect/handshake deadline and15-second idle I/O
deadline bound stalled peers. Crypto work remains timer-preemptible in the owning
process. No worker thread, broker process or browser sandbox is supplied here.

Every fatal failure aborts/releases its process-owned socket. Destruction frees
upstream state, erases the DRBG/context and releases its socket immediately;
it is also the cancellation operation, rather than an unbounded close wait.
Only authenticated TLS close_notify is successful EOF. Raw TCP EOF is rejected as
truncation even if preceding application bytes were received. The complete browser
must still enforce its own HTTP framing, redirects, paths and content policy.

Kernel four-socket and bounded native heap quotas remain. Tests exhaust all four,
reject the fifth, cancel every context and successfully establish a later verified
session. This is not an engine-sized memory or concurrent-tab capacity claim.

## Dependencies and verification

The exact upstream commit, Apache2 license choice and174-file original-byte
compiler dependency closure are in `docs/dependencies/mbedtls.json` and verified
by `tools/verify_mbedtls.py`. The boot entropy configuration stays separate.
No upstream crypto code is modified. `MBEDTLS_NO_UDBL_DIVISION` selects the
upstream portable division path; no homegrown128-bit crypto/compiler helper is
substituted. The private stdio declaration permits compiling upstream reporting
functions that are discarded by section GC; no formatter implementation is faked
or advertised, and the final native link has no unresolved symbols.

Host fixtures require Python cryptography (tested50.0.0, Python3.12.14) and OpenSSL
through Python ssl; those are host test dependencies, not target libraries.
`make test-tls test-tls-sanitize CC=clang LD=ld.lld` checks the actual X.509 parser
and verifier, including every certificate truncation, wrong host/root/date/signature
and malformed DER under ASan/UBSan. Use the regular aggregate suites too.

`tests/qemu_tls.py --mode direct|uefi --out FRESH` boots immutable kernel/ISO
snapshots with disposable CHFS disks and real emulated Ethernet queues. It records
actual TLS negotiations, SNI/ALPN, all four TLS1.2 ciphersuites and both configured
TLS1.3 ciphersuites, P384, intermediate chains and damaged issuer signatures, certificate
purpose/host/root/date failures, malformed records, fragmented authenticated data,
unauthenticated EOF, a stalled peer, cancellation/exhaustion and stable physical
pages after the loader's first-use retained file-buffer arena. The stalled-peer
fixture also operates desktop Diagnostics through actual emulated keyboard input.
`--missing-entropy` and `--missing-clock` require the correct initialization error
class and zero server connections. Test private keys remain in the external
per-run fixture directory and must never be published as product material.

Results and retained investigation failures are in the machine-readable report.
QEMU evidence does not qualify Fusion or physical hardware. Modern rendering,
JavaScript, full browser security/integration and live Pi5 wireless remain open;
Stage3 is incomplete, binaries stay held, and0.50.0 remains reserved.

## TLS1.3 PSA state and test method

TLS1.3 uses upstream PSA Crypto, HKDF and TLS1.3 implementation units from the
same immutable MbedTLS release. The external PSA RNG is bound only while a client
handshake/read/write call executes, to that client's already-seeded HMAC-DRBG.
It is cleared before every return, including WANT/error returns. A request outside
that scope returns insufficient entropy, a zero output count and erased output.
Actual guest tests check that property before creation and after every handshake
step. There is no hidden PSA seed, timer entropy, alternate RNG or exported key.

The client API remains single-threaded within each process. Separate EL0 processes
have separate state. Per-process PSA initialization is reference-counted across
client contexts, and the final destructor releases upstream PSA state after its
SSL/key/DRBG objects. Two overlapping handshakes and subsequent authenticated
requests prove that destroying one client does not erase the other's keys.
Four-context exhaustion/cancellation and later recreation remain tested. The
component currently owns that process's PSA lifetime; integration with other PSA
consumers or future threads requires a coordinated ownership/locking contract.

`tests/run_tls_cipher.py --suite TLS_AES_128_GCM_SHA256 -- --mode direct --out FRESH`
and the analogous AES256/UEFI invocation restrict only the local fixture's
OpenSSL TLS1.3 ciphersuite through a temporary configuration file. The runner
asserts the actual negotiated version, cipher, SNI, ALPN and request counts.
`tests/qemu_tls.py --tls-version 1.2 ...` verifies negotiation with TLS1.2-only
peers using the same dual-protocol native executable. Neither mechanism modifies
CHOS verification settings or installs a test root into the product.

See [TLS1.3 results](validation/stage3-native-tls13.json) for the current profile;
[initial TLS1.2 results](validation/stage3-native-tls.json) retain their earlier
snapshot and investigation failures. Production CA/time policy, modern engine
integration, live wireless and platform qualification remain
open; this protocol extension does not complete Stage3.

## IPv6 transport extension

`chos_tls_create6_ex` adds a16-byte network-order IPv6 peer on the existing
OS-selected interface. Named-host verification/SNI and all TLS policy are shared
with IPv4; no raw route, NIC or network-configuration access is added. The caller
supplies a resolved address; this does not wire browser AAAA/URL handling or add
manual IPv6 settings/DHCPv6. Scope-ID selection and IPv6 literal certificate names
are not provided by this DNS-hostname-oriented API.

The context retains its transport family for every status/send/read/abort operation.
IPv6 status validates the versioned OS ABI. A pending TCP6 entropy refill keeps
the same socket and fixed connect deadline, returning AGAIN without emitting a SYN
or resetting a driver. Real connection errors remain fatal. Zero/multicast peers
are rejected. IPv4 APIs keep their existing transport and verification path.

The IPv6 fixture uses the real guest network stack and host IPv6 loopback through
QEMU user networking. Captured Ethernet/IPv6/TCP packets must include actual SYN
and data. The dual-stack fixture also keeps IPv4 and IPv6 contexts live together,
completes their verified requests and independently frees each family, even when
their numerical kernel handles coincide. Missing clock/entropy runs permit no
TCP6 frames or peer connection. This is actual transport evidence, not public-site,
Fusion IPv6, browser or live wireless qualification. See
[IPv6 TLS results](validation/stage3-native-tls6.json).

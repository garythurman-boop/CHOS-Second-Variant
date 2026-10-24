# CHOS HTTPS / TLS integration boundary

## Current security state

CHOS 0.30.0 recognizes `https://` URLs, assigns port 443 by default, preserves HTTPS across relative and scheme-relative URL references, and parses HTTPS redirects. **It does not implement a TLS transport and cannot securely load HTTPS pages.** The browser intentionally refuses an HTTPS request before opening a TCP connection and never retries it as plain HTTP.

A URL parser is not a TLS implementation. Do not remove the fail-closed check until all of the conditions below are implemented and tested together.

## New groundwork in this snapshot

- A bounded nonblocking `chos_entropy_read()` kernel service, backed only by a negotiated modern VirtIO RNG device; no time/counter-derived fallback is used.
- `CHOS_SYS_RANDOM_BYTES` / `chos_random_bytes()` exposes at most 256 bytes per nonblocking request to the owning user process, returning no bytes when the source is not ready.
- QEMU run targets attach `virtio-rng-device`; the serial smoke test expects explicit successful RNG initialization.
- HTTPS URL parsing, canonicalization, relative URL resolution, redirect handling, and fail-closed policy are host tested.

The VirtIO RNG driver has passed host-mock unit and sanitizer tests. Its actual hardware interaction has **not** been run in QEMU or VMware in this environment. Do not treat the availability of this syscall as proof of entropy quality or as sufficient for cryptographic use until hardware behavior and lifecycle are reviewed.

## Requirements before enabling HTTPS

1. Vendor a reviewed TLS library compatible with CHOS's freestanding AArch64/userland ABI and license policy. Do not write new cryptographic primitives from scratch.
2. Integrate TLS with the existing TCP client/socket APIs without bypassing process ownership or memory bounds. Support partial writes/reads, handshake timeouts, interruption, close-notify behavior, and bounded memory use.
3. Require certificate-chain validation against a managed CA root bundle and hostname verification against the URL's DNS name. Handle DNS names and IP literals correctly.
4. Define how CHOS obtains a trustworthy clock, how roots are updated, and how clock-not-set or expired/not-yet-valid certificates fail safely. Never silently disable verification.
5. Feed the TLS library with a cryptographically suitable entropy adapter. It must fail the handshake if entropy is not ready or reads return short; do not use a deterministic fallback.
6. Handle TLS protocol/version/cipher policy, downgrade resistance, certificate parsing limits, SNI, ALPN where needed, and resource/time limits. Support only protocol versions the chosen library and policy can validate.
7. Add deterministic tests for handshake success/failure, wrong hostname, expired/untrusted certs, malformed records, truncation, connection reset, short I/O, redirect-to-HTTPS, and no plaintext downgrade. Use local test certificates and a loopback test server.
8. Add live QEMU tests before advertising HTTPS support.

## Release gate

Until these requirements have been met and tested, HTTPS must remain disabled in the GUI browser with a clear status message. An HTTPS URL that parses successfully must not be represented as secure content if no TLS session was established and the peer identity verified.

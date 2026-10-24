# CHOS 0.21.1-dev release notes

## Browser reliability and presentation

- Canonicalizes typed hostnames and HTTP URLs before storing navigation history or resolving page-relative links.
- Replaces a prior fetch with an explicit process-owned TCP abort so a manual navigation cannot wait indefinitely for the peer's graceful shutdown.
- Adds a browser-side hard deadline for close/redirect phases; deferred redirect navigation continues after the old socket is released.
- Adds a bounded TCP FIN-WAIT-2 timeout so a peer that ACKs the local FIN but never sends its own FIN cannot retain a socket forever.
- Adds a controlled TCP abort syscall that makes a best-effort RST and reclaims the process-owned socket.
- Adds conservative HTML body sniffing when the HTTP Content-Type header is missing or inaccurate.
- Improves text-preview presentation for headings, links, lists and keyboard link focus. Tab cycles through valid links and Enter opens the focused link; a clickable-line fallback avoids assuming proportional-font glyphs have fixed widths.
- Adds regressions for normalized URLs, body sniffing, TCP FIN-WAIT-2 timeout, socket abort and process ownership.

## UEFI ISO layout

- Changes the El Torito catalog to use a standard validation entry followed by an explicit EFI platform-specific section header and boot entry.
- Retains the zero sector-count convention for an EFI System Partition extending through end of media, and validates the EFI section, boot entry, FAT16 volume and AArch64 EFI PE/COFF target.
- The ISO remains experimental until it is boot-tested on real AArch64 UEFI firmware and VMware Fusion. Structural checks are not proof of a successful firmware boot.

## Limitations

CHOS Browser remains a lightweight HTML-to-text preview, not a modern rendering engine. Full CSS layout, JavaScript, image decoding/rendering, forms, downloads, TLS, HTTPS and X.509 verification are not implemented. HTTPS redirects are deliberately blocked.

## Validation goals

Clean Clang/LLD build with warnings as errors, browser and TCP regression tests, sanitizer tests, userland packaging checks, and UEFI ISO structural tests. Live QEMU browser-navigation and VMware Fusion UEFI boot checks are required to close runtime acceptance.

## Reproducible runtime browser fixture

Run `python3 tools/browser_test_server.py --bind 0.0.0.0 --port 8080` on the host machine. From a QEMU instance using standard user-mode networking, browse to `http://10.0.2.2:8080/`. The fixture includes links, a relative-link pair, all five supported redirect codes, a loop, an HTML body served as `text/plain`, a plain-text page, a delayed-close connection, and a delayed response.

# CHOS 0.20.1-dev release notes

## Summary

0.20.1-dev combines the 0.19.x bounded outbound TCP work with the first bundled CHOS Web Browser application. The browser is a deliberately small **HTTP text preview**, not a full modern browser. It uses the existing process-owned TCP API and DNS API, and it refuses HTTPS until a maintained TLS implementation and certificate validation are available.

## Browser features

- Start-menu entry **Web Browser** launches `/BIN/BROWSER.CHX`.
- URL entry accepts a hostname or `http://` URL, IPv4 literals, paths, query strings, and explicit ports.
- Asynchronous hostname resolution uses the CHOS DNS API; TCP requests use the process-owned EL0 socket interface.
- Sends bounded HTTP/1.0 GET requests and collects a bounded response.
- Parses HTTP status and headers, decodes complete chunked transfer bodies, and converts basic HTML into readable text. Script/style elements are stripped; common HTML entities are decoded.
- Includes Back/Forward history, Reload, bookmark save/view, page scrolling, and clear connection/HTTP errors.
- Bookmarks persist in `/DOCUMENTS/CHOS-BOOKMARKS.TXT`.
- The bookmark duplicate scan is length-bounded to avoid an out-of-bounds read when the bookmark file is almost full.

## Security and compatibility limits

- HTTPS is blocked with a visible message. An `https://` URL is never silently downgraded to plaintext HTTP.
- No TLS implementation, X.509 chain validation, hostname verification, root CA store, trustworthy wall clock, or cryptographic entropy service is integrated. Do not enter sensitive information into this development browser.
- This is a text preview, not a standards-compliant HTML engine. No JavaScript execution, CSS layout, image rendering, clickable links, forms, downloads, or automatic redirects are provided. HTTP sites that require redirects to HTTPS will not load through this preview.
- The TCP core remains an outbound-only development client with four socket slots, one outstanding segment at a time, bounded retries and limited reassembly/congestion behavior. It is not a general-purpose or security-reviewed TCP/IP stack.
- The UEFI ISO is structurally generated and tested, but live firmware boot, installer workflow, target-disk partitioning, and boot from an installed disk remain unverified/incomplete.

## Validation

- Clean AArch64 Clang/LLD build with `-Werror`.
- Browser-core tests: URL validation, fail-closed HTTPS handling, bounded HTTP headers, chunked body decoding, safe HTML-to-text extraction, malformed entity boundary case.
- Full C regression binaries pass, including GUI menu visuals and the browser integration's public TCP/DNS API compile probe.
- ASan/UBSan suite passes, including browser core, TCP/network stack, CHFS and GUI tests.
- Userland apps including `browser.chcx` pack and validate; its virtual image remains below the 64 KiB loader limit.
- UEFI ISO structural test and image generation pass. Firmware runtime boot was not performed in this environment.
- `tests/test_mkchfs.py` passes independently (7 tests). The aggregate `make test` command exceeded the environment's execution limit during that slow Python stage, so the aggregate command is not claimed as a completed pass.
- The user has already confirmed the lower networking stack live in QEMU, including DHCP, DNS, ICMP and a TCP HTTP GET returning `HTTP/1.1 200 OK`. The new graphical browser app itself still needs live QEMU testing.

## Build

```sh
make clean
make CC=clang LD=ld.lld
make test-browser-core
make test-userland CC=clang LD=ld.lld
make test-sanitize
make test-uefi-iso
make uefi-iso
make run-gui CC=clang LD=ld.lld
```

For the default desktop workflow, `make run-gui` builds and synchronizes bundled system apps while preserving an existing disk image. Back up `chos-disk.img` before testing migrations or experimental boot paths.

## Manual graphical browser smoke test

1. Back up `chos-disk.img` before experimenting with disk migration or UEFI boot. `make run-gui` builds and synchronizes the bundled `/BIN/BROWSER.CHX` without intentionally formatting an existing disk image.
2. Boot with `make run-gui CC=clang LD=ld.lld`, confirm the serial shell reports `DHCP: bound`, and open **Start → Web Browser**.
3. Enter `example.com` and press Enter. The app should resolve the host, connect over TCP port 80, and display the response as plain text. The browser app has not been live-GUI-verified by the build environment, so report any launch fault, timeout, or blank response with the serial output.
4. Test Back, Forward, Reload, scrolling, Star and Marks. Saved URLs are stored in `/DOCUMENTS/CHOS-BOOKMARKS.TXT`.
5. Enter `https://example.com`. It should display the deliberate “HTTPS blocked” message and must not attempt a plaintext connection to the HTTPS host.
6. An HTTP server that redirects to HTTPS will not load in this preview; automatic redirects are not yet implemented.

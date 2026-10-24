# CHOS 0.21.0-dev release notes

## Browser navigation and HTTP reliability

- Adds automatic bounded redirects for HTTP 301, 302, 303, 307 and 308 responses when a valid Location header is provided.
- Supports absolute HTTP targets, scheme-relative targets (resolved to HTTP only), root-relative targets, directory-relative paths, query references and dot-segment normalization.
- Limits redirects to five to protect against loops; redirects to HTTPS are blocked because TLS/certificate verification is not integrated.
- Parses Location and Content-Length headers with bounds. Conflicting duplicate Content-Length headers are rejected. When a non-chunked response declares a valid body length, the browser finishes after that many body bytes rather than requiring EOF.
- Adds clickable HTML anchor text, bounded to eight links per page, with URL validation and relative-target resolution. Lines containing links are shown in blue; clicking within the anchor text navigates.
- Improves safe HTML-to-text conversion with `#` heading markers, `*` list items, comment stripping, common entities, and `[Image: alt text]` placeholders. It never executes scripts or CSS.
- Browser and core compile with `-Oz` to keep the app under CHOS's 64 KiB virtual image limit.

## Limitations

This is still a lightweight text-preview browser, not a modern rendering engine. It does not provide JavaScript, full CSS layout, actual image decoding/rendering, form submission, downloads, TLS, HTTPS, or X.509 trust validation. Image alt text is shown when available. HTTPS and HTTPS redirects fail closed.

The UEFI ISO is experimental structure-validated boot groundwork, not a verified installer. Test live GUI/network behavior in QEMU after building from this source.

## Validation gates

The development build is intended to pass a clean Clang/LLD AArch64 build with `-Werror`, browser-core tests, userland package checks, the general host regression suite, ASan/UBSan, CHFS/CHCX image tools, and UEFI ISO structural validation. Runtime redirect/link behavior still needs live QEMU verification. The browser close path retries a graceful TCP close when the stack reports EAGAIN, preventing a pending segment from permanently blocking later navigation.

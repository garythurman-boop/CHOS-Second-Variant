# CHOS PAL digest backend

This implements the unchanged original WebCore PAL `CryptoDigest` interface with
the pinned original MbedTLS SHA-1, SHA-224, SHA-256, SHA-384 and SHA-512 code. It is
a selected engine component, not a TLS or WebCrypto implementation.

`CryptoDigestCHOS.cpp` belongs to the real PAL target and must compile using that
target's generated WTF/JSC/PAL/WebCore configuration. The standalone builder uses
the verified published WTF kit only to qualify this component; it does not supply
the full engine's ABI configuration.

The reusable CMake target `CHOS::PALDigest` (`chos_pal_digest`) builds
`DigestBackend.c` and the four original crypto/zeroization units. Add this
directory with `add_subdirectory`, link PAL privately to that target, and add
`CryptoDigestCHOS.cpp` to PAL's own sources. The target exports only this private C
interface's include directory. It exports no frozen WTF libraries or JSC flags.

The digest-only configuration gives every MbedTLS symbol a private
`chos_pal_mbedtls_*` name. SHA-1 is available for legacy/content digest APIs here;
the existing TLS configuration, certificate policy and TLS symbols are unchanged.
No random generator, TLS protocol, certificate parser or PSA subsystem is enabled
by this target.

Creation errors return null. Input and cryptographic-operation errors become
sticky; the C API returns failure with zeroed output and length, and PAL returns
an empty vector. Invalid finish-output arguments return an error without
poisoning an otherwise valid context.
No valid supported digest has zero length, including the digest of an empty
message. Repeated finalization returns the cached digest. Adding more bytes after
finalization invalidates it. Original MbedTLS free/zeroization routines erase
contexts before release. Individual contexts are not advertised as concurrently
mutable; workers use independent contexts.

The source inventory adds only original `library/sha1.c` to the existing MbedTLS
3.6.7 pin. Its exact commit Git blob and independently fetched official HTTPS
bytes match. The release tag is unsigned; no signed-release authentication is
claimed. The receipts remain in `authentication/`.

See [the native checkpoint](../../docs/NATIVE_WEBCORE_CRYPTO.md) for executed
tests, retained failures, reproduction and limits.

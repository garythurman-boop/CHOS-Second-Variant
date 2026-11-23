# Native WebCore certificate metadata

CHOS now supplies owned certificate values and DER inspection for the native
WebCore port. The C backend uses maintained MbedTLS 3.6.7 parser code; the C++
`CertificateInfo` adapter uses actual original WebCore summary types and the
full-port WTF archive. This is a page-platform prerequisite. It does not verify
certificate trust, fetch a page or establish a secure browser connection.

## Values and parser boundary

The value owns exact DER byte vectors. Isolated copies retain verification
metadata and deep-copy the chain, so destroying the original cannot invalidate
the copy. A default or empty chain reports unknown verification with a negative
error value. A nonempty chain may carry success/error flags and an optional
trust-anchor index reported by its caller. Descriptions explicitly say
"Caller reported"; parsing never manufactures successful verification.

Actual DER parsing supplies a leaf subject, UTC validity times and DNS/IPv4/IPv6
subject alternative names. The subject preference is CN, then organization,
then the first subject attribute. UTF-8 and valid BMPString values retain their
length, including embedded NUL; ASCII PrintableString/IA5String/T.61 values are
supported. Unsupported subject encodings produce no summary. Other SAN kinds
are omitted from the DNS/IP summary. These are metadata values; hostname
matching, current-time checks, path validation, trusted roots, display escaping
and the browser's security UI remain separate integration work.

The backend accepts one exact DER certificate of at most 256 KiB. Trailing
bytes, PEM, malformed/unsupported data and allocation failure return failure.
Original MbedTLS cleanup and zeroization release contexts. The weak-signature
predicate examines the signature digest of every supplied certificate unless a
successful caller report explicitly identifies a trust anchor. Even that anchor
must parse first. Malformed, unsupported or more-than-32-certificate chains are
conservatively treated as unsafe by the boolean predicate. This is not a chain
validator or an assertion that all non-SHA-1 algorithms are secure.

The private parser target renames its MbedTLS/PSA symbols and does not alter the
independently configured TLS library or certificate-signature policy. Enabling
SHA-1 here permits inspection of deliberately weak test certificates; it does not
authorize their use in TLS. Native WTF Vector allocation retains its existing
fatal-on-OOM behavior; only the separately tested C parser allocation interface
returns recoverable failure.

## Executed qualification

The [validation report](validation/stage3-native-certificate-info.json) records
exact source, original dependency, public vector, SDK, archive, object, executable
and result hashes. Build metadata distinguishes linkage from execution.

- Seven public synthetic DER cases are independently generated and inspected
  with host `cryptography`, with an actual OpenSSL SHA-1-signed negative fixture.
  They cover RSA/ECDSA, UTF-8, embedded NUL, subject fallback, DNS/IPv4/IPv6,
  SHA-1 inspection and explicit unsupported Ed25519 refusal. No private keys or
  production trust anchors are retained or installed.
- The C fixture checks 137 selected truncation/trailing/PEM/limit refusals,
  13 individually injected real allocation failures and three workers performing
  ten rounds. Its separately compiled host binary passes address,
  undefined-behavior and leak sanitizers.
- The native C++ fixture checks actual subject/UTC/SAN values, deep-copy ownership
  after destruction, default and reported verification metadata, empty-chain
  refusal to report success, known/unknown anchor handling, malformed-anchor
  refusal, chain bounds and three concurrent workers.
- Direct and isolated UEFI each execute three complete process lifetimes for
  both native fixtures. The C++ process performs three internal rounds and
  explicitly requires zero user allocations/mapped bytes after every round.
  Process reaping restores cold physical pages, engine ownership, committed
  frames and futex state. The C++ value fixture has native coverage; the host
  sanitizer result applies to the C parser, not the complete C++/WebCore engine.

The final SDK9 C parser executable is SHA256
`bdf8c02b2de9f4606cfd10e2a682438938f8122a81a8dc09026f5fd7a1d1b82a`;
the final C++ value executable is
`ef9584563cea7e1ff945de9365efee02fc2095af0023c077fb239b72cfd6f0ae`.
All four final guest runs use the same frozen kernel/ISO inputs, restoring
110,684 free pages in direct boot and 493,298 in UEFI. The qualification audit
also verifies 1,078 current source and dependency hashes, the original pinned
WebCore summary header, both exact executables and the retained TLS configuration.

An earlier UEFI value run correctly failed its harness assertion when it sampled
the process summary before the later exit-status row had finished arriving.
The native program had returned successfully. Both runners now wait for the
complete process-report end marker and complete memory line before checking
status and cleanup. That failure is retained alongside all four passing final
replays. Earlier fixture compile errors from using the wrong WTF String span
constructor are also retained; no replacement header or parser was introduced.

This is selected metadata qualification, not full X.509 conformance, fuzzing,
production trust policy or hostile-content browser security acceptance. New
Fusion, Pi 5 and laptop execution remains unrun. Stage 3 remains incomplete at
development version 0.30.0.

## Reproduce

Use the prepared toolchain and fresh output directories. The optional value
target needs an actual configured full WebCore build with completed `libWTF.a`,
generated headers, its real `CertificateInfoCHOS.cpp` compile record and the
original bmalloc `pas_process` object. It does not use JSCOnly replacement headers.

```sh
source /workspace/chos-tools/activate
export PATH=/workspace/chos-tools/build-python/cmake/data/bin:/workspace/chos-tools/build-python/bin:$PATH
python3 tools/verify_certificate_fixtures.py
python3 tools/build_native_certificate.py --host --sanitize --out /path/to/fresh/host
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /path/to/fresh/host/parser/native_certificate_parser
python3 tools/build_native_certificate.py --native-sdk /path/to/verified/sdk --webcore-build /path/to/full-webcore/build --out /path/to/fresh/native
python3 tests/native_certificate_parser/qemu.py --application /path/to/fresh/native/parser/native_certificate_parser --image-root /path/to/frozen/images --out /path/to/fresh/parser-direct
python3 tests/native_certificate_parser/qemu_value.py --application /path/to/fresh/native/native_certificate_value --image-root /path/to/frozen/images --out /path/to/fresh/value-direct
```

Repeat the last two commands with `--mode uefi` and new output directories.
The frozen image directory supplies `chos.elf`, `uefi-kernel.elf` and `chos.iso`.
The builder defaults to pinned public vectors, verifies their hashes and independent
metadata, and records all relevant source/configuration/dependency inputs before
and after compilation. `--generate-cases` creates additional disposable vectors;
`--cases` replays an explicitly selected existing set. Neither supplies trust roots.

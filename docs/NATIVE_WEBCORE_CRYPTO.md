# Native PAL digest checkpoint

The original WebCore PAL `CryptoDigest` API now links and runs on AArch64 CHOS
with actual MbedTLS SHA-1, SHA-224, SHA-256, SHA-384 and SHA-512. Original PAL
headers and the published native WTF implementation are used. This completes a
selected page-platform dependency; full WebCore rendering and the modern browser
remain incomplete. Development version remains 0.30.0.

The [adapter](../ports/webcore-crypto/README.md) keeps the original API and uses a
private C backend over maintained original algorithms. C-backend allocation/start
failures return null. The C++ wrapper/context and result Vector retain original
WTF fast-allocation fatal-on-OOM behavior; the selected failure sweeps do not
establish graceful recovery for those allocations. Update/cryptographic-finalization failures remain sticky and return
no digest, rather than accepting a partial hash. Invalid C finish-output arguments
fail without poisoning a valid context. Valid empty-message hashes are still the
full expected length. Repeat finalization, invalid algorithms, uninitialized
objects and attempts to append after completion are checked. Context cleanup uses
the original zeroization routines.

The separate digest-only configuration renames its MbedTLS symbols, so it can
coexist with the independently configured native TLS implementation. SHA-1 is
confined to this content/legacy digest target. The TLS configuration and
certificate-signature policy are unchanged. The source inventory adds only the
original `sha1.c` file at the existing 3.6.7 commit pin; its Git blob and official
HTTPS bytes match. The release tag is unsigned, and no signed-release claim is
made. Exact receipts and the original license are retained.

The [validation report](validation/stage3-native-webcore-crypto.json) records the
complete source, SDK, library, object, executable and result hashes. The selected
native executable is 291,040 bytes, SHA-256
`1cfdd52f7a6252497dcdb5eda034615e45c6065209f2855c713e0b8bce913235`.

- Eighty independent host-`hashlib` reference cases cover all five algorithms:
  empty/`abc` known answers, one million `a` bytes, binary messages and lengths
  around both hash block/padding boundaries. The reference is not linked into the
  native executable. Input arrives in 1-, 7- and 4,096-byte fragments; the million
  byte case uses 4,096-byte delivery.
- Both the C backend and the actual PAL C++ API compare complete digest bytes.
  Original WTF hexadecimal conversion, cached finalization, invalid arguments,
  allocator failure and injected original start/update/final failures pass.
- Three native pthread workers hash independent contexts through the actual PAL
  API. Each process performs three complete rounds and returns to zero native
  heap allocations/mapped bytes before `main` returns.
- Direct and isolated UEFI each pass three process lifetimes with exit zero,
  exact cold physical-page restoration (110,116 / 494,486), released engine and
  committed-frame ownership, and no queued futex waiters.
- A separately compiled host fixture runs the same C backend and original crypto
  units with GCC ASan, UBSan and LeakSanitizer. It does not execute the native-only
  C++/WTF wrapper. A second sanitized host consumer checks the reusable CMake
  target. Its native counterpart builds five real AArch64 archive members and
  links without unresolved symbols; that CMake consumer is build-only.

Earlier failures remain in evidence. Initial fixture compilation used outdated
WTF Vector/String call forms. The first native executable's hexadecimal oracle
expected lowercase even though original WTF emits uppercase; correcting the
fixture establishes the final pass, without modifying hash algorithms or WTF.
The interrupted native6 build was linked but not counted as executed.

The reusable `CHOS::PALDigest` CMake dependency contains only the private C
backend. Full PAL must compile `CryptoDigestCHOS.cpp` under its own consistent
generated configuration. The standalone component does not establish that full
ABI, page rendering, browser networking, WebCrypto policy, complete cryptographic
conformance, hostile-input fuzzing or production security. Physical Fusion,
Pi 5 and laptop execution remain separate acceptance gates.

Reproduce with fresh output directories and the prepared toolchain:

```sh
source /workspace/chos-tools/activate
python3 tools/build_native_webcore_crypto.py \
  --out /workspace/chos-evidence/UNIQUE-PAL-DIGEST \
  --native-sdk /workspace/chos-evidence/webcore-link-20261112/memory/sdk7 \
  --engine-kit /workspace/chos-rendering-inputs-a89db07 \
  --webcore-source /workspace/chos-reference/webkit-2.54.2
python3 tests/native_webcore_crypto/qemu.py \
  --out /workspace/chos-evidence/UNIQUE-PAL-DIGEST-DIRECT \
  --application /workspace/chos-evidence/UNIQUE-PAL-DIGEST/native_webcore_crypto \
  --image-root /workspace/chos-evidence/native-cairo-20261108/clean-cairo-images
```

Repeat the guest command with `--mode uefi` and a fresh result directory. For the
host backend, build with `--host --sanitize` instead of native SDK/source-kit
options, then run with `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1`. The builder refuses existing output directories,
verifies all original MbedTLS files, and checks native SDK, pinned PAL Git blobs
and the exact published WTF/bmalloc kit before and after compilation.

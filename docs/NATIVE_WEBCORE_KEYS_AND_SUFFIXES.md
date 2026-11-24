# WebCore key storage and public suffixes

The CHOS key backend owns parsed RSA and EC keys in Mbed TLS. It accepts public
SPKI and unencrypted version-zero PKCS8 envelopes. The fixture uses OpenSSL to
generate disposable RSA-2048, P-256, P-384 and P-521 keys. Python cryptography
independently decodes the exports and checks their mathematical components.
The build checks that the export oracle and input vectors belong to the same run.

Three direct and three UEFI QEMU processes each passed eight imports, four
private export comparisons, 54 refusals and 99 allocation-failure trials.
Threads also exported from shared keys. Each process returned its heap and the
guest's free-page count to baseline. The host sanitizer run exercised 25,718
allocation failures. The fault sweep records and replays real OS entropy within
a single test to keep allocation order stable across retries. Normal imports use
fresh entropy. The WebCore EC/RSA DER wrapper compiled in the page build.
JavaScript WebCrypto execution, signing, and key generation remain open.

The builtin public suffix table uses pinned ICANN and private rules. Both QEMU
boot modes and the host sanitizer fixture passed all 78 upstream registrable-domain
cases plus four ICANN/private and cookie-domain checks. The host and target
generated identical table bytes. Cookie storage and end-to-end browser policy
still need their own tests. [Source inventory](dependencies/native-publicsuffix.json)
and [validation results](validation/stage3-key-storage-public-suffix.json)

## Reproduction

Use an empty working directory outside the repository for the disposable
vectors, generated private-key headers and export files. With the working
directory in `run` and a prepared SDK in `sdk`, run from the repository root.

```sh
umask 077
mkdir -p "$run/exports"
python3 tests/native_webcore_crypto_keys/generate_cases.py --out "$run/vectors"
cmake -S tests/native_webcore_crypto_keys -B "$run/host" -G Ninja \
  -DKEY_CASES="$run/vectors" -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build "$run/host"
"$run/host/native_webcore_crypto_keys" "$run/exports"
python3 tests/native_webcore_crypto_keys/verify_exports.py \
  --vectors "$run/vectors" --exports "$run/exports" --out "$run/oracle"
cmake -S tests/native_webcore_crypto_keys -B "$run/host" -DKEY_EXPORT_ORACLE="$run/oracle"
cmake --build "$run/host"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$run/host/native_webcore_crypto_keys"
cmake -S tests/native_webcore_crypto_keys -B "$run/native" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/ports/native/chos-toolchain.cmake" \
  -DCHOS_SDK_ROOT="$sdk" -DCHOS_APPLICATION_PROFILE=engine \
  -DKEY_CASES="$run/vectors" -DKEY_EXPORT_ORACLE="$run/oracle"
cmake --build "$run/native"
cmake -S tests/native_publicsuffix -B "$run/suffix" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/ports/native/chos-toolchain.cmake" \
  -DCHOS_SDK_ROOT="$sdk" -DCHOS_APPLICATION_PROFILE=engine
cmake --build "$run/suffix"
```

Run `tests/qemu_webcore_component.py` for each mode, `direct` and `uefi`.
Supply the frozen kernel/ISO directory with `--image-root` and a fresh
`--out` for each run. Pair `--fixture crypto-keys` with
`--application "$run/native/native_webcore_crypto_keys"` and `--fixture public-suffix`
with `--application "$run/suffix/native_publicsuffix"`.

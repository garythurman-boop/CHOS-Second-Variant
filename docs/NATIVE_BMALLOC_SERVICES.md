# Native WebKit allocator services

Stage 3 remains incomplete. This component builds the actual pinned upstream
bmalloc library with `USE_SYSTEM_MALLOC=ON`, links it into CHOS applications and
executes allocation, entropy, diagnostics and mutex paths. It does not build or
execute WTF, JavaScriptCore, WebCore or the modern browser. libpas/mimalloc are not
enabled. The downloadable test ISO remains cc29ea8, version 0.30.0.

## Implemented services

The checked [upstream patch](../ports/webkit/patches/0001-chos-platform-bringup.patch)
adds explicit CHOS paths rather than claiming Linux or complete POSIX support.
CryptoRandom calls the existing OS RANDOM_BYTES service in at most 256-byte chunks,
handles partial progress and permits 32 transient EAGAIN/EINTR retries with one
scheduler tick between retries. Errors, exhausted retries, denied capabilities and
invalid output memory crash the calling application. There is no local random
algorithm, timestamp seed or weak fallback. This transports OS entropy; production
reseed policy and target-specific entropy qualification remain separate gates.

Native diagnostics format into a 512-byte buffer and handle partial Console writes
in at most 256-byte chunks. Truncation and formatting errors have explicit markers;
a denied/closed output terminates the write attempt. Mutex contention yields through
the actual scheduler service. Existing algorithms for other platforms are retained.

The original LLVM 19.1.7 dependency closure now contains 804 byte-verified files,
including `src/hash.cpp`, C++23 `expected` and `numbers`. The hash implementation is
compiled into the native library. The imported numbers header uses unsuffixed
double literals even for long double: the test explicitly detects widening and
lost quad precision. No binary128 constant accuracy or complete C++23 claim is made.

## Reproduction and evidence

Use the pinned complete JSCOnly source roots and the real native SDK:

```sh
source /workspace/chos-tools/activate
make native-sdk expected-hash-test CC=clang LD=ld.lld -j4
python3 tools/preflight_javascriptcore.py --source "$WEBKIT_SOURCE" --out "$CONFIG_OUT" \
  --native-sdk build/native-sdk --cmake cmake --ninja ninja --ruby ruby
cmake --build "$CONFIG_OUT/cmake" --target bmalloc -j4
python3 tools/build_bmalloc_vm.py --source "$WEBKIT_SOURCE" --out "$FIXTURE_OUT" \
  --native-sdk build/native-sdk --bmalloc-archive "$CONFIG_OUT/cmake/lib/libbmalloc.a"
python3 tools/test_bmalloc_services_host.py --source "$WEBKIT_SOURCE" --out "$HOST_OUT"
python3 tests/qemu_bmalloc_services.py --mode uefi --out "$GUEST_OUT" \
  --application "$FIXTURE_OUT/cmake/bmalloc_services"
python3 tests/qemu_expected_hash.py --mode direct --out "$EXPECTED_OUT"
```

The fixture builder verifies source/patch/SDK identities, converts the upstream
thin archive into a standalone regular archive, and checks every member is an
AArch64 relocatable object. The accepted library has 168 members. Its guest tests
exercise out-of-line malloc/realloc/free, zero/aligned/large allocation, overflow,
real entropy transport, bounded diagnostics, three contending native workers and
repeated complete process reaping. One upstream allocation-map bucket cache lives
until process exit: one 64 KiB heap mapping, reclaimed by process cleanup.

Negative guest tests deny output, deny entropy, pass read-only entropy memory and
boot without a hardware RNG or EFI seed. Failures remain confined to the application;
subsequent allocation/logging works, physical pages return and no futex waiters remain.
The expected/hash fixture checks an independent prime model, collision-heavy maps,
node extraction, expected monadic/value/error/void behavior and threaded lifecycles.

The actual ported sources also pass a host partial-transport model under ASan/UBSan;
that uses host formatting and modeled syscalls, not native hardware entropy evidence.
The [validation record](validation/stage3-bmalloc-services.json) records guest,
SDK, host, sanitizer and retained investigation evidence. Initial failures include
a fixture incorrectly rejecting standard `%n`, an overclaimed quad constant test,
missing original hash implementation and missing host Clang sanitizer libraries.
Tests/tool selection were corrected; production formatting and upstream bytes were
not weakened to hide these failures.

## Remaining integration

The actual engine build now stops in WTF: its platform threading primitives and
public `numeric` header are missing. Full thread lifecycle/stack discovery/GC
suspension, engine-sized memory, filesystem and OS services, the remaining upstream
dependencies, rendering and native desktop/network/TLS integration remain open.
The bounded 4 MiB image, 8 MiB owned frames and 2 MiB anonymous aperture are not an
engine-sized address space. QEMU results do not qualify Fusion, Pi 5 or laptops;
new target runs, real Wi-Fi and every revised acceptance gate are still required.

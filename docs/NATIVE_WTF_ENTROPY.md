# Native WTF entropy, process and page services

The pinned WebKit RandomDevice implementation now uses CHOS's actual random-byte
syscall. Each request is bounded to256bytes, partial positive results advance only
by the returned length, and an operation has at most32 transient EAGAIN/EINTR
retries with one scheduler tick between them. Denial, a missing source, zero or
oversized results, failed sleep and retry exhaustion terminate only the requesting
process. Empty requests perform no syscall. There is no synthesized or weak
fallback and no `/dev/urandom` dependency on CHOS.

The adapter is stateless so sharing an instance does not share a mutable buffer
or descriptor. The kernel continues to own entropy availability and source policy.
The native transport evidence does not establish source health, cryptographic
quality, reseeding or the production browser's security policy.

The original ProcessID header obtains a real positive native process ID and fails
closed when permission is denied. PageBlock queries the native sysconf page size
and checks positivity, power-of-two and engine ceiling constraints. CHOS avoids the
upstream generic unsynchronized lazy page-size cache. Other operating-system
branches retain their behavior.

## Verification

The actual upstream units build in both the JSCOnly WTF target and a native SDK
application. The application executes in direct and isolated UEFI QEMU guests,
with600-byte guarded requests and three concurrent workers. Three worker cycles
and three process lifecycles retain zero user-heap ownership, no queued futexes
and strict equality with cold PMM free pages. Reduced-capability RNG/PID children
and read-only output abort with exit134, without CPU user faults. A separate
no-RNG direct guest verifies bounded abort and a successful subsequent empty
request and process. A host model executes the same upstream source with partial
17-byte results,256-byte chunk boundaries, all32 allowed transient retries,
zero/oversized/denied results, sleep failure and invalid PID/page values, in normal
and ASan/UBSan builds. This models broker errors; it is not a real hardware test.

```sh
source /workspace/chos-tools/activate
python3 tools/build_wtf_entropy.py --source "$WEBKIT_SOURCE" \
  --native-sdk build/native-sdk --engine-build "$JSC_BUILD/cmake" --out "$APP_OUT"
python3 tools/test_wtf_entropy_host.py --source "$WEBKIT_SOURCE" --out "$HOST_OUT"
python3 tests/qemu_wtf_entropy.py --application "$APP_OUT/cmake/wtf_entropy" \
  --mode direct --out "$DIRECT_OUT"
python3 tests/qemu_wtf_entropy.py --application "$APP_OUT/cmake/wtf_entropy" \
  --mode uefi --out "$UEFI_OUT"
python3 tests/qemu_wtf_entropy.py --application "$APP_OUT/cmake/wtf_entropy" \
  --no-rng --out "$MISSING_OUT"
```

The [validation record](validation/stage3-wtf-entropy.json) retains hashes and
results, including the initial host-test compilation failure caused by a deprecated
volatile chained assignment. Splitting that test assignment corrected the warning;
the adapter and its checks were unchanged. The accepted native application was
rebuilt after the patch was frozen and has the same binary hash as the initial
build. Raw guest evidence and its verified archive remain outside build.

Full WTF/JSC and JavaScript execution, modern browser rendering, engine-sized
memory/stacks, foreign-thread GC suspension, production TLS/trust/time policy,
live Pi5 wireless and platform qualification remain open. This is prerequisite
source work, with no new ISO or version0.50.0 claim.

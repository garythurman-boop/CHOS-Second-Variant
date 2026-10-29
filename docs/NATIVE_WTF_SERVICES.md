# Native WTF synchronization and clocks

The real pinned WebKit WTF primitive headers now execute against CHOS's private
pthread subset. Original upstream CurrentTime, WallTime and MonotonicTime units
also run. This is a component of the engine port, not a complete WTF/JSC library,
JavaScript execution, rendering or Stage 3 completion. The downloadable test ISO
remains cc29ea8 and version 0.30.0.

## Native platform boundary

The [checked upstream patch](../ports/webkit/patches/0001-chos-platform-bringup.patch)
identifies WTF OS(CHOS) explicitly and selects the existing private pthread API.
CHOS remains outside OS(UNIX)/OS(LINUX). JSCOnly selects the checked
[mutex/condition backend](../ports/webkit/chos/ThreadingPrimitivesCHOS.cpp), rather
than the POSIX thread implementation's unsupported signals and semaphores. This
backend supplies mutex/condition operations; the rest of the Thread lifecycle,
stack discovery and GC suspension must still be implemented separately.

Unexpected mutex, condition, TLS-binding and clock errors terminate the calling
application in release builds. An ordinary busy try-lock and expired deadline have
their expected false return values. NaN, negative and zero deadlines return false
without releasing the held mutex. Positive infinity and deadlines beyond the
signed native seconds representation wait for a signal. Finite deadlines validate
the floating-point range before integer conversion and use the native absolute
CLOCK_REALTIME condition wait, including mutex reacquisition on timeout.

The actual upstream clock functions use checked native CLOCK_REALTIME and
CLOCK_MONOTONIC calls. Unavailable/denied clocks cannot silently become epoch zero.
The native monotonic path avoids the generic port's mutable wall-clock fallback.
Approximate/continuous variants use the real monotonic source in this bounded
profile; system suspend/resume accounting has not been implemented or qualified.
Clock availability is separate from authenticated wall-clock trust. Production
browser time/trust policy remains open.

## Dependency import and recovery

The SDK now includes 818 original LLVM 19.1.7 files, including 14 headers selected
for numeric algorithms by the real AArch64 C++23 preprocessor. Each imported byte
still matches the pinned official archive. No locale implementation or foreign-OS
header is invented to satisfy includes under inactive preprocessor branches.

The interrupted previous import had copied four files before finding an absent
foreign-platform locale dependency. Those exact original bytes were verified and
preserved outside the source tree before recovery. The importer now discovers and
checks its entire selection before writing any new source or manifest bytes.
The --native-header-closure mode exposes original private headers and explicitly
selected public headers only; --check-only makes no source changes. Four hermetic
regression tests cover late dependency failure, invalid paths, unregistered files,
archive identity, dry run, exact source import and idempotent source-set tracking.

The cloud restart discarded temporary build/source directories. Dependencies were
rebuilt with the original verified data package digest, and the build symlink now
points to /workspace/chos-stage3-build so compiled outputs survive subsequent
workspace restoration. This location is a local cache, not a distributed artifact.

## Reproduction and qualification

```sh
source /workspace/chos-tools/activate
make native-sdk CC=clang LD=ld.lld -j4
python3 tools/preflight_javascriptcore.py --source "$WEBKIT_SOURCE" --out "$ENGINE_OUT" \
  --native-sdk build/native-sdk --cmake cmake --ninja ninja --ruby ruby
python3 tools/build_wtf_primitives.py --source "$WEBKIT_SOURCE" --native-sdk build/native-sdk \
  --engine-build "$ENGINE_OUT/cmake" --out "$FIXTURE_OUT"
python3 tests/qemu_wtf_primitives.py --mode direct --out "$DIRECT_OUT" \
  --application "$FIXTURE_OUT/cmake/wtf_primitives"
python3 tests/qemu_wtf_primitives.py --mode uefi --out "$UEFI_OUT" \
  --application "$FIXTURE_OUT/cmake/wtf_primitives"
make test CC=clang LD=ld.lld -j1
make test-sanitize CC=clang LD=ld.lld -j1
```

The fixture uses the actual upstream configuration, headers and clock units plus
the production native synchronization backend. It checks 9,000 guarded increments
per process, condition predicates/broadcast, exact thread-local key destruction,
real deadlines and clock advancement across native workers. It exercises original
numeric scans/reductions/gcd/lcm/midpoint, including large floating values and signed
integer boundaries. Seven child cases deny futex/clock access or misuse ownership/
TLS; each exits134 while the parent and desktop remain usable. Three complete
process lifecycles return physical pages to the cold baseline, with zero private
futex waiters. This does not claim a WebKit Thread or GC implementation.

The [validation record](validation/stage3-wtf-services.json) records the direct/UEFI
component runs, original bmalloc and ICU/expected regressions, Ethernet/TLS checks,
fresh SDK positive/negative links, and full host/sanitizer suites. The initial host
suite was blocked by sandbox socket policy; that failed log is retained and the
permitted rerun passes. Host sanitizer coverage and real guest execution remain
distinct evidence, and neither establishes new Fusion/Pi/laptop qualification.

## Remaining engine gates

Actual complete WTF compilation next encounters sys/ucontext.h through its register
context declarations. CHOS has no Unix signal/machine-context implementation. Native
stack/register discovery and safe owned-thread suspension, the rest of the engine's
OS/filesystem/runtime services and engine-sized memory must be supplied. Rendering,
browser features and standards/security/site tests, live Pi 5 SDIO/firmware/radio/
authentication, remaining IPv6/interface management and independent target tests
still gate Stage 3 and version 0.50.0.

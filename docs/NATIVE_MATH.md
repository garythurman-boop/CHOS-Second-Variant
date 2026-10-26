# Native binary32/binary64 math and FP environments

CHOS now builds a static math library from pinned, unmodified musl1.2.5 sources.
The selected170 modules implement float/double operations and actual ARM64 fenv
register access. This supplies a concrete libc++/engine prerequisite; it is not a
complete C++ library, modern engine or browser conformance result. Development
remains0.30.0 and the Stage3 binary hold remains active.

## Dependency and build profile

`docs/dependencies/musl-math.json` pins the immutable public-mirror revision,
selected native/host modules and all235 original SHA-256 values. The complete
COPYRIGHT and per-file notices are retained. `tools/verify_musl_math.py` checks the
closed file set and refuses the generic dummy fenv backend. Native architecture
implementations replace matching generic modules; the library never links both.
The official release website was unavailable through the proxy; the tagged Git
mirror supplied the recorded revision over verified HTTPS, without a verification
bypass or claim of independently verified release signatures.

`make CC=clang LD=ld.lld -j4 libm` produces the archive and original public headers
under build/userland/libm. Original sed/type inputs generate bits/alltypes.h.
Link the archive after callers, using the tested native app linker for separate
RX/R/RW segments. The existing2MiB image/four-MiB frame quotas remain. No Linux
syscall layer or privileged device access is added.

Math user objects enable base FP and fenv-sensitive compilation. Kernel C and
the native integer C runtime retain -mgeneral-regs-only. Pinned upstream warnings
for intentional expression precedence, volatile force-evaluation temporaries and
unused nan payload parameters are suppressed only in this library rule. The
original source is not edited to appease those diagnostics. The archive is built
afresh when its member list changes, avoiding stale removed backends.

The supported profile is IEEE binary32/binary64. Public upstream declarations
also describe binary128 long double and nexttoward, but those implementations are
not linked or qualified; attempts to use them fail at link time. Software binary128
rounding/exception semantics need further work. This is an explicit open runtime
gate, not a nearest-only fallback presented as complete support. The current math
header declares math_errhandling through FP exceptions, not errno. Legacy signgam
is process-global; concurrent callers needing gamma sign should use the _r APIs.

## Thread environments

Native pthread_create now snapshots its creator's FPCR/FPSR and installs them in
the worker before calling user code. A small GPR-only assembly shim reads/writes
those system registers; C runtime code retains its existing compilation contract.
The parent and worker then have independently saved rounding/status state across
IRQ, SVC, sleep and yield. No Q-register data is copied between them. The kernel's
raw worker entry still clears all vector/control state, preserving fresh-process
isolation; the C library deliberately applies only its own creator's environment.

## Verification

Committed scalar fixtures use mpmath1.3.0 at400 decimal digits. The generator can
reproduce/check them offline when that development dependency is available; normal
builds use the committed fixture and do not need it. There are889 cases covering
28 functions in both precisions, including large trig arguments and extreme
magnitudes. Square root requires exact rounding for these cases; other finite
results permit at most4 ULP. Explicit tests cover signed zero, NaN/infinity,
subnormals, fused operations and domain/overflow flags. This is a regression
sample, not an exhaustive C/ECMAScript conformance suite.

Host tests compile the original generic math sources with real host fenv. Object
symbols are prefixed after compilation to preserve original weak aliases and
prevent accidental interposition on host libc/sanitizer functions. The same
fixtures and special-value checks pass under ASan/UBSan and leak detection.
Those tests do not establish ARM64 assembly or Fusion support.

`tests/qemu_libm.py --mode direct|uefi --out FRESH` runs the actual isolated ARM64
archive and compiler-generated FP calls. Three pthreads inherit distinct rounding
modes/status flags and preserve them across scheduling. Hold/update environment,
invalid rounding, repeated groups and stable warm physical pages are checked.
Legacy full-Q-register/fresh-process isolation, pthread and verified IPv6 TLS
regressions pass. Exact profiles and hashes are in validation/stage3-native-math.json.
New Fusion/Pi/laptop qualification is NOT RUN. Engine-sized memory, complete C++/
POSIX/loader, actual engine/browser, production trust/time/reseed and wireless
requirements remain open; no binary or0.50.0 release is published.

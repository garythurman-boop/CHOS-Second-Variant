# Native binary128 compiler helpers

The native SDK now supplies `libchos-quad.a`: original LLVM compiler-rt arithmetic,
comparison, signed/unsigned32/64/128 conversions and float/double extension/
narrowing for ARM64's IEEE binary128 long double. It uses the real AArch64 FPCR/
FPSR backend, not the generic nearest/no-op fallback. Twenty-three sources and
their internal headers are pinned in dependencies/compiler-rt-quad.json;
verify_compiler_rt.py verifies the union with the existing compiler-TLS closure.
Archives are extracted on demand; these helpers are not mandatory objects in
legacy application images. The native CMake toolchain links the archive in its
real runtime group. No host/libgcc/quadmath library is substituted.

## Upstream carry defect found by the new tests

The exact-rational test oracle exposed a lost carry in LLVM19.1.7's binary128
wideMultiply. Maximum finite negative long double times maximum subnormal rounded
to c000fffffffffffffffffffffffffffc instead of c000fffffffffffffffffffffffffffd.
The failing direct guest logs and images are retained. The original, unmodified
LLVM20.1.8 fp_lib.h contains the carry fix. Its release tag was resolved with
`git ls-remote` and its bytes checked against immutable commit
87f0227cb60147a26a1eeb4fb06e3b505e9c7261. The dependency manifest explicitly records
that single-file version override. Its other change removes an unused binary32
fmax helper. All remaining imported compiler sources keep their pinned19.1.7
bytes; no local arithmetic algorithm or altered expected result masks the defect.

## Evidence and limits

`tools/generate_quad_vectors.py` computes expected IEEE representations directly
from exact Python integer ratios, rounding nearest/ties-even. It checks its own
binary64 packing against3,000 host conversions. Reproducible fixtures cover192
binary128 pairs, each with add/sub/multiply/divide and float/double narrowing,
plus integer conversion and128 extension fixtures. Boundary, normal/subnormal,
finite overflow/underflow and cancellation cases are retained.

`tests/qemu_quad.py` executes real native programs three times per profile and
checks child exit, no unresolved imports/PT_TLS/W+X mappings, stable warm physical
pages and no queued futex waiters. Direct mode uses the Makefile-linked program;
UEFI uses the ordinary CMake/SDK-linked program. Tests also check NaN/infinity/
signed-zero behavior and three worker lifecycles with inherited FPCR/FPSR,
independent directed addition rounding and inexact flags across scheduling.
Full host/ASan/UBSan suites and native compiler-TLS UEFI regression pass.
See validation/stage3-native-binary128.json.

This is a compiler-helper subset. Upstream multiply/divide, narrowing and integer-
to-float conversions use nearest-even rather than complete dynamic-FENV behavior;
not every IEEE exception is raised by software helpers. Addition/subtraction read
the actual ARM64 rounding mode and addition publishes inexact to FPSR. The tests
do not establish exhaustive IEEE/C/ECMAScript conformance. Full long-double libm,
formatted streams, floating parsing and engine integration remain open. The
existing binary32/binary64 hardware math profile is preserved. New Fusion/Pi
execution is NOT RUN; Stage3/version0.50.0 and binary publication stay held.

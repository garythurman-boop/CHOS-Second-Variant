# Original WTF Int128 and native integer runtime

The maintained CHOS WebKit patch now builds the original upstream `Int128.cpp`
in the actual JSCOnly WTF target. The native no-localization C++ profile does not
provide an iostream runtime, so only the optional `std::ostream` diagnostics and
their declarations/includes are excluded on CHOS. Other platforms retain them.
Integer arithmetic, floating constructors and WTF `PrintStream` method bodies
remain original and present. This is an explicit native profile restriction, not
a replacement arithmetic implementation or a complete hosted C++ claim.

The first candidate guard also excluded an absolute-value helper used by signed
division. Compilation caught that mistake; the guard now ends before the helper.
Its failed compile is retained. The actual full WTF build progresses past Int128
and currently reports five filesystem compilation failures: `FileHandle.cpp`,
`FileSystem.cpp`, `posix/FileHandlePOSIX.cpp`, `posix/FileSystemPOSIX.cpp` and
`unix/UniStdExtrasUnix.cpp`. These are compile-blocker counts for this library,
not a count of remaining Stage 3 requirements. Complete WTF linking, engine Thread/
GC services, engine-sized memory, JavaScript execution and browser rendering
remain open, alongside native Wi-Fi and the remaining security/platform gates.

Native linking exposed real missing compiler services: 128-bit signed/unsigned
division/modulo, float/double to128-bit conversions and `ldexpl`. Thirteen selected
original LLVM compiler-rt 19.1.7 translation units now supply integer division and
binary32/binary64 conversions in both directions. They share the existing
`libchos-quad.a` archive/link identity. Their original source bytes and immutable
commit/HTTPS provenance are recorded in
[compiler-rt-integer.json](dependencies/compiler-rt-integer.json). Existing shared
headers, including the documented original LLVM 20 binary128 carry correction,
remain selected. The original musl 1.2.5 `ldexpl` wrapper delegates to the already
selected native binary128 `scalbnl`; its bytes are added to the musl math manifest.
The exact closure verifier now covers 62 LLVM files and 281 musl files. No upstream
product algorithm was rewritten to satisfy the fixture.

The independent host test compiles only DUT units with renamed runtime symbols.
Its oracle translation unit retains the host compiler/libgcc symbols, preventing
an accidental comparison of a routine with itself. Normal and ASan/UBSan runs
cover 200,000 seeded full-width and 64-bit-divisor cases, signed minimum/maximum
boundaries, remainders, integer/FP rounding, fractional truncation and exponent
boundaries. It uses nearest-even for integer-to-floating comparisons. Division
by zero, signed minimum divided by minus one, and out-of-range/NaN floating-to-
integer conversions are excluded from reference expressions with undefined C/C++
semantics. This is selected runtime coverage, not complete compiler-rt or dynamic
floating-environment conformance.

The native fixture compiles and links the actual original `Int128.cpp`, using the
checked source pin/patch, actual JSCOnly configuration and staged native SDK. It
compares the upstream fallback classes with compiler-provided128-bit references
for arithmetic, division/modulo, bitwise operations, ordering and shifts. Signed
reference arithmetic stays representable; shift references explicitly handle sign
extension. It checks float/double/binary128 constructor fractions and boundaries,
and actual native integer-to-FP helper calls at powers, rounding ties and extrema.
Three workers execute 37,000 total seeded cases per process over three worker
cycles. Three complete process lifecycles must restore the cold PMM baseline,
with no user heap or queued futexes. WTF `PrintStream` bodies are compiled but
not executed by this arithmetic-only fixture; unused dependencies are discarded
by the native linker's function-section collection.

The first native fraction fixture incorrectly expected a truncated integer to
convert back to its original fractional input. Its integer-result comparison
already passed; the round-trip oracle now compares against the truncated value.
That failed run and exact source are retained. No runtime semantics or guest
thresholds were weakened to accommodate it. A separate earlier failed link records
the actual missing imports before the original runtime routines were integrated.

Reproduce the component checks after preparing the environment:

```sh
source /workspace/chos-tools/activate
make native-sdk test-integer-runtime test-integer-runtime-sanitize CC=clang LD=ld.lld -j4
python3 tools/build_wtf_int128.py --source "$WEBKIT_SOURCE" --native-sdk build/native-sdk --engine-build "$ENGINE_BUILD" --out "$APP_OUT"
python3 tests/qemu_wtf_int128.py --mode direct --application "$APP_OUT/cmake/wtf_int128" --out "$DIRECT_OUT"
python3 tests/qemu_wtf_int128.py --mode uefi --application "$APP_OUT/cmake/wtf_int128" --out "$UEFI_OUT"
```

The validation record provides exact source, patch, SDK, executable and raw
regression hashes. QEMU arithmetic evidence does not establish new Fusion,
Pi 5 or laptop qualification. Stage 3 remains incomplete and version remains 0.30.0.

Exact qualification is recorded in
[the validation manifest](validation/stage3-wtf-int128.json), including the failed
full-library build and earlier corrected fixture/candidate failures.

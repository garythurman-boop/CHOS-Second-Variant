# Native numeric conversion and binary search

The native SDK includes the original libc++19.1.7 charconv implementation and its
Ryu float/double conversion units. Integral from_chars/to_chars support the tested
signed/unsigned8/16/32/64-bit cases and all bases2..36, including extrema, overflow
and preservation of the destination on invalid/out-of-range input. Float/double
to_chars provide shortest, fixed, scientific and general formatting with bounded
output. Original cfloat and its required float.h wrapper expose the real AArch64
binary32/binary64/binary128 constants. No C++ conversion algorithm is substituted.

The upstream19 release does not provide floating from_chars. Its long-double
to_chars implementations explicitly cast to double; native binary128 precision
and range are therefore not qualified by these overloads. These are open profile
limits, not full C++23 conformance. Existing binary128 snprintf conversion remains
separately qualified. JavaScriptCore uses additional engine-owned floating parsing
code, whose actual source compilation now progresses beyond the missing headers.

The native C library also provides original musl1.2.5 memmem, with bounded binary
search, short-needle cases and its two-way algorithm. The real WTF span find helper
executes its HAVE_MEMMEM branch against that implementation. This closes a concrete
DataLog/StdLibExtras dependency without advertising a full GNU/POSIX libc.

## Provenance and import behavior

Every imported byte is checked against the pinned release archive or musl commit.
libc++ now has841 original files and the combined selected musl closure276. The
four actual C++ implementation units and their quoted Ryu headers/tables are
recorded in the dependency manifest. Original LLVM/Microsoft/Boost notices and
Ryu provenance remain in source, alongside the existing project licenses.

The native source-closure importer preprocesses real selected AArch64 C++ units,
following only active public/private and quoted source dependencies. Inactive
foreign-OS, locale branches and commented examples no longer create invented
requirements. All discovery precedes mutation; the existing rollback and atomic
manifest replacement remain. Failed scans expose their actual compiler stderr.
Six hermetic tests cover missing dependencies/identity/path failures, dry runs,
quoted source closure and idempotence. These modeled importer tests are separate
from the real compiler scan and native execution evidence.

The original memmem pointer-distance sign warning is disabled only for that
unchanged source. Its build product stays in the native runtime object directory;
the SDK installer's ownership whitelist remains intact. An initial misplaced
product was correctly refused before replacing the old SDK. The final SDK has
1,171 verified files and real positive numeric/search link probes; existing
negative import/library and full-data image-budget checks remain enabled.

## Qualification

Independent exact Python Decimal arithmetic generates540 binary32/binary64
fixed/scientific/general reference strings. Native guest tests check every vector
at zero, short, exact, extra and512-byte capacities, with guard bytes and unchanged
errno. Thousands of integral cases use a separate repeated-division digit oracle.
Random finite shortest conversions round-trip through the independent maintained
musl parser. Four rounding modes check nearest-even output independent of the
caller's mode. Concurrent workers have private errno and rounding state, then
return heap ownership to zero. Repeated process lifecycles retain strict equality
with cold PMM free pages and leave no futex waiters.

Host normal and ASan/UBSan tests compare30,000 binary searches with both an
independent reference and glibc. Protected-page boundary cases exercise the last
readable byte of each input and all short-needle paths. A periodic1MiB adversarial
haystack checks the retained two-way search. Native guests separately compare the
original primitive and actual WTF span helper against an independent reference.

```sh
source /workspace/chos-tools/activate
make native-sdk CC=clang LD=ld.lld -j4
python3 tools/build_wtf_charconv.py --source "$WEBKIT_SOURCE" \
 --native-sdk build/native-sdk --engine-build "$JSC_BUILD/cmake" --out "$APP_OUT"
python3 tests/qemu_wtf_charconv.py --mode direct \
 --application "$APP_OUT/cmake/wtf_charconv" --out "$DIRECT_OUT"
python3 tests/qemu_wtf_charconv.py --mode uefi \
 --application "$APP_OUT/cmake/wtf_charconv" --out "$UEFI_OUT"
python3 tests/generate_charconv_vectors.py --check
make test CC=clang LD=ld.lld -j1
make test-sanitize CC=clang LD=ld.lld -j1
```

See [the evidence and retained investigations](validation/stage3-charconv.json).
The full upstream WTF attempt still fails: calendar conversion, actual native
filesystem/descriptor ownership, sleep/immediate-exit and error-string helpers,
strings.h, CPU/memory reporting and optional ostream diagnostics remain concrete
build dependencies. Completing that library is still separate from executing JSC,
engine-sized memory/stacks, foreign-thread GC coordination, a modern browser,
production browser security, live Pi5 wireless and independent platform acceptance.
Stage3 is not complete, version stays0.30.0 and no new ISO is published.

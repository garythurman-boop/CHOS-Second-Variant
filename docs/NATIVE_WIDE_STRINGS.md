# Native wide strings and UTF8 conversion

The static native SDK now supplies original musl1.2.5 wide memory/string algorithms,
fixed UTF8 conversion and invariant C-locale wide classification/collation. Actual
std::wstring storage, growth, copy/move and string views use these functions.
Twenty-one original C files and three original libc++ wrappers are pinned with
source hashes; all new musl bytes matched the immutable upstream Git mirror.
The compiler-selected native ABI is retained: ARM64 bare-target wchar_t is unsigned
32-bit and wint_t is signed32-bit. Host tests use the host's own wchar ABI, while
target tests independently exercise the actual ARM64 types.

libchos-wide.a is extracted on demand in the SDK's real runtime group. It supplies
wide memory/search/copy/tokenization, mbrtowc/mbrlen/wcrtomb, bulk mbsrtowcs/
wcsrtombs, nonrestartable standard wrappers, btowc/wctob and C-locale wide classes.
UTF8 converts actual Unicode scalar values; malformed input, surrogates and
out-of-range values return EILSEQ. Bulk conversion respects byte/element limits,
does not emit partial encoded characters, updates source pointers, and supports
count-only queries. Counting preserves source and conversion state.

The private runtime TCB now has nine conversion-state slots: the original four
C16/C32 slots and five independent wide decoder/encoder/mbrlen/bulk slots. Each
worker owns its state; the kernel continues to preserve the TCB pointer without
interpreting its private layout. All affected C/C++ runtime objects are rebuilt.
There are no ELF TLS-segment or host-thread-library substitutions.

## Qualified scope

Wide-character support is enabled in the bounded libc++ profile and wchar_t's
basic_string is instantiated alongside char. Localization, streams, filesystem
and other existing unsupported features remain disabled. Wide numeric parsing,
wide time/formatted output and FILE I/O have standard declarations needed by the
C++ wrapper but no implementations. CMake real-link probes explicitly reject
swprintf and wcstod; they do not fabricate successful capability results. FILE is
only an opaque type and has no objects or success-returning I/O backend here.

Classification/case conversion and collation use the invariant C rules. They are
not Unicode UCD classification, locale switching or linguistic collation; ICU
still needs its real data/services for those. UTF8 encoding is a separate service.
No complete hosted C/C++ or modern-browser conformance is claimed.

## Verification

Host normal and ASan/UBSan tests compare20,000 generated wide string/memory cases
with glibc and exercise every Unicode scalar through encoding/decoding/chunking.
Bulk bounds, zero capacity, counting with pending state, malformed input in exact-
sized allocations, source updates, invalid wide scalars and C classification pass.
Native compiler/host signedness warnings were corrected or scoped to original
upstream pointer-distance comparisons; upstream algorithm bytes were not edited.

The actual native ELF exercises C wide functions, UTF8 boundaries, 2,000-character
wstring allocation and multi-byte bulk limits. Three std::thread workers hold
partial multibyte sequences across scheduling while using independent wide,
mbrlen, encoder, bulk and C16/C32 states. Each keeps a distinct errno. A parent's
pending wide and scalar sequences survive all three worker lifecycles. Both
direct mode and the CMake/SDK executable in isolated UEFI run three processes
with zero exit, no retained user heap, stable warm physical frames and no queued
futexes. No unresolved imports, PT_TLS or W+X LOAD segments are accepted.
Full host/sanitizer suites, the complete std::thread lifecycle/refusal UEFI fixture
and compiler-emulated-TLS direct regression pass. See
validation/stage3-native-wide.json.

The updated SDK generates actual ARM64 objects for200/202 original ICU79.1 common
units (3,911,520 bytes total), including all three formerly failing wchar units.
putil.cpp and umapfile.cpp still fail on file/time platform interfaces. This is
object-generation evidence, not a complete ICU archive/data/i18n link or runtime.
Engine-sized memory, complete platform services, actual engine/browser, production
security integration and Pi5 radio work remain open. New Fusion/Pi execution is
NOT RUN. Stage3/version0.50.0 and new binary publication remain held.

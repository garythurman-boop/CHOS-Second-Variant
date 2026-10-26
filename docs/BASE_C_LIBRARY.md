# Numeric and Unicode prerequisites for libc++

The native C library now supplies signed/unsigned strtol/strtoll/strtoul/strtoull,
atoi/atol/atoll, abs/div families, bsearch and in-place heapsort qsort. Numeric
conversion checks base2..36 or automatic octal/decimal/hex, consumes ASCII space
and sign, preserves precise end positions and saturates with ERANGE on overflow.
An incomplete0x prefix leaves x unconsumed. Prefix rules are C11/C17; the C23
binary-prefix extension is not supplied. Sorting uses constant extra storage and
bounded O(n log n) comparisons, without recursion or allocation. Arithmetic and
object/comparator validity retain their usual C preconditions.

`uchar.h` supplies real UTF-8/UTF-16/UTF-32 conversion state and mbrtoc32/c32rtomb/
mbrtoc16/c16rtomb/mbsinit. Conversions preserve incomplete input across calls,
return the bytes consumed by the current call, handle UTF-16 surrogate pairs and
the pending-low-unit -3 result, and reject overlong encodings, surrogate scalars,
invalid continuation bytes and values beyond U+10FFFF. Invalid input clears the
state and reports EILSEQ; output is not partially committed on an error. Separate
implicit states live in each native TCB, so both explicit and implicit conversions
are allocation-free. Encoding is fixed UTF-8; this is not a locale or wchar library,
text shaping, normalization, IDNA policy or browser rendering.

The first libc++ cross-compilation identified missing C div types and mbstate_t
alongside math, scheduling and other dependencies. The numeric types now have
matching real functions and mbstate_t has tested conversion semantics. No fake
success header or host C library is substituted into the native application.
Full libc++/C++/stdio/libm/runtime and engine integration remain open.

Host tests prefix exports to avoid replacing the host C library or sanitizer
interceptors. They compare20,000 numeric inputs with the real host conversions,
exercise integer limits and sorting/search, and round-trip every Unicode scalar
against host C.UTF-8 encoding. Every scalar also passes byte-at-a-time decoding
and UTF-16 conversion; malformed prefixes, null/zero-length operations and state
errors are tested under ASan/UBSan. Those are host tests, not platform evidence.

`tests/qemu_basec.py --mode direct|uefi --out FRESH` runs actual EL0 calls and
three pthreads with distinct multibyte sequences, partial implicit states and
errno across yield/sleep/preemption. Three worker lifecycles per process and
repeated launches must preserve stable warm pages and zero queued futex records.
The user heap returns to zero allocations after each join group. Aggregate host/
sanitizer, compiler TLS and verified IPv6 TLS regressions pass; exact profiles
are in validation/stage3-base-c.json.

The initial guest heap-zero check failed because Clang materialized an emulated
TLS address before selecting the caller's supplied state. Its executable and
disassembly are retained. Moving the four small implicit states into the existing
TCB removes that hidden allocation. Only subsequent corrected profiles qualify.
New Fusion/Pi/laptop behavior is NOT RUN. Stage3 remains incomplete, development
version remains0.30.0 and binary publication stays held.

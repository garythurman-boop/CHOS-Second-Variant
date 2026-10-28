# Native bounded string formatting

The SDK now implements snprintf and vsnprintf using original musl1.2.5
formatting code, its actual variadic parser and binary floating conversion.
Each invocation uses a private memory sink on its own stack. This does not
change the public read-stream FILE layout or provide writable FILE streams,
stdout/stderr objects, printf/fprintf or wide formatted I/O.

Five original files and their pinned commit/SHA256 are recorded in
[the dependency manifest](dependencies/musl-format.json). Original sources are
unchanged. The build generates a checked vfprintf copy that rejects INT_MIN
field width before signed negation and rejects invalid positional slot0.
The first edge case produced a real UBSan failure against the original source;
the native copy now returns EOVERFLOW. Invalid positional slot0 returns EINVAL.
The private adapter writes actual bytes to the bounded memory buffer, reports
the full logical length, terminates when space permits, and supports count-only
NULL/zero-capacity calls. It requires no allocations or shared stream locks.
The original C error-message table backs the internal GNU %m extension in the
runtime's invariant C locale. There is no locale translation service or public
strerror_l implementation implied by that internal path.

Host normal and ASan/UBSan tests compare 4,000 integer and 1,500 floating inputs
against glibc, each at ten capacities with surrounding guard bytes. They also
cover precision/width, all standard integer lengths, positional arguments,
wide UTF8, %n, errors and four rounding modes. C permits different hexadecimal
normalization for subnormals: those cases compare the represented value rather
than imposing glibc's exponent spelling. An early failure diagnostic itself
read an unterminated zero-capacity test buffer; it now renders bounded diagnostic
copies. That sanitizer failure is retained separately from formatter failures.

Native direct and isolated UEFI/SDK tests use 120 independent binary64/binary128
output fixtures computed from exact values with Python Decimal at 20,000-digit
precision. Six capacities check counts, NUL termination and untouched bytes.
The corpus explicitly applies C's zero/exponent conventions rather than Decimal's
stored exponent for zero. Tests cover mixed/copied va_list cursors, positional
arguments, wide characters, overflow and invalid input. Three native workers use
different errno/FP-rounding states across three lifecycles. Real process runs
exit0, release heap allocations, retain stable warm physical frames and leave
no private futex waiters. See [evidence](validation/stage3-native-format.json).

The implementation retains the native runtime's bounded C/UTF8 profile and
binary128 helper limitations documented in NATIVE_BINARY128.md. It is not a full
hosted C/POSIX environment or a production browser. The existing cc29ea8 test ISO
remains the published Fusion milestone; new native formatting results are QEMU
evidence until independently tested on Fusion/Pi/laptop targets.

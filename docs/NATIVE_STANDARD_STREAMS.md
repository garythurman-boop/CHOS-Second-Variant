# Native console standard streams

CHOS provides process-local stdout/stderr objects and real unbuffered console
output through printf/vprintf/fprintf/vfprintf, fwrite, fputc/putc/putchar,
fputs and puts. The existing maintained musl formatter writes through a checked
callback bridge. Public FILE objects retain their native layout; a private
formatter context and buffering live on each call's stack. No public/private
FILE layout alias is used and no replacement formatting algorithm is introduced.

Each stream operation holds its yielding native stream lock until completion.
Partial writes advance by the accepted length and each kernel request is at most
256bytes. Zero, oversized, malformed negative and failed results stop the operation,
set errno/ferror and return the documented failure or complete-element count.
There is no retry loop without progress. Existing error indicators remain sticky
until clearerr; a later successful write does not erase an earlier error.

Both standard streams route through the calling process's existing WRITE
capability and console ownership. The native syscall has one console destination;
fileno returns the conventional1/2 identities, without implying independent pipe,
redirect or descriptor-table ownership. fclose logically closes that process's
standard stream and does not disable the other one or another process's console.
The reader registry and its file budget remain separate from these two objects.

Ordinary FILE snapshots retain read-only modes and reject output. Console streams
reject reads and seeking. fflush handles synchronous console output without
inventing a backing file flush; fflush(NULL) has no buffered output to drain.
setvbuf accepts unbuffered console mode; line/full output buffering and changing
read buffering return ENOTSUP. Writable ordinary files, stdin, POSIX descriptor
redirection, complete stdio and C++ iostream/locale remain separate work.

The native public variadic entry points now compile with the AArch64 floating
argument ABI enabled, matching the maintained formatter and their callers. This
matters for mixed integer/double/binary128 formatting; a general-registers-only
variadic wrapper would interpret floating arguments incorrectly.

## Evidence and reproduction

Host normal and ASan/UBSan tests cover guarded output, chunking/partial-element
counts, error recovery, invalid formats and INT_MIN widths, invalid pointers,
unsupported buffering/directions, stream closure and300 complete records from
three concurrent writers with one-byte transport progress. Existing read-stream
40,000-operation model and formatting/rounding vectors remain intact.

The native SDK fixture builds actual upstream FilePrintStream, PrintStream and
FastMalloc units. Guest tests exercise their real borrowed-stream lifetime and
output, mixed floating/integer/UTF8 text, three workers and three cycles per
process, denied WRITE children and independent stderr after stdout closure.
No diagnostic or allocator method is replaced by a test stub. Exact application
bytes and54 complete worker lines are checked on each lifecycle. The serial
recovery shell shares the transport; the fixture separates only its exact prompt
and structured launch acknowledgment from application output. Per-process FILE
serialization does not promise atomicity with independent kernel diagnostics.

```sh
source /workspace/chos-tools/activate
make native-sdk CC=clang LD=ld.lld -j4
python3 tools/build_wtf_stdio.py --source "$WEBKIT_SOURCE" \
 --native-sdk build/native-sdk --engine-build "$JSC_BUILD/cmake" --out "$APP_OUT"
python3 tests/qemu_wtf_stdio.py --mode direct \
 --application "$APP_OUT/cmake/wtf_stdio" --out "$DIRECT_OUT"
python3 tests/qemu_wtf_stdio.py --mode uefi \
 --application "$APP_OUT/cmake/wtf_stdio" --out "$UEFI_OUT"
make test CC=clang LD=ld.lld -j1
make test-sanitize CC=clang LD=ld.lld -j1
```

See [recorded qualification and investigations](validation/stage3-standard-streams.json).
The read-stream unknown-syscall test now uses the current ABI limit rather than
assuming that SEEK+1 remains unassigned. Its seek/ownership tests are unchanged.
The SDK has a real positive stream/formatter link probe; missing-import/library
and full-ICU image-budget refusals remain checked. Original upstream Assertions,
FilePrintStream, PrintStream and FastMalloc objects compile in the JSCOnly target.

This removes concrete diagnostic-stream dependencies. Full WTF/JSC execution,
foreign-thread lifecycle/GC suspension, engine-sized memory, filesystem services,
remaining standard headers, modern browser integration/conformance, production
security policy, live Pi5 wireless and independent platform qualification remain
open. Stage3 is not complete, version remains0.30.0 and no new ISO is published.

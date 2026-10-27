# Native read streams and file positions

The native SDK now provides real read-only C streams over CHOS's existing
process-owned file snapshots. This is an ICU/browser runtime prerequisite, not a
modern browser or a complete C/POSIX I/O implementation.

`libchos-stdio.a` implements fopen (r/rb), fclose, fread, fgetc/getc, fgets,
ungetc, fseek/ftell, fgetpos/fsetpos, rewind, feof/ferror/clearerr, fflush and
fileno. Four streams share the kernel's four-descriptor budget. Each stream has
a 256-byte buffer, a logical cursor independent of kernel read-ahead, one byte
of guaranteed pushback, and separate EOF/error state. Registry and per-stream
locks yield when contended. There are no stream allocations. Output/update
modes fail explicitly before opening a file; output streams, formatting, wide
stream I/O and standard input/output FILE objects remain unimplemented.
`fflush(NULL)` has no pending output in this read-only profile. Flushing a
specific input stream synchronizes its physical cursor and discards read-ahead.
Mixing raw descriptor reads/seeks with buffered I/O requires that synchronization.

SEEK syscall 63 validates the owning address space/generation and open handle
under the process lock. SET/CUR/END accept positions from zero to INT64_MAX,
including beyond EOF. Reads beyond EOF return zero. Invalid origin, a position
before zero, and overflow leave the cursor unchanged. A bad destination during
a read also leaves the cursor unchanged. CHFS opens retain their immutable
snapshot behavior across subsequent file replacement.

SEEK belongs to the existing READ permission class. Permission masks retain
bits 1..62 and a positive signed result; syscall 63 never consumes the sign bit.
The actual syscall-number limit and permission-bit limit are now separate.
Reduced READ permissions deny SEEK before any descriptor operation. This does
not add directory/path-specific browser sandboxing, which remains an open gate.

## Evidence and reproducibility

Host normal and ASan/UBSan tests cover one million file-position calculations
against an independent signed-128-bit oracle; stream buffering, limits, short
reads, injected failures, EOF, pushback, 40,000 modeled operations, and concurrent
shared-stream readers. Native direct and isolated UEFI fixtures cover process
ownership, denied READ rights, failed user copies, snapshots, concurrent real
std::thread readers, exit status, heap cleanup and stable warm physical pages.
The ordinary CMake SDK fixture uses actual executable link probes and runtime
execution. See [recorded evidence](validation/stage3-native-stdio.json).

An early guest stress run yielded after every byte and exceeded the unchanged
30-second fixture deadline. The accepted workload yields every 128 consumed
bytes, still uses three native workers and timer preemption, and retains exact
byte histograms across three worker lifecycles. The initial failure is retained;
there is no claim that uncached single-byte scheduling is fast. A first GUI
fixture observed the completion text before asynchronous process cleanup had
finished; the accepted checker waits for actual quiescence instead of assuming
a fixed 400 ms cleanup delay. The first host
aggregate exposed a missing filename-test include path; that build rule is fixed.

Original ICU 79.1 umapfile.cpp now produces a native ARM64 object using its
existing MAP_STDIO implementation (`U_HAVE_MMAP=0`). Its real imports are fopen,
fclose, fread, fseek, ftell and ferror plus ICU memory functions. This brings the
common source inventory to 201/202 code-generated units when combined with the
previous 200-unit result. putil.cpp still needs platform time/locale services;
full ICU archive/data/i18n linking and runtime execution remain open.

## Fusion user test

The ISO embeds `/BIN/STDIO.ELF`; it is not run automatically. In CHOS Console,
enter `run /BIN/STDIO.ELF` (serial recovery uses `exec /BIN/STDIO.ELF`). It creates
`/DOCUMENTS/CHOS-IO-TEST.TMP`, refuses an existing file, and removes its test file
on success. A failed test can leave that named file; inspect it before removing
it to retry. It exercises native buffered I/O, permissions, separate processes,
threads and cleanup. Expect the final line:

```
[ OK ] native buffered read streams,seek ownership and permission boundaries passed
```

Repeat three times. Also check network status and example.com, held Backspace
and caret blinking in Console/editor/browser, persistent settings/files after a
restart, resolution controls, and shutdown/restart. Capture failure output and
`bootlog`, `drivers`, `pci`, `netstatus` if the new build behaves differently.
QEMU results do not establish Fusion compatibility; this build needs independent
Fusion validation. The earlier confirmed Fusion Ethernet result remains distinct.

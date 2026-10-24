# CHOS CHCX executable format (v1 and v2)

CHCX is a small memory-resident AArch64 executable format, not ELF. The kernel
loads immutable files from embedded ROMFS, validates the header, copies image
bytes into private address-space pages, then enters EL0. All file integers are
little-endian. Both formats retain a 1–8 page private RW/XN stack and a
position-independent image based at `CHOS_AS_USER_VA_BASE`.

## CHCX v1 header (24 bytes)

| Offset | Size | Field | Requirement |
|---:|---:|---|---|
| 0 | 4 | Magic | ASCII `CHCX` |
| 4 | 2 | Version | `1` |
| 6 | 2 | Header size | `24` |
| 8 | 4 | Image size | 1–65,536 initialized bytes |
| 12 | 4 | Entry offset | 4-byte aligned; at least one instruction remains |
| 16 | 2 | Stack pages | 1–8 |
| 18 | 2 | Flags | zero |
| 20 | 4 | Reserved | zero |

All v1 payload pages are EL0 read/execute and EL1 execute-never. Writable static
segments are not supported in v1. Existing v1 packaged programs remain valid.

## CHCX v2 header (56 bytes)

The first 24 bytes keep the v1 field positions, with `Version=2`, `Header size=56`,
`Image size` covering initialized payload bytes and `Entry offset` relative to the
start of the executable code segment. The 32-byte extension is:

| Offset | Size | Field | Meaning |
|---:|---:|---|---|
| 24 | 4 | Code size | executable bytes beginning at virtual offset zero |
| 28 | 4 | Read-only offset | page-aligned virtual offset of `.rodata` |
| 32 | 4 | Read-only size | initialized `.rodata` bytes |
| 36 | 4 | Data offset | page-aligned virtual offset of writable `.data` |
| 40 | 4 | Data size | initialized writable bytes |
| 44 | 4 | BSS offset | must equal `data_offset + data_size` |
| 48 | 4 | BSS size | zero-initialized writable bytes |
| 52 | 4 | Virtual size | page-rounded mapped image span |

The payload stores the linked image through the end of `.data`, including zero
padding between page-separated segments. The loader maps code pages RX, `.rodata`
pages R/XN, and `.data`/BSS pages RW/XN. Frames are zero-filled before initialized
bytes are copied, so BSS starts zero. The format forbids overlapping/out-of-order
segments, unaligned segment starts, unsupported flags, size overflow, an entry point
outside code, and images spanning more than 16 pages. Every file length must match
exactly `header_size + image_size`.

## Building and verifying

`tools/mkchcx.py payload.bin output.chx` builds a v1 image.
`tools/mkchcx.py --v2-symbols image.sym payload.bin output.chx` packages a linked
image as v2 using the linker labels listed by `nm`; `--verify` validates either
version. The Makefile compiles `userland/examples/cdemo.c`, emits a flat ARM64 image,
uses `userland/user.ld` to keep executable/read-only/writable/BSS regions page-separated,
and packages `/BIN/CDEMO.CHX` as v2.

## Runtime ABI and constraints

EL0 starts with `x0=argc`, `x1=argv`, `SP_EL0` 16-byte aligned, and a null-terminated
pointer vector. For processes spawned from the shell, `argv[0]` is the executable
basename. Additional shell arguments are whitespace-separated; quoting and
environment variables are not implemented. Syscalls use `x8` with `SVC #0`, inputs
in `x0`/`x1`, and result or negative errno in `x0`.

- `WRITE` (`1`): `x0` buffer, `x1` bytes (maximum 256); returns bytes written or `-errno`.
- `EXIT` (`2`): `x0` exit status; does not return.
- `GETPID` (`3`): current PID or `-ESRCH`.
- `YIELD` (`4`): yield scheduled task or `-ENOSYS` outside scheduled execution.
- `SLEEP` (`5`): `x0` timer ticks (maximum 60,000 at the current 100 Hz timer).
- `GETSTATUS` (`6`): `x0` PID; returns status code (loading=1, queued=2, running=3,
  exited=4, failed=5) or `-ESRCH`.
- `WAIT` (`7`): `x0` PID; yields until a different scheduled process becomes terminal
  and returns its exit status. Unknown PID returns `-ESRCH`, self-wait returns `-EDEADLK`,
  and waiting on a live process outside a scheduled EL0 context returns `-ENOSYS`.

## Limits

The table, executable image size, user stack and ROMFS are bounded. The OS still has
no disk-backed persistent filesystem, dynamic linking, environment variables, a
full libc, or a general-purpose user heap. This is a development runtime, not a
production security boundary; kernel locking and scheduling are currently single-core.

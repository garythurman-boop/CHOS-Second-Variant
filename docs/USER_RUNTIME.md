# CHOS user runtime and application services (0.12.0-dev)

## C startup and runtime

`userland/start.S` enters `main(argc, argv)` using the AArch64 C calling convention;
when `main` returns, its result is passed to `CHOS_SYS_EXIT`. The loader supplies
`argv[0]` and a null-terminated pointer vector on a 16-byte-aligned private stack.
`spawn /BIN/CDEMO.CHX argument-ok` demonstrates a second argument. Shell tokenization
is currently whitespace-only (no quotes or environment expansion).

The freestanding header `userland/libchos/include/chos/user.h` includes wrappers for
`chos_write`, `chos_getpid`, `chos_getstatus`, `chos_wait`, `chos_yield`,
`chos_sleep_ticks`, and `chos_exit`, plus small `memcpy`, `memset`, `strlen` and
`strcmp` equivalents. A general `malloc` is deliberately deferred until CHOS has a
user-space heap design.

## CHCX v2 memory model

CHCX v1 remains supported as a flat RX payload plus private RW/XN stack. CHCX v2
has page-separated executable code (RX), read-only data (R/XN), and writable data/BSS
(RW/XN). BSS frames are zero-filled by the kernel loader. The new C demo includes a
nonzero initialized writable global and a zero-initialized global to exercise both
segment classes. Segment bounds and entry points are validated before allocation.
See `docs/CHCX_FORMAT.md` for the binary header and packer interface.

## Process management

- `proclist` lists PIDs, states and exit statuses; `procinfo` retains detailed diagnostics.
- `status PID` reports a process's current or terminal status.
- In the scheduled runtime, `wait PID` yields until the requested process exits/fails.
- `chos_getstatus(pid)` returns a process status code; `chos_wait(pid)` returns its exit status.
- `spawn /BIN/CDEMO.CHX argument-ok` starts the C demo with a user argument.

Start with `runtime` to run an interactive scheduled shell. From there, use `spawn`,
`proclist`, `status PID`, and `wait PID`. `runtime-exit` leaves the runtime after live
scheduled work completes. From the boot shell, `spawn PATH` queues work and `runqueue`
executes the queued batch. A live-process wait needs the scheduled runtime; terminal
status queries can be made as long as the PID record has not been reused.

## Verification

`make test` includes host tests for CHCX v1/v2 validation, the v2 packer and existing
memory/scheduler/address-space policies. `make test-sanitize` runs host parser, allocator,
VMM-policy and scheduler tests under AddressSanitizer/UBSan. `make test-userland`
builds and verifies the sample C executable. Runtime execution of the new v2 C demo
should also be confirmed under QEMU on the development Mac.

## Scope and safety notes

CHOS remains a single-core EL1 kernel with EL0 applications. Its process table, image
size, address-space slots, stack and ROMFS are bounded. There is no persistent disk
filesystem, dynamic linker, full libc, user-space heap, or environment-variable support.
This remains an experimental OS, not a production security boundary.

## File descriptors and directory APIs (0.12.0-dev)

EL0 processes can use `chos_open(path)`, `chos_read(fd, buffer, length)`, and `chos_close(fd)` for process-owned file descriptors. `READ` accepts at most 256 bytes per call; zero indicates EOF. The file API also provides `chos_list_dir`, `chos_mkdir`, `chos_write_file`, `chos_unlink`, `chos_rmdir`, `chos_fsync`, and `chos_rename`. Paths are absolute and case-insensitive. Directory listing returns direct children only. Writes are capped at 64 KiB per syscall; the CHFS core's per-file limit is 1 MiB. Embedded ROMFS paths remain read-only. Persistent rename supports regular files and directory subtrees; directory moves reject destination collisions and self-descendant paths. There is no in-place file-growth API yet.

The current implementation stages a complete file into a kernel-owned buffer when opened, so the on-disk file checksum is checked before bytes are exposed. Each process has four descriptor slots. Closing a descriptor releases its owned buffer immediately; when a process exits or faults, process reaping closes all of its descriptors and releases any remaining buffers. ROMFS-backed entries may be opened as static read-only memory and do not allocate an owned file buffer. Descriptor integers are process-local and are not transferable between tasks.

The process filesystem ABI assumes a single-core kernel and synchronous block I/O. `READ` rejects lengths above `CHOS_SYS_MAX_READ` with `-E2BIG`; an invalid descriptor returns `-EBADF`; invalid user addresses return `-EFAULT`.


## GUI applications and file/process service calls (0.12.0-dev)

The user runtime provides `chos_gui_submit`, `chos_gui_poll_event`, `chos_list_files`, `chos_list_dir`, `chos_mkdir`, `chos_write_file`, `chos_unlink`, `chos_rmdir`, `chos_fsync`, `chos_rename`, `chos_spawn`, `chos_spawn_args`, and `chos_console_read`. `CHOS_SYS_LIST_FILES` returns a bounded merged CHFS/ROMFS catalog; `CHOS_SYS_LIST_DIR` returns the direct children of a directory. `CHOS_SYS_SPAWN_ARGS` passes a whitespace-separated argument tail to an embedded process; quotes and paths containing spaces are not supported. `CHOS_SYS_CONSOLE_READ` is a nonblocking stream available only to the current GUI Console owner PID; the 4096-byte stream discards oldest bytes if a child produces output faster than Console consumes it.

`/BIN/CONSOLE.CHX` and `/BIN/FILEMAN.CHX` are now embedded EL0 GUI apps. File Manager can launch `.CHX` entries and opens text-like entries with `/BIN/TEXTVIEW.CHX` using the selected path argument. The kernel's Console and File Manager views remain fallbacks. The GUI ABI is still experimental: four owned app windows and 32 retained drawing operations per app window are currently supported.


## CHFS directory and mutation API (updated 0.17.1-dev)

Directory metadata is persisted in CHFS v3 directory copies, with 511 total records and redundant 64-sector catalogues reserved at LBAs 1–128; data starts at LBA 129. The kernel continues to mount CHFS v1/v2 images and attempts guarded in-place migration through v2 to v3 at mount; back up development disk images before first use because older kernels cannot read v3 volumes. If migration cannot safely finish, the filesystem remains mounted in v1 format. `CHOS_SYS_WRITE_FILE` atomically replaces/creates a file in one request up to 64 KiB. The filesystem itself supports files up to 1 MiB using contiguous extents, and the API does not yet provide streaming writes or file growth. `CHOS_SYS_RENAME` supports persistent files and directory subtrees, with collisions and self-descendant moves rejected. The alternating checksummed metadata copies preserve a previous generation and protect its data extents during failed commits; this is not a journal and does not guarantee recovery from every power-loss or device-failure scenario.

The socket definitions in `include/chos/net.h` are a forward-looking ABI model only. 0.12.0-dev does not yet implement network syscalls, sockets, Ethernet, IP, TCP, DNS, or TLS.

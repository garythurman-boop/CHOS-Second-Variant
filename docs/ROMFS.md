# CHOS read-only ROMFS (0.12.0-dev)

The filesystem is a read-only catalog embedded in the kernel image. It does not
read a block device or persist changes. The API exposes absolute paths, bounded
lookup, listing and CHCX format validation. Path matching is ASCII case-insensitive;
traversal components, repeated separators, invalid characters and overlong paths
are rejected.

Built-in files:

- `/BIN/HELLO.CHX` — probes invalid-pointer rejection, writes a greeting and exits 0.
- `/BIN/ABOUT.CHX` — prints information about CHOS and the loader, then exits 0.
- `/BIN/FAULT.CHX` — intentionally reads an unmapped EL0 address for fault testing.
- `/BIN/ASYNC.CHX` — calls GETPID, writes a start message, sleeps for three timer ticks,
  writes a resume message, yields once and exits 0. It is intended for scheduled execution.
- `/BIN/CDEMO.CHX` — a C application packaged as CHCX v2. It checks `argc`/`argv`,
  initialized writable data, zero-initialized BSS, and timer sleep/wakeup behavior.

Existing programs use CHCX v1; `/BIN/CDEMO.CHX` uses CHCX v2. Both are embedded
as immutable bytes. `romfs_lookup()` returns those bytes and the process loader
validates the image before mapping it into a private user address space.

Useful commands:

- `apps` — list packaged programs and validate their CHCX headers.
- `exec /BIN/HELLO.CHX` — run a simple program synchronously.
- `spawn /BIN/ASYNC.CHX` followed by `runqueue` — run the asynchronous demo in a scheduled task.
- `spawn /BIN/CDEMO.CHX argument-ok` followed by `runqueue` — test the C runtime and v2 segments.
- `runtime` — enter a persistent interactive runtime and spawn background programs.
- `romfs-test` — validate catalog entries and exercise normal/faulting programs.

CHFS remains a flat, read-only-to-apps interface in this milestone; true directories and user-space file mutations remain future work. Arguments
are now passed to user applications, but shell tokenization is whitespace-only and
environment variables are not implemented.

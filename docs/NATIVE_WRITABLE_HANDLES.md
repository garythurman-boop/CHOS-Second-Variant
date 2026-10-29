# Native writable working-copy descriptors

The native API now opens a process-owned, read/write CHFS working copy with
explicit create, exclusive-create and truncate options. It uses the existing
four-descriptor table; ordinary `chos_open` continues to open immutable read-only
snapshots, including ROMFS recovery files. This is a native bounded service, not
a claim of POSIX filesystem conformance or a complete engine filesystem port.

`FILE_OPEN_WRITE` (70) accepts a path, an exact 32-byte version1 request and its
size. The request supplies flags and capacity; reserved fields must be zero.
Capacity is 1..4 MiB. Create/exclusive/truncate, snapshot data and its runtime
identity/revision are resolved atomically under the storage lock. Exclusive
requires create. Opening an existing directory is refused. Creation/truncation
is durable during open; later private changes are committed separately. A failed
allocation or descriptor/budget reservation cannot create or truncate a file.

`FILE_WRITE` (71) accepts at most256 bytes, advances the shared cursor and grows
only within the reserved capacity. It first copies the complete actual input
into a private temporary buffer. Bad user memory therefore cannot change a file
prefix, length or cursor. Seeking beyond EOF is supported; subsequent successful
writes and `FILE_TRUNCATE` (72) growth fill the gap with zeros. A write beyond
capacity is refused rather than wrapping or silently discarding data. Truncation
does not reposition the cursor. Reads and positional reads use the same locked
working view, while old read-only snapshots retain their original contents.

`FILE_FLUSH` (73) commits a dirty working copy against its original mount/record/
revision lease. A changed source returns ESTALE, preserving the caller's dirty
bytes. The filesystem's actual data/catalogue/flush error also preserves the
working copy and quarantines uncertain storage as documented in
[NATIVE_STORAGE_SNAPSHOTS.md](NATIVE_STORAGE_SNAPSHOTS.md). Successful flush
publishes the new lease and clears DIRTY. A clean view has no pending write to
flush; it does not overwrite a newer source simply because it is closed.

Existing `CLOSE` (10) flushes a dirty writable view before releasing it. A failed
close retains the descriptor and dirty bytes, so the application can report the
failure or explicitly discard them. `FILE_ABORT` (74) discards uncommitted changes
and closes. Process exit also discards without implicit disk writes; it does not
undo a create/truncate already committed during open. Applications must explicitly
flush successful changes. There is no automatic overwrite/reload after conflict.

The table reserves the descriptor and its entire capacity before allocation or
file-data I/O. Both legacy owned read snapshots and working buffers count against
an 8 MiB per-process budget, including reservations in progress. Embedded ROMFS
views require no owned buffer. Failed reservations have no filesystem side effect.
All reservations are cancelled on failure. Reserved/in-flight handles fence the
process reaper before any unmap/free. A dirty commit marks the descriptor busy,
releases the process lock for storage I/O, then publishes the result under that
lock. Sibling operations return EBUSY while busy; they cannot close, free, mutate
or replace its borrowed buffer. No process lock is held over CHFS I/O. This does
not remove CHFS's existing whole-file IRQ-save storage-lock latency.

Calls70..73 use the existing WRITE_FILE18 permission class; open additionally
requires OPEN8 and READ9. Abort uses CLOSE10. Positive capability bits1..62 remain
unchanged. Reduced children cannot acquire the new authority through a console
write permission. Descriptors belong to the calling address-space generation;
threads share their process's table, while children have independent tables.
Raw 64-bit descriptor values are checked before narrowing, including legacy READ
and CLOSE, preventing a high-bit value from aliasing a valid small descriptor.

Unprivileged writes to the reserved network profile/directory are refused. ROMFS
files cannot gain a new disk shadow through create; existing authorized disk
shadows can be edited, but concurrent removal cannot turn that open into an
unapproved creation. Existing CHFS path/filename restrictions remain. Metadata
keeps its existing64-byte layout: legacy SNAPSHOT/READ_ONLY flags retain their
meaning, while WORKING_COPY and DIRTY describe the new view. Reserved fields stay
zero. No inode numbers, timestamps or private kernel pointers are invented.

The host integration test runs actual CHFS/storage code with two simultaneous
exclusive creators, truncate/lease changes, invalid inputs and real lower-I/O
fault propagation, under normal and ASan/UBSan builds. Native qualification runs
an actual C++ application built from the staged SDK. It exercises shared-cursor
record writes from three threads, independent content validation, hole/truncate
zeroing, read-only snapshot stability, guard-page refusal with unchanged bytes and
cursor, stale flush/close/abort, two concurrent commits with exactly one winner,
concurrent 4 MiB reservations with exactly two of three admitted, shared budget
refusal without creation, reduced-capability and foreign-descriptor children,
rename conflicts and unsaved main/worker exit. The abandoned allocation is made
observable and checked through actual heap accounting before immediate exit.
Three complete process lifecycles must restore the cold physical-page baseline,
with no remaining user heap or queued futexes.

The associated validation record distinguishes host and QEMU evidence from target
hardware testing. This implementation does not add writable C stdio/POSIX fcntl,
full WTF filesystem linking, engine-sized asynchronous COW storage, larger files,
Unicode filenames, a full modern browser, or native Wi-Fi. Those remain Stage3
work. New Fusion, Pi5 and laptop qualification must not be inferred from QEMU.

Reproduce with the prepared native SDK and tools:

```sh
source /workspace/chos-tools/activate
make all uefi-iso install-disk-image native-sdk CC=clang LD=ld.lld -j4
python3 tools/build_native_write.py --native-sdk build/native-sdk --out "$APP_OUT"
python3 tests/qemu_native_write.py --mode direct --application "$APP_OUT/cmake/native_write" --out "$DIRECT_OUT"
python3 tests/qemu_native_write.py --mode uefi --application "$APP_OUT/cmake/native_write" --out "$UEFI_OUT"
make test CC=clang LD=ld.lld -j1
make test-sanitize CC=clang LD=ld.lld -j1
```

Fourteen accepted final guest profiles and the complete host/sanitizer suites are
recorded in [the validation](validation/stage3-native-writable-handles.json).
An initial IPv4 TLS profile timed out starting QEMU's monitor with no guest serial
output. The identical profile passed on rerun; its failed launch remains recorded,
with exact cause unresolved. No monitor, guest, network or TLS deadlines were
relaxed. Earlier host compile/root-classification failures and initial native
probes are also retained separately from final qualification.

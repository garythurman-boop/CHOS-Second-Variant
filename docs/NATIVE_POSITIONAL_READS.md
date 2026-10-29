# Native snapshot metadata and positional reads

The native file API now exposes the length and current cursor of the calling
process's open read-only snapshot. `FILE_INFO` (68) copies a versioned64-byte
value, with read-only/snapshot flags and an open-time CHFS-source flag. That flag
does not claim that the source is currently mounted or writable. No kernel
pointer, foreign process handle, inode, timestamp or invented POSIX stat metadata
is returned. Reserved fields are zero; exact output size and the entire writable
output range are checked before copying.

`READ_AT` (69) accepts an exact32-byte request containing an explicit offset,
output buffer and bounded length. Flags and reserved input must be zero. The
operation uses the same immutable open-time snapshot and ownership lock as the
existing sequential reader. It never saves/changes/restores the shared cursor.
Close, seek and sibling reads cannot invalidate a borrowed buffer while it is in
use because no snapshot pointer survives the existing process-lock interval.
The operation allocates no new snapshot and retains no user pointer after return.

Both new operations share the existing READ9 permission class. The positive
permission mask remains bits1through62; no sign bit is consumed. A child process
has its own descriptor table. A denied caller receives EPERM without output.
Bad descriptor values, requests, reserved fields, oversized reads and invalid
actual output spans are refused. Reads beyond EOF return zero and do not access
the destination. Actual nonempty results are fully validated before copy, so a
partly read-only destination is refused without a prefix write. These semantics
match the current immutable-snapshot service; writable handles remain separate
implementation work.

The native C `pread` adapter rejects negative offsets, issues a maximum256-byte
request and preserves the shared sequential cursor by using the atomic kernel
operation. Partial reads are intentional. It translates transport errors into
errno and refuses malformed positive counts or error values that cannot be
represented safely. It does not emulate positional reads with a racy seek pair,
retry forever, or invent EOF on an error. No POSIX device/file-lock/inode or full
filesystem conformance is inferred from this bounded interface.

The first native fixture found a real shared copy-helper defect: a destination
crossing from writable into read-only memory returned EFAULT after already
changing its writable prefix. `address_space_copy_to_user` now checks every
page before storing, under the same IRQ-save address-space lock used throughout
the copy. Its second walk uses the stable internal root walker without recursively
taking the lock. Protection, unmap and reap cannot change that plan between
validation and copy. It allocates nothing or schedules while holding the lock.
The existing process-to-address-space lock order is preserved. The regression
checks both positional and existing sequential reads, including unchanged cursor
and bytes on refusal. A short read at EOF can still succeed when only the actual
returned bytes fit the writable region; no blanket full-request check hides EOF.

The native fixture checks concurrent reads against an independent byte pattern,
uses three workers over three cycles, and confirms the sequential cursor remains
at its independently set position. It replaces/unlinks an actual CHFS file while
snapshots are open: old snapshots retain their bytes, while a new open sees the
replacement. Additional cases cover child-process ownership, READ permission
reduction, CHFS/ROMFS source flags, signed64 offset limits, EOF, canaries,
read-only/cross-page refusal, malformed request records and zero reserved output.
The runner repeats three process lifecycles and requires cold physical-page
baseline equality, zero remaining user heap and no queued futexes.

Fourteen accepted guest profiles pass: positional reads in direct/UEFI boots,
read streams in both modes, thread context, CPU/memory resources, native ELF and
memory isolation, ICU resources, TLS1.3 over IPv4/IPv6, isolated typing and direct/
UEFI Ethernet/storage/USB/desktop stress. The full host and sanitizer suites pass,
as do the adapter's independent normal and ASan/UBSan boundary/error tests.

The initial direct fixture's real prefix-write failure is retained, alongside
the corrected run. An initial typing run timed out waiting for the editor's
presented blink-on frame after Console/browser/editor repeat checks passed. Its
exact timing cause remains unestablished. The old fixture read visibility,
frame-generation counters and pixels at separate instants. It now snapshots all
of them while QEMU is paused only for that debugger observation, reads both64-bit
generation counters in full, and resumes before waiting for the next phase.
It never forces a caret/frame state. The rerun passes the unchanged3-second
phase deadlines and verifies different caret pixels in completed on/off frames.
No product input or blink timing was altered. The failed trace and initial
fixture are retained without retroactively marking that run PASS.
No complete browser, writable descriptor service, filesystem metadata model,
WTF/JSC integration or additional hardware support is claimed by this work.

```sh
source /workspace/chos-tools/activate
make native-sdk all uefi-iso CC=clang LD=ld.lld -j4
python3 tools/build_native_pread.py --native-sdk build/native-sdk --out "$APP_OUT"
python3 tests/qemu_pread.py --mode direct \
 --application "$APP_OUT/cmake/native_pread" --out "$DIRECT_OUT"
python3 tests/qemu_pread.py --mode uefi \
 --application "$APP_OUT/cmake/native_pread" --out "$UEFI_OUT"
```

Machine-readable evidence: [stage3-positional-reads.json](validation/stage3-positional-reads.json).
Retained raw archive: 98229435bytes; SHA-256
`f78aa3f65e0b92a8be06d46accc15d61afd78f4533f139cffa7495bc4d04a684`. All member bytes/lengths/modes were
verified against the retained original tree. No new ISO is published.

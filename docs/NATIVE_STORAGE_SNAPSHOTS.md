# CHFS snapshot ownership and conditional commits

CHFS now supplies a kernel-private snapshot/conditional-commit service for future
writable descriptors. An open snapshot carries a 32-byte lease containing a mount
identity, record identity, revision, catalogue index and zero reserved field.
These values identify runtime ownership; they are not persistent inode numbers,
POSIX stat metadata, a public user capability or a new on-disk format.

A successful snapshot verifies the actual CHFS file through its existing CRC
reader before returning the lease and byte count. It refuses directories, missing
files and insufficient capacity. Caller-owned output metadata is untouched on
failure. The private data buffer may contain a failed read's prefix and must be
discarded on failure; this API does not copy that buffer directly to user memory.

A conditional commit validates the complete lease and path under the same storage
lock as the filesystem mutation. An unrelated file's modification leaves the
lease valid. An identical-content write still changes that file's revision.
Rename changes the affected records' revisions, including directory descendants.
Delete/recreate receives a new record identity even if all catalogue bytes match
the deleted file. Remount, format and detach invalidate all previous leases.
Identities never wrap: exhausted 64-bit identity space refuses further mutation.
Only a successful commit publishes a replacement lease. Two writers holding the
same revision cannot both commit it; the loser receives ESTALE.

All existing storage mutation wrappers update this ledger, so the old whole-file
write/rename/unlink path cannot bypass stale-writer detection. Fixed BSS catalogue
scratch avoids placing a 32 KiB table on a 16 KiB scheduled kernel stack. The
existing IRQ-save storage lock serializes the catalogue, ledger and lower I/O.
This iteration does not solve whole-file I/O latency or provide asynchronous,
engine-sized storage; those remain explicit implementation work.

A filesystem-layer block proxy preserves the real backend descriptor, context,
geometry and driver callbacks. It records read/write/flush failures without
resetting DMA queues or changing driver ownership. In particular, a failed
inactive-catalogue read prevents the allocator from issuing subsequent writes
with unverified extent ownership. A failed data/catalogue write or flush makes
the filesystem read-only until recovery. A logical refusal such as an invalid
path does not quarantine an otherwise healthy device. Existing read-only access
continues where the selected catalogue and data can still be read.

Recovery mounts validate/select the actual CHFS catalogue and, after uncertainty,
require a successful real backend flush before re-enabling writes. A failed
recovery flush keeps the volume read-only. Missing flush support never produces
a fabricated durability acknowledgement: discovery and reads remain available,
but writes and storage sync are refused. A failed final catalogue flush is an
uncertain acknowledgement; a later recovery may select either complete catalogue,
including the new contents. No unconditional rollback-to-old-data claim is made.
Backend flush success retains the underlying hardware's durability guarantees;
this layer cannot manufacture persistence from a device that misreports them.

The pure identity test uses an independent 10,000-operation reference sequence,
rename, identical writes, byte-identical delete/recreate, remount, invalid records
and counter exhaustion. The integration fixture links the actual storage, CHFS,
GPT, block-device and lock code over an owned memory block device. It checks
independent files, a real two-thread commit race, subtree rename, zero-length
commits, unchanged output metadata on read failure, missing flush support and
failures at allocator reads, data writes, first flush, catalogue write and final
flush, including a failed callback that already changed part of the medium. Recovery itself is tested with a second failed flush before a good one.
Only hardware discovery is stubbed in this host fixture; it is not live hardware
qualification. Native regression results are recorded in the validation manifest.

There is no new public syscall in this iteration. Existing read-only descriptor
APIs and their permission classes remain unchanged. Public writable handles,
partial writes/truncate/flush/close semantics, quotas, large-file/cache policies,
engine filesystem adapters and browser persistence remain separate work. QEMU
results do not independently qualify these changes on Fusion, Pi 5 or laptops.

Reproduce the deterministic checks after activating the prepared environment:

```sh
source /workspace/chos-tools/activate
make test-storage-versions test-storage-snapshots
make test-storage-versions-sanitize test-storage-snapshots-sanitize
make test CC=clang LD=ld.lld -j1
make test-sanitize CC=clang LD=ld.lld -j1
```

 The exact native profile arguments, results and hashes appear
in [the validation record](validation/stage3-storage-snapshots.json). Native NVMe
writability/status now consults the current storage write authority rather than
reporting only its earlier enablement flag.

An initial installer profile reached INSTALL_FAILED during verified ESP writes,
before CHFS mount. Its progress screenshot and terminal state are retained; the
exact backend cause was not captured by that earlier fixture and remains
unresolved. The fixture now preserves job state, failing sector and actual
backend error strings before teardown, without masking the original exception.
The deliberately injected first-write failure validates that capture against the
real NVMe command-failure report, while keeping the disk unchanged and GUI alive.
No driver timeout or queue behavior was changed to conceal the initial failure.
The validation record distinguishes that investigation from accepted reruns.

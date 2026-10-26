# Bounded isolated-process messages

Syscalls49–53 add mutual-PID connection, read-only status, message send/receive
and close. This is a real prerequisite for separated applications; it is not
POSIX AF_UNIX/socketpair, shared memory, descriptor passing, a complete content
sandbox, or an integrated browser engine. Stage3 remains incomplete.

Both participants must explicitly connect to the other's live PID before any
message is sent. An application cannot inject a usable endpoint into a peer that
has not accepted it. One connection per PID pair is idempotent while open. Closed
pairs must finish closing before reconnecting. Only the current process bound to
the SVC address space can use its endpoint. No administrative operations or
kernel pointers are carried by the service.

Each of eight channels has two fixed queues of four512-byte messages. A process
can open at most four channels. Quota checks also apply when accepting a pending
connection. Sends are atomic messages; a full/unaccepted peer yields EAGAIN.
Undersized receive capacity yields EMSGSIZE without discarding the message. An
empty queue yields EAGAIN while the peer exists, then EOF after peer closure;
sending to a closed peer yields EPIPE. No internal waits or dynamic allocation
occur. The current four-process kernel profile remains unchanged.

Handles contain a monotonically issued generation plus endpoint index. Foreign,
closed, modified and stale handles are refused. Handle-generation exhaustion
fails closed. Process PID allocation now also fails closed at32-bit exhaustion
rather than recycling a security owner identity within the same boot. The latter
boundary has code review, not a claim of four billion actual guest launches.

The syscall checks PID width/live state, size limits and complete user ranges
before consuming or enqueuing data. It copies through a bounded kernel buffer,
so message storage never points into user memory. Status is a64-byte versioned,
pointer-free structure with zero reserved fields. Closing erases the destination's
unread queue; outgoing messages already queued to a live peer remain readable.
Both normal/fault cleanup paths close all endpoints for the exiting owner,
including pending connections; the final endpoint erases the channel's storage.

Host/ASan/UBSan tests cover ordering, backpressure, preserved messages on short
reads, owner rejection, peer death, stale handles, accepting over quota, global
exhaustion and10,000 repeated lifecycle/slot-reuse iterations. Live direct and
UEFI QEMU runs use actual EL0 parents, children and an unrelated process through
SVC. They verify512-byte round trips,24 foreign-process ownership attempts,
null/read-only/short destinations, guarded copies, child exit without close,
queued-message drain/EOF/EPIPE and repeated process/channel reuse.

The first live fixture sampled memory before the kernel file-buffer heap was
warm and incorrectly labeled its retained seven-page arena a process leak.
Those failures are retained. Corrected runs record the cold count, warm once,
then require equal quiescent free-page counts after three more complete runs.
This matches the existing heap's arena-retention policy; no claim is made that
all kernel cache allocations disappear after first use. Direct117696→117696 and
UEFI500535→500535 passed on the recorded local inputs.

Build `build/userland/ipc_test.elf`; run `make test-ipc test-ipc-sanitize` and
`python3 tests/qemu_ipc.py --mode direct|uefi --out NEW-DIRECTORY`. The runner uses
only disposable disks, records actual matched boot inputs, reads diagnostics,
and does not modify guest memory. Fusion and physical hardware tests are NOT RUN.
The full Stage3 binary hold and reservation of0.50.0 remain active.

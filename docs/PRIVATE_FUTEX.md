# Bounded private futex wait/wake

Native syscalls61/62 add WAIT_WORD and WAKE_WORD for32-bit words in the calling
process. The word must be aligned, mapped Normal memory and RW/XN. Ownership is
the complete address-space handle plus virtual address; the caller cannot select
another process. A foreign process with the same virtual address cannot wake it.
No hardware discovery, privileged device pointer or shared-process futex is added.

WAIT compares the word, publishes one bounded wait record and marks the current
task SLEEPING while local IRQs remain masked. It releases the lock before enabling
IRQs and actually waiting for dispatch. A waker therefore cannot run between the
value comparison and sleep publication. A wake occurring before the waiter reaches
the await function still works; no wake credit is stored for a future waiter.
This transaction is for the existing single-CPU, preemptive user profile, not SMP.

A mismatched value returns EAGAIN without sleeping. Relative timeouts are0..60000
100-Hz ticks, with zero meaning an immediate timeout after a matching comparison.
The record is removed on timeout, notification or process-group cancellation.
The scheduler's saved kernel stack remains live until SVC unwinds; an exit never
abandons a kernel lock or futex record. Queue storage is fixed at the eight-task
scheduler bound and each task can have one active wait. Capacity failure is
explicit; there is no dynamic allocation or user-memory pointer retained across
sleep, only its owner/address key.

WAKE returns the number of sleeping waiters it notified, capped by the requested
0..8 count. It validates the word and owner, skips expired/already-notified records
and makes selected tasks runnable. It does not transfer mutex ownership or promise
FIFO fairness. Callers must recheck their predicate in a loop. No priority
inheritance, robust-owner recovery, requeue, bitset or inter-process operation is
advertised. A mapping can be removed by another user thread while waiting, but the
kernel retains no physical pointer to it; the wait remains bounded and later wake
validation still checks the current mapping.

These operations use the existing immutable syscall permission guard. They are
optional rights; restricted children without them receive EPERM before queue
mutation. The positive scalar permission mask now reaches its62-bit bound. Future
syscall extensions require an explicit permission ABI redesign, rather than
silently shifting into a sign bit or treating unknown operations as permitted.

The host fixture compiles the actual wait implementation with deterministic
scheduler/IRQ mocks. It verifies comparison/queue/sleep ordering, an early wake
before await, foreign-owner refusal, timeout, cancellation, failed preparation,
invalid memory/bounds and capacity under ASan/UBSan. Those mocks are not hardware
support evidence. The initial guest fixture accidentally used Guest.symbol's
second argument as a size rather than an offset; its padding read is not accepted
as queue-cleanup evidence. Qualified runs read the actual counter correctly.

`tests/qemu_futex.py --mode direct|uefi --out FRESH` runs actual EL0 threads and
SVCs. Three workers contend on a private futex mutex, voluntarily yield while
holding it and produce600 exact updates per lifecycle. A separate process cannot
wake the matching virtual address. Real wake counts2 and1, value mismatch,
timeout, RX/invalid address, scalar-width and count rejection are exercised.
Three worker lifecycles run within each process, followed by repeated processes,
exit with blocked waiters and successful later reuse. Physical pages return to a
stable warm baseline; queued records must be zero. Keyless wait/wake/timeout/cancel
counters are captured from the immutable guest for actual execution evidence.

The complete C/C++/pthread runtime and modern browser integration are separate
remaining work. This primitive does not make the existing allocator or libraries
thread-safe by itself. New Fusion/Pi/laptop futex qualification is NOT RUN. No
Stage3 completion,0.50.0 version bump or binary publication is implied. See
validation/stage3-private-futex.json for exact profiles and retained investigation.

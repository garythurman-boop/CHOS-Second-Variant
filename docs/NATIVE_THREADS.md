# Bounded native threads and saved thread-local state

The native EL0 API now creates actual scheduled workers within the caller's
address space. Workers share globals, PID, file handles and process permissions;
other processes retain private mappings. No user entry runs at EL1. This supplies
a browser-runtime prerequisite, not complete POSIX pthreads or a modern engine.

## ABI and ownership

Syscalls56..60 provide CREATE, INFO, REAP, EXIT and SELF. CREATE copies a64-byte,
versioned request with zero reserved fields before queueing work to a scheduled
canonical-kernel-root task. It validates an aligned RX entry, a dedicated private
anonymous RW/XN stack mapping and an aligned, mapped RW/XN TLS pointer. Stack
length is page-aligned,4..128KiB; the entire mapping must match the allocation.
Arguments and TLS pointers belong to the process; the kernel never dereferences
an arbitrary user TCB during execution. Entry receives the supplied argument in
x0; the entry must finish through THREAD_EXIT rather than return to an absent LR.

Each process has at most three secondary join records, with the existing eight
scheduler slots shared by kernel tasks and all processes. Capacity failure is
explicit EAGAIN. Threads retain an address-space reference. Their stack mapping
is pinned against unmap and against a second worker using the same allocation.
No extra page permission or executable anonymous mapping is introduced.

INFO returns a32-byte, pointer-free snapshot. REAP releases a terminal join record;
it does not free caller-owned stack/TCB memory. A live record cannot be reaped.
The user may unmap its stack after exit/reap; process reaping also reclaims
abandoned mappings after all workers stop. The canonical worker reaps committed
dead scheduler stacks before creating replacements, so repeated create/join cycles
do not depend on the slow periodic maintenance interval. Foreign address spaces
cannot inspect or reap these IDs. Thread and scheduler IDs refuse exhaustion
rather than silently recycling identities; the4-billion boundary is code-reviewed,
not exercised by4-billion launches.

SELF identifies the current native task, including the main task. THREAD_EXIT
ends a secondary worker only; it returns EINVAL on the main task. Process EXIT
from any worker and any user fault terminate the group. Main-thread pthread_exit,
detach, signals and POSIX cancellation semantics are not supplied by this API.
The native C allocator/errno and libraries still need a coordinated thread-safe
runtime; merely creating threads does not make those libraries safe concurrently.

## Exit and fault cleanup

The process remains RUNNING while its main kernel trampoline stops and drains
workers. Pending creates are cancelled, sleeping tasks are woken, and a CPU-bound
EL0 worker is redirected through its own return trampoline on a timer return.
An EL1 IRQ frame is never redirected out of a live kernel critical section.
Blocking SVC WAIT/spawn paths observe cancellation and unwind through the original
SVC frame. CREATE has a500-tick service deadline instead of a permanent wait.
Only after workers leave EL0 are their stack pins released and the process's
sockets, GUI ownership, address-space mappings and file data eligible for cleanup.
A worker fault is recorded against its owning process, and the fault status wins
over a later normal main return.

File READ now keeps the source allocation and offset protected by the process
lock through its bounded copy. The former unlocked interval would allow a sibling
CLOSE to free the source. This was a code-review finding during thread enablement;
the precise former race is not represented as a reproduced fault. The live fixture
exercises simultaneous owned CHFS file opens/reads/closes and validates real ELF
data, alongside the shared-memory thread workload.

## AArch64 context

IRQ/SVC frames preserve TPIDR_EL0 at offset816, plus alignment padding, extending
the full base-FP/SIMD frame to832 bytes. Existing GPR, SP_EL0, Q0..Q31 and control
offsets remain unchanged. Synthetic frames initialize the field to zero. The EL0
trampoline installs the requested thread pointer and restores the caller's prior
value when returning to EL1. Main process entry retains its previous argc/argv
interface and initializes the user thread pointer to zero.

This is opaque thread-pointer preservation, not PT_TLS loading, compiler TLS
relocation support, libc per-thread errno, or the C++ thread_local runtime. SVE/SME
remain trapped and kernel C retains its integer-only register contract.

## Verification and remaining work

Host/sanitizer tests check frame offsets, request width/overflow/alignment/version/
reserved-field validation, exact stack pins, owner separation, unmap/release
refusal while pinned and release after unpin. The syscall permission guard extends
to60; process EXIT, thread EXIT and capability inspection are mandatory rights.
Restricted children cannot create or inspect threads without their granted bits.
An older ABI test incorrectly assumed CAPABILITIES55 was the last syscall; its
failure is retained and the test now checks the extended limit explicitly.

`tests/qemu_threads.py --mode direct|uefi --out FRESH` runs real EL0 workers from a
disposable CHFS disk. Three workers preserve distinct TLS pointers across IRQ/SVC/
sleep/yield, share a PID, make360,000 exact atomic updates per lifecycle, access
three simultaneous owned files, and reject foreign TID access. Three create/join
cycles run within each normal process. The fixture checks pinned-stack unmap and
duplicate-stack refusal, NX entry/RO TLS refusal, status destination/capacity,
join/reap, process exit with CPU-bound/sleeping workers, worker fault and later
normal owner reuse. Warm physical page counts must remain stable after every
normal/cancelled/faulted process group. First-use file-buffer heap arenas are
recorded separately rather than called a process leak.

Aggregate host/sanitizer, legacy FP, permission, C/C++ allocation, typing and TLS
checks are reported in validation/stage3-native-threads.json. QEMU is evidence for
those virtual profiles only; new Fusion/Pi/laptop thread qualification is NOT RUN.
Full pthread/futex/event APIs, compiler TLS, thread-safe runtime/libraries,
engine-sized memory, actual modern engine and wireless work remain mandatory.
No binary is published and0.50.0 stays reserved for complete revised Stage3.

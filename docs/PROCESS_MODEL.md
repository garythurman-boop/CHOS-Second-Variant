# CHOS process model (0.9.3-dev)

## Execution model

`process_run_image()` and `process_run_path()` are foreground smoke-test APIs. The
asynchronous APIs `process_spawn_image()` and `process_spawn_path()` load and validate
a CHCX program, create its private address space, and queue a scheduler task pinned to
that address space. `process_spawn_path()` resolves immutable bytes from embedded ROMFS.

The timer-preemptive scheduler activates the selected task's TTBR0 root before restoring
the saved kernel or EL0 exception frame. The kernel's code, runtime data and task stacks
remain reachable in each process root. User code is RX at EL0 and XN at EL1; user stacks
are RW and XN.

## Syscalls and user runtime

EL0 programs use `SVC #0`, with syscall ID in `x8` and arguments in `x0`/`x1`. The syscall ABI now includes file, GUI, network and entropy calls as defined in
`include/chos/syscall.h`; WRITE, EXIT, GETPID, YIELD and SLEEP remain supported. The tiny optional C header is
`userland/libchos/include/chos/user.h`. `/BIN/ASYNC.CHX` exercises PID lookup, write,
sleep, yield and exit. The example C source is compiled to an AArch64 object to verify
the ABI, but no general CHCX packer/startup runtime exists yet.

## Interactive runtime shell

The boot shell can queue work using `spawn PATH` and dispatch a batch through `runqueue`.
The opt-in `runtime` command creates a persistent scheduled kernel-shell task and enables
timer preemption for the runtime session. The shell can then spawn background user
processes while remaining interactive. `runtime-exit` leaves the runtime shell; the
scheduler continues until all other live tasks complete before returning to the boot shell.

## Per-task EL0 state

User return state is stored on each task's EL1 stack, not in shared globals. This allows
timer interrupts to save a task while another enters EL0 without corrupting saved return
trampoline state. The scheduler and process manager retain address-space references until
the relevant task has been reaped.

## Fault containment and cleanup

For supported lower-EL synchronous faults, CHOS records ESR/ELR/FAR in the owning process,
redirects exception return to an EL1 cleanup trampoline, and marks the process failed.
The process task exits through scheduler deferred-reap; only then can the process manager
unmap user pages, destroy its inactive address space and return owned frames to PMM.
Kernel-mode exceptions remain fatal.

## Testing

- `async-test` queues `/BIN/ASYNC.CHX` and `/BIN/HELLO.CHX`, checks syscall/wakeup activity, verifies both exit successfully, and checks PMM/address-space baselines.
- `spawn /BIN/ASYNC.CHX` followed by `runqueue` exercises the queued batch path.
- `runtime`, then `spawn /BIN/ASYNC.CHX`, exercises the persistent interactive session.
- `process-preempt-test` covers one normal CPU-bound user process plus a deliberate user data fault.

## Limits

Single-core only; scheduled runtime preemption is enabled automatically on graphical boot when supported, and remains explicitly available from the serial shell. There are no ASIDs, SMP, FP/SIMD context switching, general ELF loader, full IPC/message bus, signals, dynamic linker or full libc yet. CHFS is a disk-backed development filesystem with explicit/implicit directories, a bounded 511-entry CHFS v3 catalogue, contiguous extents and limited capacity; it has no full journal or block allocation map yet.

## File descriptor ownership

Since 0.7.0-dev, each process table record contains four read-only file descriptor slots. Each slot stores its file data pointer, size, current offset, and optional owned kernel buffer. A CHFS open allocates and checksum-validates a private kernel buffer; a ROMFS open references immutable embedded bytes. Descriptors are local to the owning process and are inaccessible through another process's address space. `CLOSE` detaches the slot before freeing its buffer. Process reaping closes every remaining descriptor after the process address space is no longer live, so a fault or exit cannot leave file buffers behind. File writes and cross-process descriptor passing are not currently exposed to EL0.


## Stage 1 entropy validation

`CHOS_SYS_RANDOM_BYTES` is bounded to 256 bytes, validates the owning process and
entire writable user range before consuming entropy, and returns EAGAIN without a
hardware result. `userland/examples/rng_test.c` is a live EL0 regression for null,
read-only and wrapping pointers, oversized/zero requests, output guards and both
present/absent entropy. The QEMU runner installs it only on its disposable CHFS copy.
Process code loading can resolve CHFS files before falling back to embedded ROMFS;
a corrupt found disk file fails closed. No TLS provider or entropy quality claim is
implied by a successful syscall transport test.

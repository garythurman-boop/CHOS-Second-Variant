# Native floating-point and SIMD context

The AArch64 application profile now enables base floating-point and Advanced SIMD.
Both IRQ and lower-EL synchronous exception entry save all 32 full 128-bit Q
registers, FPCR and FPSR alongside the existing GPR/ELR/SPSR/ESR/SP_EL0 frame.
The common return path restores the scheduler-selected frame. The first 288 bytes
retain their previous offsets; the original FP frame is816 bytes. The subsequent native-thread extension adds
TPIDR_EL0 and padding, making the current frame832 bytes, aligned to16. See
[NATIVE_THREADS.md](NATIVE_THREADS.md); the saved FP offsets remain unchanged.
Synthetic task and idle frames are explicitly zero-initialized.

Fresh EL0 entry clears all vector registers and both controls before user code.
It therefore cannot inherit data or rounding/exception state from a previous task.
CPU discovery requires base FP and Advanced SIMD before installing these vectors.
EL2 entry permits base FP; CPACR_EL1 enables it at EL0/EL1 while keeping SVE and
SME trapped. Scalable vectors and streaming/ZA state are not part of this ABI.

Kernel C must remain compiled with `-mgeneral-regs-only`: the cooperative EL1
switcher deliberately retains its existing integer-register ABI. Application
state is protected by the surrounding exception frames, including when a syscall
yields or sleeps. The explicit FP-enabled native test removes that compiler flag
for its own C object; other legacy applications retain their existing flags.

The MMU-off recovery path cannot run isolated EL0 applications. Its exception
frames initialize the FP area with integer stores and skip vector restoration,
avoiding 128-bit accesses to Device-typed RAM. This preserves the narrow recovery
contract; it does not add FP applications to that profile.

`tests/qemu_fpu.py` runs an actual native ELF from a disposable CHFS disk. Assembly
checks every Q lane and FPCR/FPSR before entering C. Two concurrent workers use
different PID-derived patterns and rounding modes, busy-loop without syscalls,
then YIELD/SLEEP and verify all 64 lanes plus controls. Real volatile double
arithmetic is compiled with the FP ABI. Three parent lifecycles create twelve
workers; physical page counts remain stable after first-use kernel heap retention.
Both direct and UEFI QEMU runs pass, with hundreds of measured timer preemptions.
Host layout checks, sanitizers, native protection/fault cleanup, Intel Ethernet
HTTP/link recovery and direct smoke also pass. See the machine-readable report.

This is a browser-runtime prerequisite. No modern engine, TLS or live Wi-Fi is
provided by it. New Fusion/Pi/laptop qualification is NOT RUN, Stage 3 remains
incomplete, binary publication stays held, and version 0.50.0 remains reserved.

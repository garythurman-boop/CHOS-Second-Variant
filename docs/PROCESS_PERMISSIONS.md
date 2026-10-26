# Inherited process syscall permissions

SPAWN_LIMITED (54) launches a child with a reduced effective syscall mask.
CAPABILITIES (55) reads the caller's own mask. Process EXIT, secondary-thread
EXIT and permission inspection remain available. The native thread extension
uses syscalls56..60, and private futex wait/wake use61/62, with the same
process-wide immutable permission guard. The scalar representation has now
reached its positive62-bit bound. Unknown bits, adding a permission the parent lacks, or an
invalid user string fail before loading or publishing a child. Unknown syscall
numbers fail closed until the kernel explicitly extends the supported range.
The current scalar ABI intentionally has a compile-time size bound; adding more
than62 syscall bits requires an explicit ABI redesign rather than silent truncation.

The kernel records policy in the bounded spawn request, applies it under the
process lock before creating the runnable task and retains it in the owning
address space's process record. Every known user syscall checks that record
before dispatch. Ordinary SPAWN/SPAWN_ARGS inherit the caller's current rights;
launching another executable cannot restore an omitted permission. Further
reductions are permitted, increases are not. The parent's rights are unchanged.

Trusted kernel desktop launches retain the existing full legacy profile, so
normal Console, File Manager, network tools and user programs keep their behavior.
Kernel-created development EL0 probes without process records retain only their
established WRITE/EXIT path, not an implicit full profile. The application cannot
edit its process record or install a different kernel root.

This primitive can deny filesystem reads/writes, network access, GUI creation,
entropy consumption or additional process creation to future content workers.
It does not select a WPE role automatically, assign site origins, restrict allowed
filesystem paths inside a granted file permission, broker browser permissions,
or implement a complete browser/content sandbox. Those policies and engine
integration remain mandatory Stage3 work. Native IOMMU/device isolation remains
separate. Existing profile-file protection stays in force for full-profile apps.

Host and sanitizer tests exercise every supported permission bit, mandatory rights,
empty masks, exact inheritance, escalation refusal and unchanged failure output.
Actual direct/UEFI EL0 parents create restricted children that attempt filesystem,
network, entropy and GUI operations; all are refused. Both ordinary spawn variants
produce grandchildren with the same restrictions. Attempts to spawn a full-rights
child fail. A further zero-optional-rights child can still exit. Bounded memory
allocation remains usable only when explicitly allowed. Repeated nested process
creation/exits preserve the parent's rights and stable warm free-page counts.

Run `make test-process-caps test-process-caps-sanitize`, build caps_test.elf, then
`tests/qemu_process_caps.py --mode direct|uefi --out NEW-DIRECTORY`. Guest tests use
only disposable disks and ordinary syscalls; no permission or process memory is
changed by the test controller. Evidence is in validation/stage3-process-permissions.json.
New Fusion/physical behavior is NOT RUN. No binary or0.50.0 completion release is
published for this prerequisite.

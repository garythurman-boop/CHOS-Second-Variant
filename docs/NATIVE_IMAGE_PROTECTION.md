# One-way image data protection and actual WTF configuration freezing

`IMAGE_PROTECT78` reduces permissions on the calling process's existing ELF data
pages. It shares MAP35's capability bit; it grants neither access to another
process nor additional write/execute permission. The libc `mprotect` adapter tries
this service only when anonymous-memory control reports an unsupported range and
the requested permission is read-only or inaccessible. Anonymous mappings retain
their existing reversible protection contract.

The base must be page aligned and the nonzero length at most1MiB. Length rounds
up to whole pages. Every page must belong to the current live image generation
and be an already-owned normal data descriptor. Executable pages, sparse holes,
stacks, anonymous allocations, device/kernel addresses, overflowing ranges and
unknown full-width operations are refused. Validation covers the complete range
before any descriptor changes. The existing allocation-free address-space
transaction performs break-before-make and local TLB maintenance under its lock.
The process-to-address-space lock order matches owned user-buffer copies. No disk
I/O, allocation or scheduling occurs inside the permission transaction. A request
can touch up to256 pages while local IRQs are masked; qualification does not
establish a physical-hardware interrupt-latency bound.

Only read-only/XN and inaccessible/XN are accepted. There is no operation to
restore write access, and a per-image-generation bitmap prevents restoring reads
after an inaccessible reduction. Other mapping, discard and unmap services cannot
replace or alter the image. Image ownership remains intact for fault/exit reaping.
Kernel copyout must respect read-only descriptors and copyin must refuse
inaccessible ones. These protections apply to the existing single-CPU profile;
they do not establish SMP, an IOMMU or a full browser sandbox.

The actual maintained WTF port already calls native `mprotect` from
`Config::permanentlyFreeze`. With this service its real `Config::finalize` path
can freeze the static configuration pages. The linked original-library fixture
checks the frozen flag, rejects write restoration and launches a child that
attempts a volatile write to its own frozen configuration. The child must suffer
an EL0 data abort; the parent remains runnable. This replaces the previous
checkpoint's explicit unsupported-static-range test. It does not provide
JavaScript execution, GC thread suspension or complete engine hardening.

The independent host policy oracle exercises50000 sparse/dense interval/set
cases, rounded endpoints, invalid full-width modes and unchanged error outputs.
Libc routing tests cover anonymous success, fallback errors, old-kernel ENOSYS
compatibility and refusal to route writable requests. The native fixture tests
invalid suffixes without changing the writable prefix, actual RX/stack/foreign
range refusal, kernel copy permissions, reversible anonymous mappings, three
concurrent sealing workers, two faulting children and a capability-denied child.
The exit assertion checks both the actual data-abort exit code and failed state.
Three complete process lifecycles must return to the cold physical-page baseline.
The linked WTF fixture separately verifies freeze/fault cleanup alongside its
filesystem and adopted-thread tests. All13 final guest profiles and the full host
and sanitizer suites pass, including direct/UEFI protection, direct/UEFI WTF,
reversible anonymous VM, ELF fault recovery, directory/ICU, IPv4/IPv6 TLS, typing
and the UEFI platform profile. The initial incorrect test exit-code assertion and
its original executable/trace are retained rather than counted as a passing run.

Run only on disposable disks: the filesystem fixture recursively removes fixed
test directories. See [machine-readable qualification](validation/stage3-native-image-protection.json)
for exact inputs, accepted profiles and retained failures. QEMU evidence does not
qualify new source on Fusion, Pi5 or laptops. Version remains0.30.0; all revised
Stage3 acceptance gates are still required before0.50.0.

# Native current-thread context and stack discovery

CHOS now provides a real read-only snapshot of the calling scheduled EL0 thread.
This supplies the engine port's native register layout and current stack bounds;
foreign-thread capture, suspension/resumption and the complete WebKit Thread/GC
implementation remain open. Stage 3 is not complete and version remains 0.30.0.

## ABI and ownership

THREAD_CONTEXT66 takes a writable output address, exact 832-byte size and flags0.
It shares the existing read-only THREAD_INFO57 permission class. The positive
64-bit capability-mask ABI and its bits1..62 remain the permission boundary;
callers must not shift a mask by66. Unknown flags/sizes fail before output writes.

The [public layout](../include/chos/thread_context_api.h) contains version/size,
current thread ID, validity flags, kernel-owned stack bounds, all31 general
registers, the user PC/SP, FPCR/FPSR and all32 full128-bit SIMD registers. The
reserved field is zero. ESR, privileged PSTATE, kernel addresses and internal
thread/TLS records are excluded. This is a native format, not Unix ucontext.

The dispatcher resolves the current address-space generation and capability first,
then validates the complete user output range. Thread identity and pinned worker
stack bounds come from kernel-owned thread records. Main stack bounds come from
the live process's loader-owned mapped-stack ledger. Thread/process locks remain
separate; no borrowed record crosses an unlock. A pure checked converter accepts
only a lower-EL0 frame, valid user PC, page-aligned bounded stack range and aligned
SP inside that range. It copies explicit user register fields into a zeroed public
record before checked user copyout. The caller cannot select another thread or
process, supply bounds/register contents, alter a frame or suspend a task.

An alternate anonymous stack is currently outside the registered stack contract
and is refused. Legacy unscheduled diagnostics do not gain this service. This is
not stack growth, signal delivery, a debugger, a GC suspension mechanism or an
engine-sized memory profile. Main stacks remain32KiB and workers at most128KiB;
WebKit's64KiB default reserved stack zone still requires a larger native profile
before ordinary engine execution can be accepted. The 4MiB image,8MiB owned-frame
and2MiB anonymous-aperture bounds continue to constrain the engine.

## Idle heap ownership correction

The regression matrix exposed allocation-order-dependent retention of an empty
four-page kernel heap arena during repeated ICU resource runs. Diagnostics showed
live payload unchanged at240bytes in two blocks, with no quarantine, while backing
pages increased from4 to8. A deterministic host case reproduces that retained
footprint alongside a live small anchor. Fully empty arenas now return to PMM,
including minimum-size arenas; failed ownership returns retain their quarantine.
The ICU runner keeps strict equality and now also requires the first run to match
the cold baseline. Original failed matrix and before/after diagnostics remain in
the evidence. This changes idle ownership policy rather than permitting a leak
allowance or adding arbitrary warm-up runs.

## Native engine and SDK integration

The checked upstream patch selects the native public register layout in
PlatformRegisters and excludes Unix signal-handler declarations for CHOS.
StackBounds.cpp obtains the current thread's bounds through the actual syscall
and checks ABI/validity before constructing the engine's bounds object. The
upstream stack-bound unit compiles in the real JSCOnly target. Its private class
method is build-qualified here; the native syscall itself has live guest evidence.
No complete WTF library, Thread lifecycle, GC or JavaScript execution is claimed.

The native CMake toolchain now explicitly targets AArch64 for assembly too. The
fresh SDK probe compiles/links the actual C++ application and assembly register
probe, with positive API links and the existing missing-import/library/full-ICU
budget refusals preserved. Source and SDK headers retain exact provenance checks.

## Qualification

```sh
source /workspace/chos-tools/activate
make thread-context-test all uefi-iso install-disk-image CC=clang LD=ld.lld -j4
make native-sdk CC=clang LD=ld.lld -j4
python3 tests/qemu_thread_context.py --mode direct --out "$DIRECT_OUT"
python3 tests/qemu_thread_context.py --mode uefi --out "$UEFI_OUT" \
  --application "$SDK_BUILD/sdk_thread_context"
make test CC=clang LD=ld.lld -j1
make test-sanitize CC=clang LD=ld.lld -j1
```

The actual assembly probe sets known GPR arguments/tags, all32 SIMD registers,
thread-specific differing lower/upper lanes, rounding modes and exception flags.
It checks the exact user PC at SVC return and restores the caller's callee-saved
registers and floating environment. Three workers repeatedly inspect their own
state; observed IDs/stacks are distinct and match live stack objects. Guards,
null/RO/wrapping/cross-page output, invalid flags/sizes, a temporary alternate stack
and actual reduced-capability children test refusals without output changes.
Repeated processes and worker cleanup return PMM pages to the cold baseline.

The typing regression now synchronizes on hardware acceptance before checking a
single initial deletion and reads full-width guest repeat counters. Event draining
checks the Console prompt and address-field clipping as displayed. Host boundary
checks retain490/500ms and25Hz/no-burst assertions. Live repetition keeps a bounded
six-second observation for six compositor-limited repeats, rather than equating
emulator rendering throughput with the deadline frequency. Window comparisons
wait for the completed new maximize/restore/drag frame, preserving their pixel
change requirement. These are fixture synchronization changes; product input and
window behavior were not altered. Earlier failed observations remain recorded.

Pure host/ASan/UBSan checks verify every mapped field, excluded privileged data,
ABI offsets and nonmutating invalid-frame/bounds failures. The full host/sanitizer
suites and guest regression matrix are recorded in
[the validation](validation/stage3-thread-context.json), including the earlier
build/probe investigations. QEMU evidence does not establish new Fusion, Pi 5 or
laptop qualification.

## Next engine work

Actual WTF compilation progresses beyond the Unix context header to further real
services: standard-stream diagnostics, filesystem interfaces, remaining standard
headers, process/page helpers and the engine's entropy adapter. The register type
and current bounds do not provide safe foreign-thread suspension. Rendering,
modern browser behavior/conformance, production browser TLS/time/trust/update
policy, live Pi 5 wireless, remaining IPv6/interface work and platform acceptance
still gate the revised Stage 3/version0.50.0 deliverable. The latest test ISO
remains cc29ea8; this is source work.

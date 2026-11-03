# Explicit native engine execution profile

Current checkpoint: [native engine reservations and first VM execution](NATIVE_ENGINE_RESERVATIONS.md). The real original-ICU VM now executes initial JavaScript/Intl and collection/release in direct and UEFI QEMU. The earlier boot/link failures and limits below are historical evidence. Full browser, Wasm/conformance, wireless, security and new hardware acceptance remain open. Engine main stacks are now 1 MiB; default/maximum secondary workers remain 128 KiB.

This records the resource/link checkpoint. The subsequent
[VM boot fixture](NATIVE_JSC_VM_BOOT.md) reaches executable entry, but creation
aborts at the missing aligned virtual-reservation service. JavaScript does not
yet run; later attempts do not turn this earlier link into VM qualification.

The executable loader now admits an explicitly marked engine application into
the separately accounted 192 MiB frame profile. A normal native executable keeps
its 4 MiB image, 32 KiB main/default worker stacks, 2 MiB anonymous window and
16 mapping records. CHCX limits are unchanged.

The engine profile has a 64 MiB image span, 128 KiB main/default worker stacks,
a 96 MiB anonymous window and 512 mapping records. It retains the existing
1 MiB limit on a single anonymous mapping, three secondary worker records,
private address-space window, W^X and exclusive engine-pool lease. These are
measured development limits, not a claim that a complete browser fits them.

The legacy ABI note remains version 1 with a four-byte descriptor. The explicit
engine note uses an eight-byte descriptor: version 1 and profile 1. Unknown,
ambiguous, duplicate and truncated profile notes refuse before allocation.
The loader reads the note independently of program-header order and applies its
own profile-specific file/span checks. A read-only CRT hint selects libc heap
metadata and pthread defaults; modifying that hint cannot grant kernel resources.
The SDK toolchain accepts `CHOS_APPLICATION_PROFILE=engine`; legacy is the default.

Libc preserves its ordinary 16-record allocator and adds caller-owned metadata
for up to 512 records. The engine runtime selects that external storage without
putting a large heap object on the application stack. Anonymous mappings remain
RW and execute-never, with guards. Terminal memory cleanup marks the owner as
releasing and pins its address space across an unlocked unmap loop. Ordinary
operations cannot reopen it. Process cleanup leases prevent duplicate reaping or
slot reuse while incremental teardown yields. A synchronous runner separately
retains its historical result record until its final access.

The resource fixture executes a real 48 MiB BSS image, touches every page with
distinct markers, uses 64 KiB of actual main and worker stack, allocates beyond
the former 16-record heap limit, and exercises protection, discard, partial
unmap and abandoned mappings. Direct and UEFI guests each pass three lifecycles,
four contained access faults and three malformed-profile refusals. Physical
pages, logical commitment, engine lease and futex waiters return to baseline.
Timer interrupts advance inside the large terminal teardown. Separate 256 MiB
guests refuse engine reservations without leaking resources. This fixture is
an application-resource test, not JavaScript execution.

The actual pinned JavaScriptCore C API VM now links with full original ICU data,
the 141-object JSC archive and its 53-object interpreter/bytecode subtarget. JIT,
DFG, FTL and sampling stay disabled; checked Wasm stays enabled. The resulting
54,814,608-byte file has a 50,675,712-byte virtual span, no unresolved symbols,
and passes the actual kernel loader policy with profile 1 and 32 stack pages.
Two positive links produce identical bytes. The ordinary 4 MiB profile still
refuses that VM at link time. Thin archive member contents are individually
hashed before linking and rechecked afterward, in addition to archive hashes.

The full VM has **not yet executed**. Its file exceeds the ordinary CHFS file
limit, so packaging and live runtime debugging are the next step. Full GC,
JavaScript/Intl/Wasm runtime, WebCore rendering, browser integration, native
wireless, production security policy and platform qualification remain open.
See [current results](STAGE3_CURRENT_RESULTS.md). Version remains 0.30.0;
0.50.0 requires every revised Stage 3 gate.

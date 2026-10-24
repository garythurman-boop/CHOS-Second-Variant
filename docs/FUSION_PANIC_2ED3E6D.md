# Fusion panic in build 2ed3e6d

The user reports that the published ISO panics on Fusion 26H1u1, macOS 27,
M1 MacBook Air, one CPU and 2 GiB RAM. The same source works in QEMU. This
supersedes earlier desktop-entry evidence for this revision. Fusion is unavailable
on the cloud machine; a corrected build still requires an actual Fusion retest.

## Exact symbolication

The screenshot reports ESR `0x96000035`, ELR `0xf868f650`, FAR `0xfb7cf598`.
Using the preserved, published UEFI ELF:

* DFSC `0x35` means unsupported exclusive or atomic access; EC `0x25` is a
  data abort at the current exception level.
* `uart_console_lock` is linked at `0x43351598`. Its reported physical address
  implies relocation delta `0xb847e000` and image base `0xf867e000`.
* Applying that same delta to ELR yields `0x40211650`, exactly
  `chos_spin_lock_irqsave+0x20`: `ldaxr w10, [x8]`.

This identifies an exclusive access to the UART lock, not an xHCI MMIO fault.
The loader deliberately disables MMU and cache before entry. With translation
disabled ARM64 data accesses have Device attributes; software cannot assume
exclusive accesses are supported. QEMU accepting this sequence hid the defect.

The old panic handler prints to GOP and then unconditionally takes the UART lock,
even when no supported UART was attached. A synchronous exception is not masked
by DAIF, so that fault recursively prints another report and eventually wraps
the GOP log. The screenshot identifies this reporting fault; the original fault
that first entered the handler cannot be recovered from that screenshot alone.
GUI dirty generation, timer ticks and other locks also used exclusive operations
with the MMU off and require correction, regardless of the original caller.

## Synchronization correction

`include/chos/atomic.h` is the shared kernel-RAM synchronization boundary. With
SCTLR.M clear, aligned loads/stores and bounded IRQ-masked read/modify/write
operations replace exclusive and acquire/release instructions. Barriers preserve
publication ordering and nested users restore the exact saved IRQ state. This is
valid for the existing **single-CPU** runtime; secondary CPUs must stay parked.
With translation enabled and kernel objects mapped Normal memory, the existing
hardware atomic operations remain. The choice reads SCTLR directly and requires
no initialized global state. It covers UART, PMM, heap, spinlocks, GUI generation,
timer ticks, user-mode flags and syscall counters.

This leaves the current identity-DMA, MMU/cache-off USB/PCI contract intact.
It does not enable the MMU without supplying appropriate MMIO/DMA mappings, and
it does not claim SMP support or introduce a platform-ID special case.

The host regression models a pending timer IRQ across read/modify/write, nested
masked callers, wraparound and the transition to hardware atomics. ARM64 compiler
probes verify that the MMU-off implementations emit no exclusive/acquire/release
instructions, the Normal-memory backend retains atomics, and runtime dispatch
tests SCTLR.M. A source audit rejects raw atomic bypasses outside the boundary.
Existing threaded spinlock/allocator tests still exercise the hardware backend.

Evidence is retained under `/workspace/chos-evidence/fusion3/`. Full host tests,
ASan/UBSan and direct QEMU serial smoke pass after this correction.
`atomic-native/result.json` passes native keyboard/tablet/wheel, 32 fast key pairs,
two nested hubs, 20 pointer reconnects, subtree removal with held-state release,
visible clock minute rollover, PCI VirtIO storage/network/RNG and persistence
across a fresh guest. It uses one CPU and 2 GiB RAM; idle sample is 1.0% CPU.
QEMU results never establish Fusion acceptance. Terminal panic-containment is a
separate follow-up so reporting cannot hide a later unrelated fault.

## Terminal panic containment

Fatal vectors latch once using plain aligned accesses before touching SP or
calling C, then switch to a reserved 8 KiB emergency stack. Nested synchronous
exceptions park at the vector without recursively logging or clearing GOP. A C
guard also covers fatal calls from the syscall dispatcher. DAIF remains masked.
The first ESR/ELR/FAR/SPSR/vector/image-base/SCTLR is retained in
`chos_first_panic`; `chos_panic_report_complete` marks a completed terminal report.

Emergency UART output bypasses its lock permanently and ignores an unattached
UART. GOP resets to a compact first-fault page with build revision, decoded cause,
addresses, SCTLR and relocated image/PC offset. Detailed serial output cannot
wrap away that page. DFSC `0x35` now has an explicit diagnostic description.

`tests/qemu_panic.py` boots immutable copies in disposable VMs, inserts a real
BRK at the completed bootstrap entry through QEMU's debugger, makes the ordinary
UART lock held and SP unusable, and verifies terminal reporting. It triggers a
second distinct BRK and checks byte-for-byte preservation of the first record,
framebuffer and serial output. A second VM detaches the UART before both faults.
This deliberately modifies guest instructions/registers only for fault injection;
it does not call CHOS input APIs or modify the copied disk/ISO/kernel files.
`panic-stack-injection/{serial,no-uart}/result.json` both pass with MMU off.
Host regression, ASan/UBSan and direct serial smoke also pass after this change.

An earlier `panic-injection` attempt completed its serial case, but its second VM
timed out in firmware before any EFI-loader/kernel log. Its immutable ELF and ISO
match. The later emergency-stack run boots both profiles and passes; the earlier
firmware timeout is retained and is not counted as a successful kernel test.

Reproduce with the prepared tool environment:

```sh
python3 tests/qemu_panic.py --out /workspace/chos-evidence/NEW-PANIC \
  --firmware /workspace/chos-tools/root/usr/share/AAVMF/AAVMF_CODE.fd \
  --qemu-data /workspace/chos-tools/root/usr/share/qemu
```

## Required Fusion retest

Use the newly published ISO identified in `artifacts/publication.json`, one CPU
and 2 GiB RAM. Verify desktop entry, advancing taskbar clock, keyboard typing and
Shift+letters, pointer alignment, buttons/wheel, dragging/maximize/restore,
reconnect and an idle soak. If input is missing, wait for the automatic rotating
driver reports and capture their actual controller/device/refusal information.
If a panic occurs, capture the single stable page including build ID and PC
offset. Keep NVMe disabled on valuable media. Actual Fusion device acceptance
and remaining Stage 1 platform/network/IOMMU gates remain open.

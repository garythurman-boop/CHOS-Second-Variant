# Kernel engine-frame admission and incremental teardown

The kernel now has a distinct engine frame profile with 49,152 owned pages
(192 MiB). Its ownership index has 131,072 buckets, separate from the four legacy
address-space tables. Exactly one engine space may hold this pool at a time.
The default executable loader still selects the original 2,048-page/8 MiB
profile. There is no new application syscall or executable ABI note in this
checkpoint, and the application image, heap and stack bounds remain unchanged.
This is a kernel memory service prerequisite, not admission or execution of the
full JavaScriptCore VM or a modern browser.

Profile IDs and exact limits are checked, rather than accepting caller-chosen
quotas. Logical commitments are bounded to 65,536 pages/256 MiB. Creation also
checks physical headroom for that profile plus a 64 MiB system floor and five
initial table pages. Every owned-page and new user-L3 allocation independently
checks the physical floor. Unknown/forged profiles, a second engine owner,
overflow and exhausted budgets refuse without transferring ownership.
Commitment is a logical limit, not a PMM reservation or a promise that later
kernel allocation cannot cause an application allocation to fail. Page-table
costs and other kernel allocations remain separate; failure must roll back.

Teardown first requires an inactive root with no task references and no user
mappings. It then marks the slot as destroying under the address-space lock.
Every handle operation refuses that state; the slot, pool lease and commitment
remain reserved. Ownership, table allocations and hash reachability are validated
before the first release. Physical scrubbing/release and the full-table scan run
with the caller's incoming IRQ state restored; short bookkeeping changes retain
the lock. No root can be activated or newly pinned during this phase. A failed
preflight or unexpected PMM release quarantines the slot and reservations until
reboot. Successful completion releases the commitment and exclusive pool only
after all pages and tables have been disposed.

This removes the old whole-process IRQ-save interval while retaining normal
page-level serialization. It does not force-enable interrupts for callers that
arrive with them masked, implement SMP, establish a maximum interrupt-latency
bound, or make future large anonymous mapping operations incremental. The
existing anonymous-memory layer and larger-image loader still need their own
resource-profile integration and cancellation/cleanup work.

`as-engine-frames-test` exercises the real kernel service. Each call owns and
maps the full 192 MiB RW+XN, checks every initially allocated byte for zero,
refuses over-quota/mapped-free/cross-owner/duplicate/stale requests, frees and
refills alternate pages, verifies every retained VA/PA/payload, and permutes
release order. It deliberately leaves 96 MiB of sparse unmapped owned pages
for destruction. The architectural timer IRQ count is read **inside** that
teardown call; the live diagnostic requires progress when the timer is enabled.
Three fresh lifecycles per guest also require the original physical-page count,
zero logical commitment and an unleased engine pool after every call.

Direct and UEFI pressure profiles boot with 256 MiB, refuse engine admission
three times without consuming physical pages or logical reservations, and then
pass the legacy full-quota diagnostic. A refusal's existing `[FAIL]` diagnostic
is expected in these pressure fixtures; their machine-readable result records
the intended refusal explicitly. Host policy tests cover exact admission
thresholds, overflow, forged fields and immutable failure outputs. Fault injection
into PMM release or simultaneous SMP teardown is not claimed.

The fixed engine ownership table and enlarged explicit-test page ledger add
2,473,984 bytes (604 pages) of kernel BSS relative to the previous indexed-frame
checkpoint. These pages are kernel reservations, not leaked runtime allocations.
The qualified direct text page count is unchanged. Dynamic index allocation and
multiple simultaneous engine spaces remain future work.

See [exact qualification](validation/stage3-engine-frames.json) and
[current Stage 3 scope](STAGE3_CURRENT_RESULTS.md). Fusion/physical evidence is
not inferred from QEMU. The shipped ISO remains `cc29ea8`, development version
remains 0.30.0, and all revised Stage 3 gates are still required for 0.50.0.

One initial full platform run timed out while waiting for the GUI ping result.
It had a bound DHCP lease, a received ARP reply and a ready driver; the separate
IPv4/IPv6 TLS fixtures passed on the identical kernel. Three instrumented platform
replays passed complete Ethernet, USB, window and storage/persistence checks.
Their captures each contain one matching ICMP request/reply with valid checksums,
endpoints, identifier, sequence and payload; the last two also require native
ping state REPLIED alongside the GUI text. The original timeout lacked packet
and console snapshots, so its cause remains unproven and is retained as a
qualification concern, not described as a repaired network defect. A separate
instrumentation run failed because Clang scalarized the private ping aggregate;
the corrected read follows its real first scalar, verified against the getter's
disassembly. That instrumentation failure is not a driver failure. No network
driver or kernel change was made for these replays.

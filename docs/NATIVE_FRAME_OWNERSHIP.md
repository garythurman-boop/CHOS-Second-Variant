# Indexed native frame ownership

Native address spaces now index their owned physical frames by the complete
64-bit page address. Mapping, unmapping, copying, protection and release use the
same ownership index. This removes the previous full-list lookup on each page,
which would make loading and reclaiming a large engine image quadratic.

The index is a bounded, power-of-two open-addressed table with at most half its
buckets occupied. Deletion closes the collision chain rather than accumulating
tombstones. Moving an entry preserves both its physical page and mapped user VA.
Callers hold the existing address-space lock; an entry index cannot outlive a
mutation. Removal first validates the affected cluster without changing ownership.
Only after that succeeds does the kernel scrub/release the physical page and
commit the allocation-free index update. A PMM refusal leaves ownership recorded.
Mapped pages, duplicates, invalid page addresses and foreign owners are refused.

This changes lookup and bookkeeping, **not application admission**. The existing
4 MiB ELF image, 8 MiB owned-frame quota, 2 MiB anonymous aperture and current
stack bounds still apply. The full JavaScriptCore VM remains too large for this
profile. No JavaScript VM, full GC, WebAssembly or modern-browser execution is
claimed. Larger per-profile address layouts, frame/heap/system reservations,
stacks, incremental large operations and executable-file loading remain needed.

Each of the four existing address spaces has 4,096 16-byte buckets, replacing
2,048 dense 16-byte entries. The explicit full-quota diagnostic additionally
reserves a 16 KiB page ledger. Together these add 144 KiB of kernel BSS, plus one
4 KiB text-page increment in the qualified direct kernel. There is no dynamic
allocation for the index and no new application syscall or capability.
Worst-case lookup/removal is still bounded by the bucket count; the index is
not an adversarial constant-time guarantee or an SMP implementation.

The pure algorithm has 2.5 million independently modelled operations at capacities
2,048 and 49,152, including high physical banks with repeated low address bits,
capacity and mapped-frame refusals, exact entry/VA/count conservation, repeated
hole reuse, and deliberately colliding chains that wrap around the table.
The 49,152-frame workload simulates bookkeeping for 192 MiB; it does not allocate
that amount in a guest or enable a larger native resource profile.

`as-frames-test` is an explicit kernel diagnostic. It requires the canonical
kernel root and no live application spaces, keeps the active root unchanged,
and serializes concurrent diagnostic calls. Each invocation has three rounds
of 2,048 real owned pages: allocate/verify all zero bytes/map RW+XN, free and
refill alternate frames while the others stay mapped, verify every physical
payload and user copy, then permute release order. It also tests quota,
mapped-free, duplicate-VA, cross-owner, double-free and stale-handle refusals,
and destroys sparse sets of owned but unmapped pages. Every round must return
to the exact cold physical-page baseline. Ordinary page operations retain their
existing IRQ-save locking; this does not qualify long-operation IRQ latency for
a future larger profile.

See [machine-readable qualification](validation/stage3-frame-ownership.json)
and [current Stage 3 boundaries](STAGE3_CURRENT_RESULTS.md). The initial live
harness incorrectly expected task switches without runnable application workers;
all actual kernel ownership checks passed. It was corrected to read the real
64-bit timer counter. Original failed harness runs and their diagnosis are retained
separately from corrected qualification. Timer progress across the test is not a
measurement of maximum interrupt latency.

The source is a prerequisite checkpoint. The published Fusion ISO remains
`cc29ea8`; development version remains 0.30.0. Version 0.50.0 requires all revised
Stage 3 gates, including the modern browser, native Pi 5 wireless and platform
and security acceptance.

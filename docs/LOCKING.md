# CHOS lock-order and context rules (0.6.0-dev)

CHOS is currently a single-core EL1 kernel. `chos_spin_lock_irqsave()` masks local IRQs before taking the atomic lock and restores the prior DAIF state on unlock. VMM page-table critical sections use the same local IRQ-masking principle. These rules protect shared metadata against timer-driven task preemption on the current CPU; they are not an SMP design.

## Permitted order and isolation

1. **Kernel heap lock -> PMM lock.** Heap arena growth requests physical pages while the heap lock is held. PMM never calls the heap.
2. **Address-space lock -> PMM lock.** Address-space construction, owned user-frame allocation, mapping metadata and teardown can request PMM pages while the address-space lock is held. PMM must never call back into the address-space manager.
3. **UART/console lock is isolated.** Do not acquire heap, PMM, address-space, or VMM mapping operations, call scheduler operations, sleep or yield while a UART transaction is held.
4. **VMM mapping operations are leaf-level critical sections.** Dynamic mappings are serialized by masking local IRQs. `vmm_map_page_auto()` combines find+map in one critical section; `vmm_find_free_dynamic_va()` is advisory. Do not call PMM/heap from inside VMM internals.
5. **Scheduler critical sections use IRQ masking, not allocator locks.** Do not acquire heap, PMM, address-space, UART or VMM mapping operations from timer/IRQ dispatch. Keep interrupt handlers short.

## Address-space rules

- Address-space roots are not active in `TTBR0_EL1` in 0.6.0; all current execution remains in the kernel root at EL1. Do not interpret `as-test` as proof of EL0 isolation at runtime.
- Each process root has separate L2 tables, but immutable kernel/device L3 tables are shared. Kernel/device leaves must remain EL1-only.
- User pages must be allocated via `address_space_alloc_user_page()`, which zeroes frames before first use. `address_space_map_user_page()` only maps frames owned by that exact address space; foreign PMM pages and kernel pages are rejected.
- A user frame can be mapped only once within an address space in this milestone. Unmap it before calling `address_space_free_user_page()`. Destroy requires all mappings to be removed, then zeroes/frees owned frames and private page tables.
- User page descriptors are RO or RW and always PXN+UXN. Do not add executable mapping permissions before direct-map aliases and TLB/ASID policy are redesigned.
- Do not activate a process root until a controlled TTBR0 switch path, required TLB invalidation/ASID handling, task ownership, and exception return path are implemented together.

## Critical-section rules

- Never call `scheduler_yield()`, `scheduler_sleep_ticks()`, or any operation that can switch tasks while holding a spinlock or inside a VMM page-table critical section.
- Never wait for timer ticks while holding a lock or with IRQs masked for a long operation.
- If an API returns a frame or mapping, keep ownership and lifetime explicit. Do not free/unmap backing memory while another task can access it.
- IRQ-save locks do not provide complete multicore safety. CHOS needs per-CPU interrupt state, SMP boot and an audited multicore TLB/lock implementation before SMP support.

## Tests

`make test` and `make test-sanitize` cover FDT, PMM, heap, VMM, scheduler policy, IRQ-frame ABI, spinlocks and address-space layout/descriptors. QEMU commands stress current hardware paths. Host-side tests do not replace live QEMU validation of page-table setup or future EL0 transitions.


## Process manager

The process table lock protects metadata snapshots and slot reservation only. It is
not held while allocating pages, creating/destroying an address space, switching
TTBR0, entering EL0, or printing. The current process runner is synchronous and
requires the scheduler to be idle, so process resource setup and teardown are not
concurrent with another process-manager operation. Do not turn this into async/user
task execution without revisiting lock ownership and per-task EL1 exception stacks.

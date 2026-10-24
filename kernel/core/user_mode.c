#include <stddef.h>
#include <stdint.h>
#include "chos/address_space.h"
#include "chos/pmm.h"
#include "chos/scheduler.h"
#include "chos/timer.h"
#include "chos/uart.h"
#include "chos/user_mode.h"
#include "chos/vmm.h"

#define USER_CODE_VA  (CHOS_AS_USER_VA_BASE)
#define USER_STACK_VA (CHOS_AS_USER_VA_BASE + 0x0000f000ull)
#define USER_STACK_TOP (USER_STACK_VA + VMM_PAGE_SIZE)

/* Synchronous shell-driven EL0 runs use this flag and handle. Scheduler-driven
 * processes derive their address space from the current scheduler task instead;
 * no process-local return state is stored in globals. */
static volatile uint32_t user_mode_session_active;
static address_space_handle_t user_mode_space;

extern uint8_t chos_user_demo_start[];
extern uint8_t chos_user_demo_end[];
extern int64_t chos_enter_el0(uint64_t entry, uint64_t user_stack_pointer,
                              uint64_t argc, uint64_t argv, int *faulted_out);

int user_mode_is_active(void) {
    return __atomic_load_n(&user_mode_session_active, __ATOMIC_ACQUIRE) != 0u;
}

int user_mode_get_address_space(address_space_handle_t *out) {
    if (out == NULL) return 0;
    if (scheduler_is_running()) {
        /* Lower-EL exceptions are dispatched on behalf of the currently selected
         * user task. Its address-space identity is task-local, not a global. */
        if (!scheduler_current_address_space(out)) return 0;
        return address_space_is_active(*out);
    }
    if (!user_mode_is_active()) return 0;
    *out = user_mode_space;
    return 1;
}

static int same_space(address_space_handle_t a, address_space_handle_t b) {
    return a.slot == b.slot && a.generation == b.generation && a.id == b.id;
}

int user_mode_execute_with_args(address_space_handle_t space, uint64_t entry,
                      uint64_t user_stack_top, uint64_t argc, uint64_t argv,
                      int64_t *exit_status_out, int *faulted_out) {
    vmm_mapping_info_t entry_mapping, stack_mapping;
    address_space_info_t space_info;
    address_space_handle_t current_space;
    uint64_t kernel_root;
    int scheduled, activated_here = 0;
    int64_t trampoline_status;
    address_space_error_t result;

    if (exit_status_out == NULL || faulted_out == NULL || !vmm_is_enabled()) return 0;
    scheduled = scheduler_is_running();
    if (!scheduled && user_mode_is_active()) return 0;
    kernel_root = vmm_kernel_root_table_address();
    if ((user_stack_top & 0x0fu) != 0u || user_stack_top == 0u ||
        !address_space_query(space, entry, &entry_mapping) ||
        !entry_mapping.accessible_at_el0 || !entry_mapping.executable_at_el0 ||
        entry_mapping.writable || entry_mapping.executable_at_el1 ||
        !address_space_query(space, user_stack_top - 1u, &stack_mapping) ||
        !stack_mapping.accessible_at_el0 || !stack_mapping.writable ||
        stack_mapping.executable_at_el0 || stack_mapping.executable_at_el1) return 0;

    if (scheduled) {
        if (!scheduler_current_address_space(&current_space) ||
            !same_space(current_space, space) || !address_space_is_active(space) ||
            !address_space_get_info(space, &space_info) ||
            vmm_current_root_table_address() != space_info.root_table_physical) return 0;
        /* The scheduler owns TTBR0 and switches it before this task resumes. */
    } else {
        if (vmm_current_root_table_address() != kernel_root) return 0;
        result = address_space_activate(space);
        if (result != AS_OK) return 0;
        activated_here = 1;
        user_mode_space = space;
        __atomic_store_n(&user_mode_session_active, 1u, __ATOMIC_RELEASE);
    }

    *faulted_out = 0;
    /* SP/DAIF/result storage is on this task's EL1 stack, never shared globally. */
    trampoline_status = chos_enter_el0(entry, user_stack_top, argc, argv, faulted_out);

    if (activated_here) {
        __atomic_store_n(&user_mode_session_active, 0u, __ATOMIC_RELEASE);
        if (address_space_deactivate() != AS_OK) {
            uart_puts("[PANIC] user_mode_execute could not restore the kernel address space\n");
            for (;;) __asm__ volatile("wfe");
        }
        user_mode_space = (address_space_handle_t){0u, 0u, 0u};
    }
    *exit_status_out = trampoline_status;
    return 1;
}

int user_mode_execute(address_space_handle_t space, uint64_t entry,
                      uint64_t user_stack_top, int64_t *exit_status_out,
                      int *faulted_out) {
    return user_mode_execute_with_args(space, entry, user_stack_top, 0u, 0u,
                                       exit_status_out, faulted_out);
}

void user_mode_request_exit(chos_irq_context_t *frame, int64_t status, int faulted) {
    extern void chos_user_mode_return(void);
    if (frame == NULL) return;
    /* x0/x1 become the per-call result registers consumed by the EL1 trampoline. */
    frame->x[0] = (uint64_t)status;
    frame->x[1] = faulted ? 1u : 0u;
    if (!scheduler_is_running()) {
        __atomic_store_n(&user_mode_session_active, 0u, __ATOMIC_RELEASE);
    }
    frame->elr = (uint64_t)(uintptr_t)chos_user_mode_return;
    frame->spsr = 0x3c5u; /* EL1h with D/A/I/F masked until the caller unwinds. */
}

void user_mode_force_return(chos_irq_context_t *frame, int64_t status) {
    user_mode_request_exit(frame, status, 1);
}

static void copy_bytes(uint8_t *destination, const uint8_t *source, size_t length) {
    size_t i;
    for (i = 0u; i < length; ++i) destination[i] = source[i];
}

int user_mode_self_test(void) {
    address_space_handle_t space = {0u, 0u, 0u};
    pmm_stats_t before_pmm, after_pmm;
    uint64_t code_page = 0u, stack_page = 0u;
    uint64_t kernel_root = vmm_kernel_root_table_address();
    size_t code_size = (size_t)(chos_user_demo_end - chos_user_demo_start);
    uint64_t writes_before, rejects_before;
    size_t spaces_before;
    int created = 0, code_mapped = 0, stack_mapped = 0;
    int activated = 0, entered = 0, exit_status = -1, faulted = 0, clean = 1, ok = 0;

    if (!vmm_is_enabled() || scheduler_is_running() ||
        vmm_current_root_table_address() != kernel_root || user_mode_is_active()) {
        uart_puts("[FAIL] el0-test requires an idle scheduler, the kernel root and an active MMU\n");
        return 0;
    }
    if (code_size == 0u || code_size > VMM_PAGE_SIZE) {
        uart_puts("[FAIL] EL0 test payload does not fit in one page\n");
        return 0;
    }

    pmm_get_stats(&before_pmm);
    spaces_before = address_space_count();
    writes_before = user_mode_write_syscalls();
    rejects_before = user_mode_rejected_writes();
    uart_puts("Preparing a private EL0 code page and stack; EL0 will use SVC write/exit only.\n");

    if (address_space_create(&space) != AS_OK) goto cleanup;
    created = 1;
    if (address_space_alloc_user_page(space, &code_page) != AS_OK) goto cleanup;
    if (address_space_alloc_user_page(space, &stack_page) != AS_OK) goto cleanup;
    copy_bytes((uint8_t *)(uintptr_t)code_page, chos_user_demo_start, code_size);
    /* Clean newly copied instructions to PoC. The entry trampoline invalidates the
       I-cache before ERET, since the code was populated through a kernel alias. */
    vmm_clean_page_table_range((const void *)(uintptr_t)code_page, code_size);
    __asm__ volatile("dsb sy" ::: "memory");

    if (address_space_map_user_page(space, USER_CODE_VA, code_page, AS_PERM_USER_RX) != AS_OK) goto cleanup;
    code_mapped = 1;
    if (address_space_map_user_page(space, USER_STACK_VA, stack_page, AS_PERM_USER_RW) != AS_OK) goto cleanup;
    stack_mapped = 1;

    {
        vmm_mapping_info_t code_map, stack_map;
        if (!address_space_query(space, USER_CODE_VA, &code_map) ||
            !code_map.accessible_at_el0 || !code_map.executable_at_el0 ||
            code_map.writable || code_map.executable_at_el1 ||
            !address_space_query(space, USER_STACK_VA, &stack_map) ||
            !stack_map.accessible_at_el0 || !stack_map.writable ||
            stack_map.executable_at_el0 || stack_map.executable_at_el1) goto cleanup;
    }

    user_mode_space = space;
    if (address_space_activate(space) != AS_OK) goto cleanup;
    activated = 1;
    __atomic_store_n(&user_mode_session_active, 1u, __ATOMIC_RELEASE);
    uart_puts("[ .. ] Entering EL0 test payload; successful output must come through SYS_WRITE.\n");
    entered = 1;
    exit_status = (int)chos_enter_el0(USER_CODE_VA, USER_STACK_TOP, 0u, 0u, &faulted);
    __atomic_store_n(&user_mode_session_active, 0u, __ATOMIC_RELEASE);

    if (exit_status != 0) {
        uart_puts("[FAIL] EL0 test payload returned status ");
        {
            uint64_t flags = uart_write_begin();
            uart_put_u64_locked((uint64_t)(int64_t)exit_status);
            uart_puts_locked("\n");
            uart_write_end(flags);
        }
    }
    if (address_space_deactivate() != AS_OK) {
        uart_puts("[PANIC] Could not restore canonical kernel address space after EL0 return; halted\n");
        for (;;) __asm__ volatile("wfe");
    }
    activated = 0;

cleanup:
    if (activated) {
        __atomic_store_n(&user_mode_session_active, 0u, __ATOMIC_RELEASE);
        if (address_space_deactivate() == AS_OK) activated = 0;
        else {
            uart_puts("[PANIC] Cannot deactivate failed EL0 test address space; halted\n");
            for (;;) __asm__ volatile("wfe");
        }
    }
    if (created && !activated && vmm_current_root_table_address() == kernel_root) {
        if (code_mapped) {
            if (address_space_unmap_user_page(space, USER_CODE_VA, NULL) == AS_OK) code_mapped = 0;
            else clean = 0;
        }
        if (stack_mapped) {
            if (address_space_unmap_user_page(space, USER_STACK_VA, NULL) == AS_OK) stack_mapped = 0;
            else clean = 0;
        }
        if (!code_mapped && !stack_mapped) {
            if (address_space_destroy(space) == AS_OK) created = 0;
            else clean = 0;
        }
    }
    user_mode_space = (address_space_handle_t){0u, 0u, 0u};
    pmm_get_stats(&after_pmm);

    ok = clean && entered && !created && !activated && !user_mode_is_active() &&
         exit_status == 0 && faulted == 0 && user_mode_write_syscalls() == writes_before + 1u &&
         user_mode_rejected_writes() == rejects_before + 1u &&
         vmm_current_root_table_address() == kernel_root && address_space_count() == spaces_before &&
         after_pmm.free_pages == before_pmm.free_pages &&
         after_pmm.allocated_pages == before_pmm.allocated_pages;
    if (ok) {
        uart_puts("[ OK ] EL0 executed a private RX page; invalid user pointer was rejected; SVC write/exit returned to EL1; memory counters restored\n");
    } else {
        uart_puts("[FAIL] EL0/syscall test, canonical-root restoration or cleanup did not return to baseline\n");
    }
    return ok;
}

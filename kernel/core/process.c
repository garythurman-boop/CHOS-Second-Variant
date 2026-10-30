#include "chos/threads.h"
#include "chos/ipc.h"
#include <stddef.h>
#include <stdint.h>
#include "chos/address_space.h"
#include "chos/user_memory.h"
#include "chos/pmm.h"
#include "chos/kheap.h"
#include "chos/net_tcp.h"
#include "chos/net6.h"
#include "chos/storage.h"
#include "chos/process.h"
#include "chos/image_memory_api.h"
#include "chos/image_memory_policy.h"
#include "chos/file_position.h"
#include "chos/file_write_api.h"
#include "chos/network_policy.h"
#include "chos/gui.h"
#include "chos/romfs.h"
#include "chos/scheduler.h"
#include "chos/spinlock.h"
#include "chos/timer.h"
#include "chos/syscall.h"
#include "chos/user_mode.h"
#include "chos/uart.h"
#include "chos/vmm.h"

#define PROCESS_PAGE_SIZE 4096u
#define PROCESS_CODE_VA CHOS_AS_USER_VA_BASE
#define PROCESS_NAME_DEFAULT "process"
_Static_assert((CHOS_ELF_MAX_SPAN % PROCESS_PAGE_SIZE)==0u,"Native span must be page aligned");
_Static_assert(PROCESS_CODE_VA+CHOS_ELF_MAX_SPAN<CHOS_USER_MAP_BASE,"Native images must not overlap anonymous memory");
_Static_assert(CHOS_PROCESS_IMAGE_PAGES>=CHOS_EXEC_MAX_IMAGE_PAGES,"CHCX images must fit the mapping ledger");

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t offset;
    void *owned_data;
    int active;
    int writable,dirty,busy;
    size_t capacity;
    chos_storage_lease_t lease;
    char path[CHOS_CHFS_PATH_MAX];
} process_file_handle_t;

typedef struct {
    chos_process_info_t info;
    uint64_t allowed_syscalls;
    address_space_handle_t space;
    process_file_handle_t open_files[CHOS_PROCESS_MAX_OPEN_FILES];
    uint64_t code_mapped_mask[(CHOS_PROCESS_IMAGE_PAGES+63u)/64u];
    uint64_t image_inaccessible_mask[(CHOS_PROCESS_IMAGE_PAGES+63u)/64u];
    uint32_t stack_mapped_mask;
    uint32_t sequence;
    uint64_t stack_base_va;
    uint64_t initial_stack_pointer;
    uint64_t argument_count;
    uint64_t argv_address;
    int address_space_live;
} process_slot_t;

static process_slot_t process_slots[CHOS_PROCESS_MAX];
static chos_spinlock_t process_lock = CHOS_SPINLOCK_INITIALIZER;
static uint32_t next_process_id = 1u;
static uint32_t process_sequence = 1u;
static uint64_t processes_started;
static uint64_t processes_exited;
static uint64_t processes_failed;
static size_t foreground_process_slot = CHOS_PROCESS_MAX;

#define PROCESS_SPAWN_REQUEST_CAPACITY 4u
#define PROCESS_SPAWN_REQUEST_PATH_CAPACITY 64u
#define PROCESS_SPAWN_REQUEST_ARGS_CAPACITY 128u
#define SPAWN_REQUEST_FREE 0u
#define SPAWN_REQUEST_PENDING 1u
#define SPAWN_REQUEST_RUNNING 2u
#define SPAWN_REQUEST_DONE 3u
typedef struct {
    uint32_t ticket;
    uint64_t allowed_syscalls;
    uint8_t state;
    uint8_t with_arguments;
    uint16_t reserved;
    char path[PROCESS_SPAWN_REQUEST_PATH_CAPACITY];
    char arguments[PROCESS_SPAWN_REQUEST_ARGS_CAPACITY];
    chos_process_error_t error;
    uint32_t pid;
} process_spawn_request_t;
static process_spawn_request_t spawn_requests[PROCESS_SPAWN_REQUEST_CAPACITY];
static uint32_t next_spawn_request_ticket = 1u;

extern uint8_t chos_user_demo_start[];
extern uint8_t chos_user_demo_end[];
extern uint8_t chos_user_exit_start[];
extern uint8_t chos_user_exit_end[];
extern uint8_t chos_user_preempt_good_start[];
extern uint8_t chos_user_preempt_good_end[];
extern uint8_t chos_user_preempt_fault_start[];
extern uint8_t chos_user_preempt_fault_end[];

static void process_scheduled_entry(void *argument);

static void record_process_failure(void) {
    uint64_t flags = chos_spin_lock_irqsave(&process_lock);
    ++processes_failed;
    chos_spin_unlock_irqrestore(&process_lock, flags);
}

static void zero_bytes(void *memory, size_t length) {
    uint8_t *bytes = (uint8_t *)memory;
    size_t i;
    for (i = 0u; i < length; ++i) bytes[i] = 0u;
}

static void copy_bytes(uint8_t *destination, const uint8_t *source, size_t length) {
    size_t i;
    for (i = 0u; i < length; ++i) destination[i] = source[i];
}

static void put_signed64_locked(int64_t value) {
    uint64_t magnitude;
    if (value < 0) {
        uart_putc_locked('-');
        magnitude = (uint64_t)(-(value + 1)) + 1u;
    } else {
        magnitude = (uint64_t)value;
    }
    uart_put_u64_locked(magnitude);
}

static void put_unsigned64(uint64_t value) {
    uint64_t flags = uart_write_begin();
    uart_put_u64_locked(value);
    uart_write_end(flags);
}

static void copy_name(char destination[CHOS_PROCESS_NAME_CAPACITY], const char *source) {
    size_t i = 0u;
    if (source == NULL || source[0] == '\0') source = PROCESS_NAME_DEFAULT;
    while (i + 1u < CHOS_PROCESS_NAME_CAPACITY && source[i] != '\0') {
        destination[i] = source[i];
        ++i;
    }
    destination[i] = '\0';
}

static int state_reusable(const process_slot_t *slot) {
    if (slot == NULL || slot->address_space_live || slot->info.cleanup_pending) return 0;
    return slot->info.state == CHOS_PROCESS_EMPTY ||
           slot->info.state == CHOS_PROCESS_EXITED ||
           slot->info.state == CHOS_PROCESS_FAILED;
}

static size_t choose_slot_locked(void) {
    size_t i, chosen = CHOS_PROCESS_MAX;
    uint32_t oldest_sequence = UINT32_MAX;
    for (i = 0u; i < CHOS_PROCESS_MAX; ++i) {
        if (process_slots[i].info.state == CHOS_PROCESS_EMPTY) return i;
        if (state_reusable(&process_slots[i]) && process_slots[i].sequence <= oldest_sequence) {
            oldest_sequence = process_slots[i].sequence;
            chosen = i;
        }
    }
    return chosen;
}

static void close_all_process_files(process_slot_t *slot) {
    size_t i;
    if (slot == NULL) return;
    for (i = 0u; i < CHOS_PROCESS_MAX_OPEN_FILES; ++i) {
        void *owned = slot->open_files[i].owned_data;
        zero_bytes(&slot->open_files[i], sizeof(slot->open_files[i]));
        if (owned != NULL) (void)kheap_free(owned);
    }
}

static int reap_slot(size_t index) {
    process_slot_t *slot;
    size_t i;
    if (index >= CHOS_PROCESS_MAX) return 0;
    slot = &process_slots[index];
    /* A reserved open or in-flight commit owns a buffer outside process_lock.
     * Terminal cleanup must not unmap/reuse that process or free its buffer. */
    uint64_t file_flags=chos_spin_lock_irqsave(&process_lock);
    int file_busy=0;
    for(i=0;i<CHOS_PROCESS_MAX_OPEN_FILES;i++)
        if(slot->open_files[i].active==2||slot->open_files[i].busy)file_busy=1;
    chos_spin_unlock_irqrestore(&process_lock,file_flags);
    if(file_busy)return 0;
    if (!slot->address_space_live) {
        close_all_process_files(slot);
        slot->info.cleanup_pending = 0;
        return 1;
    }
    /* All process-reaper work must run under the canonical kernel root. */
    if (vmm_current_root_table_address() != vmm_kernel_root_table_address()) return 0;
    if(!chos_threads_drop(slot->space)||!chos_user_memory_release(slot->space))return 0;

    for (i = 0u; i < slot->info.image_pages; ++i) {
        uint64_t bit = UINT64_C(1) << (i%64u);
        if ((slot->code_mapped_mask[i/64u] & bit) != 0u) {
            uint64_t va = PROCESS_CODE_VA + (uint64_t)i * PROCESS_PAGE_SIZE;
            if (address_space_unmap_user_page(slot->space, va, NULL) == AS_OK)
                slot->code_mapped_mask[i/64u] &= ~bit;
        }
    }
    for (i = 0u; i < slot->info.stack_pages; ++i) {
        uint32_t bit = UINT32_C(1) << i;
        if ((slot->stack_mapped_mask & bit) != 0u) {
            uint64_t va = slot->stack_base_va + (uint64_t)i * PROCESS_PAGE_SIZE;
            if (address_space_unmap_user_page(slot->space, va, NULL) == AS_OK)
                slot->stack_mapped_mask &= ~bit;
        }
    }
    uint64_t image_mapped=0;
    for(i=0;i<(CHOS_PROCESS_IMAGE_PAGES+63u)/64u;i++)image_mapped|=slot->code_mapped_mask[i];
    if (image_mapped == 0u && slot->stack_mapped_mask == 0u) {
        if (address_space_destroy(slot->space) == AS_OK) {
            slot->address_space_live = 0;
            slot->space = (address_space_handle_t){0u, 0u, 0u};
            /* Keep the last AS ID in the historical process record for procinfo. */
            close_all_process_files(slot);
            slot->info.cleanup_pending = 0;
            return 1;
        }
    }
    slot->info.cleanup_pending = 1;
    return 0;
}

void process_reap_pending(void) {
    size_t i;
    /* Drop scheduler-held AS pins whenever we are in a safe canonical kernel
     * context, including after a completed scheduler_run() returns to the boot
     * shell. scheduler_reap_completed() validates the context before touching
     * dead-task stacks, so callers need not guess whether scheduling is active. */
    scheduler_reap_completed();
    for (i = 0u; i < CHOS_PROCESS_MAX; ++i) {
        if (process_slots[i].info.cleanup_pending) (void)reap_slot(i);
    }
}

static chos_process_error_t fail_and_cleanup(size_t index, chos_process_error_t error) {
    process_slot_t *slot = &process_slots[index];
    if (slot->address_space_live && !reap_slot(index)) {
        slot->info.state = CHOS_PROCESS_FAILED;
        slot->info.cleanup_pending = 1;
        record_process_failure();
        return PROCESS_ERR_CLEANUP;
    }
    slot->info.state = CHOS_PROCESS_FAILED;
    slot->info.cleanup_pending = 0;
    record_process_failure();
    return error;
}

static chos_process_error_t process_load_image(const char *name, const void *file_bytes,
                                                size_t file_size, const char *const argv_in[],
                                                size_t argc_in, size_t *index_out,
                                                pmm_stats_t *before_out) {
    const uint8_t *file = (const uint8_t *)file_bytes;
    chos_exec_image_info_t image;
    process_slot_t *slot;
    address_space_info_t as_info;
    size_t index, i, code_pages, mapped_image_pages=0u;
    uint64_t stack_base;
    const char *default_argv[1];
    const char *const *argv = argv_in;
    size_t argc = argc_in;
    uint64_t irq_flags;

    if (file_bytes == NULL || index_out == NULL || before_out == NULL ||
        argc_in > CHOS_PROCESS_MAX_ARGS || (argc_in != 0u && argv_in == NULL))
        return PROCESS_ERR_ARGUMENT;
    if (!process_image_validate(file_bytes, file_size, &image)) return PROCESS_ERR_FORMAT;
    if (!vmm_is_enabled() || user_mode_is_active() ||
        vmm_current_root_table_address() != vmm_kernel_root_table_address() ||
        (scheduler_is_running() && !scheduler_current_is_kernel_task()))
        return PROCESS_ERR_CONTEXT;

    process_reap_pending();
    irq_flags = chos_spin_lock_irqsave(&process_lock);
    index = choose_slot_locked();
    if (index == CHOS_PROCESS_MAX || next_process_id == 0u) {
        chos_spin_unlock_irqrestore(&process_lock, irq_flags);
        return PROCESS_ERR_CAPACITY;
    }
    zero_bytes(&process_slots[index], sizeof(process_slots[index]));
    slot = &process_slots[index];
    slot->allowed_syscalls=CHOS_PROCESS_CAP_ALL;
    slot->info.state = CHOS_PROCESS_LOADING;
    slot->info.exit_status = -1;
    slot->info.pid = next_process_id++;
    /* Exhaustion stays zero: never recycle a security owner PID within a boot. */
    slot->sequence = process_sequence++;
    if (process_sequence == 0u) process_sequence = 1u;
    copy_name(slot->info.name, name);
    if (argc == 0u) {
        default_argv[0] = (name != NULL && name[0] != '\0') ? name : PROCESS_NAME_DEFAULT;
        argv = default_argv;
        argc = 1u;
    }
    slot->info.image_pages = image.virtual_size / PROCESS_PAGE_SIZE;
    slot->info.stack_pages = image.stack_pages;
    slot->info.entry_address = PROCESS_CODE_VA + image.entry_offset;
    slot->stack_base_va = CHOS_AS_USER_VA_LIMIT - (uint64_t)image.stack_pages * PROCESS_PAGE_SIZE;
    slot->info.user_stack_top = CHOS_AS_USER_VA_LIMIT;
    *index_out = index;
    chos_spin_unlock_irqrestore(&process_lock, irq_flags);

    code_pages = slot->info.image_pages;
    stack_base = slot->stack_base_va;
    if (PROCESS_CODE_VA + (uint64_t)code_pages * PROCESS_PAGE_SIZE >= stack_base)
        return fail_and_cleanup(index, PROCESS_ERR_FORMAT);

    pmm_get_stats(before_out);
    if (address_space_create(&slot->space) != AS_OK)
        return fail_and_cleanup(index, PROCESS_ERR_ADDRESS_SPACE);
    slot->address_space_live = 1;
    slot->info.address_space_id = slot->space.id;

    for (i = 0u; i < code_pages; ++i) {
        uint64_t physical = 0u;
        uint64_t va = PROCESS_CODE_VA + (uint64_t)i * PROCESS_PAGE_SIZE;
        size_t offset = i * PROCESS_PAGE_SIZE;
        size_t chunk = 0u;
        size_t source_offset=image.image_offset+offset;
        address_space_user_permissions_t permissions = AS_PERM_USER_RX;
        if(image.version==CHOS_EXEC_VERSION_ELF){
            chos_elf_page_t page;
            if(!chos_elf_page(&image.elf,(uint32_t)offset,&page))continue;
            source_offset=page.file_offset;chunk=page.file_bytes;permissions=page.permissions;
        }else if (offset < image.image_size) {
            chunk = image.image_size - offset;
            if (chunk > PROCESS_PAGE_SIZE) chunk = PROCESS_PAGE_SIZE;
        }
        if (address_space_alloc_user_page(slot->space, &physical) != AS_OK)
            return fail_and_cleanup(index, PROCESS_ERR_OUT_OF_MEMORY);
        if (chunk != 0u) {
            copy_bytes((uint8_t *)(uintptr_t)physical, file + source_offset, chunk);
            vmm_clean_page_table_range((const void *)(uintptr_t)physical, chunk);
            __asm__ volatile("dsb sy" ::: "memory");
        }
        if (image.version == CHOS_EXEC_VERSION_SEGMENTS) {
            if (offset >= image.data_offset) permissions = AS_PERM_USER_RW;
            else if (offset >= image.rodata_offset) permissions = AS_PERM_USER_RO;
        }
        if (address_space_map_user_page(slot->space, va, physical, permissions) != AS_OK)
            return fail_and_cleanup(index, PROCESS_ERR_MAPPING);
        slot->code_mapped_mask[i/64u] |= UINT64_C(1) << (i%64u);
        ++mapped_image_pages;
    }

    for (i = 0u; i < image.stack_pages; ++i) {
        uint64_t physical = 0u;
        uint64_t va = stack_base + (uint64_t)i * PROCESS_PAGE_SIZE;
        if (address_space_alloc_user_page(slot->space, &physical) != AS_OK)
            return fail_and_cleanup(index, PROCESS_ERR_OUT_OF_MEMORY);
        if (address_space_map_user_page(slot->space, va, physical, AS_PERM_USER_RW) != AS_OK)
            return fail_and_cleanup(index, PROCESS_ERR_MAPPING);
        slot->stack_mapped_mask |= UINT32_C(1) << i;
    }

    {
        uint64_t arg_pointers[CHOS_PROCESS_MAX_ARGS + 1u];
        zero_bytes(arg_pointers, sizeof(arg_pointers));
        uint64_t stack_cursor = slot->info.user_stack_top;
        uint64_t argv_address;
        size_t arg_index;
        size_t vector_size = (argc + 1u) * sizeof(uint64_t);
        for (arg_index = argc; arg_index > 0u; --arg_index) {
            const char *arg = argv[arg_index - 1u];
            size_t length = 0u;
            if (arg == NULL) return fail_and_cleanup(index, PROCESS_ERR_ARGUMENT);
            while (arg[length] != '\0' && length < CHOS_PROCESS_ARG_MAX_LENGTH) ++length;
            if (arg[length] != '\0') return fail_and_cleanup(index, PROCESS_ERR_ARGUMENT);
            ++length;
            if (stack_cursor < stack_base + length) return fail_and_cleanup(index, PROCESS_ERR_OUT_OF_MEMORY);
            stack_cursor -= length;
            if (!address_space_copy_to_user(slot->space, stack_cursor, arg, length))
                return fail_and_cleanup(index, PROCESS_ERR_MAPPING);
            arg_pointers[arg_index - 1u] = stack_cursor;
        }
        if (stack_cursor < stack_base + vector_size) return fail_and_cleanup(index, PROCESS_ERR_OUT_OF_MEMORY);
        argv_address = (stack_cursor - vector_size) & ~UINT64_C(0x0f);
        if (argv_address < stack_base ||
            !address_space_copy_to_user(slot->space, argv_address, arg_pointers, vector_size))
            return fail_and_cleanup(index, PROCESS_ERR_MAPPING);
        slot->argument_count = argc;
        slot->argv_address = argv_address;
        slot->initial_stack_pointer = argv_address;
    }

    if (!address_space_get_info(slot->space, &as_info) ||
        as_info.mapped_user_pages != mapped_image_pages + image.stack_pages) {
        return fail_and_cleanup(index, PROCESS_ERR_MAPPING);
    }
    return PROCESS_OK;
}

static void process_mark_running(size_t index) {
    uint64_t flags = chos_spin_lock_irqsave(&process_lock);
    process_slots[index].info.state = CHOS_PROCESS_RUNNING;
    ++processes_started;
    chos_spin_unlock_irqrestore(&process_lock, flags);
}

static void process_scheduled_entry(void *argument) {
    size_t index = (size_t)(uintptr_t)argument;
    process_slot_t *slot;
    int64_t status = -1;
    int faulted = 0;
    int entered;
    uint64_t flags;

    if (index >= CHOS_PROCESS_MAX) scheduler_exit_current();
    slot = &process_slots[index];
    process_mark_running(index);
    entered = user_mode_execute_with_args(slot->space, slot->info.entry_address,
                               slot->initial_stack_pointer, slot->argument_count, slot->argv_address,
                               &status, &faulted);
    if(!entered)faulted=1;
    chos_threads_finish(slot->space,&status,&faulted);
    chos_tcp_drop_owner(slot->info.pid);
    chos_net6_drop_owner(slot->info.pid);
    chos_ipc_drop_owner(slot->info.pid);

    /* This task cannot destroy its own active address space. Mark it pending;
     * scheduler cleanup switches to the kernel root and drops its pin first. */
    flags = chos_spin_lock_irqsave(&process_lock);
    slot->info.exit_status = status;
    slot->info.cleanup_pending = 1;
    if (entered && !faulted) {
        slot->info.state = CHOS_PROCESS_EXITED;
        ++processes_exited;
    } else {
        slot->info.state = CHOS_PROCESS_FAILED;
        ++processes_failed;
    }
    chos_spin_unlock_irqrestore(&process_lock, flags);
    chos_gui_process_exited(slot->info.pid);
    scheduler_exit_current();
}

chos_process_error_t process_run_image(const char *name, const void *file_bytes,
                                       size_t file_size, chos_process_result_t *result) {
    process_slot_t *slot;
    pmm_stats_t before, after;
    size_t index;
    int64_t exit_status = -1;
    int faulted = 0;
    int entered;
    chos_process_error_t error;
    uint64_t irq_flags;

    if (result != NULL) {
        result->pid = 0u;
        result->exit_status = -1;
        result->completed = 0;
    }
    if (result == NULL) return PROCESS_ERR_ARGUMENT;
    error = process_load_image(name, file_bytes, file_size, NULL, 0u, &index, &before);
    if (error != PROCESS_OK) return error;
    slot = &process_slots[index];
    process_mark_running(index);
    irq_flags = chos_spin_lock_irqsave(&process_lock);
    foreground_process_slot = index;
    chos_spin_unlock_irqrestore(&process_lock, irq_flags);
    entered = user_mode_execute_with_args(slot->space, slot->info.entry_address,
                                slot->initial_stack_pointer, slot->argument_count, slot->argv_address,
                                &exit_status, &faulted);
    chos_tcp_drop_owner(slot->info.pid);
    chos_net6_drop_owner(slot->info.pid);
    chos_ipc_drop_owner(slot->info.pid);
    chos_gui_process_exited(slot->info.pid);
    irq_flags = chos_spin_lock_irqsave(&process_lock);
    foreground_process_slot = CHOS_PROCESS_MAX;
    chos_spin_unlock_irqrestore(&process_lock, irq_flags);
    if (!entered || faulted) {
        result->pid = slot->info.pid;
        result->exit_status = exit_status;
        result->completed = 1;
        return fail_and_cleanup(index, PROCESS_ERR_EXECUTION);
    }

    irq_flags = chos_spin_lock_irqsave(&process_lock);
    slot->info.exit_status = exit_status;
    slot->info.state = CHOS_PROCESS_EXITED;
    ++processes_exited;
    chos_spin_unlock_irqrestore(&process_lock, irq_flags);

    if (!reap_slot(index)) {
        slot->info.state = CHOS_PROCESS_FAILED;
        slot->info.cleanup_pending = 1;
        record_process_failure();
        return PROCESS_ERR_CLEANUP;
    }
    pmm_get_stats(&after);
    result->pid = slot->info.pid;
    result->exit_status = exit_status;
    result->completed = 1;
    if (after.free_pages != before.free_pages || after.allocated_pages != before.allocated_pages) {
        slot->info.state = CHOS_PROCESS_FAILED;
        slot->info.exit_status = exit_status;
        record_process_failure();
        return PROCESS_ERR_CLEANUP;
    }
    return PROCESS_OK;
}

chos_process_error_t process_spawn_image(const char *name, const void *file_bytes,
                                         size_t file_size, uint32_t *pid_out) {
    return process_spawn_image_args(name, file_bytes, file_size, NULL, 0u, pid_out);
}

static chos_process_error_t process_spawn_image_args_policy(const char *name, const void *file_bytes,
                                         size_t file_size, const char *const argv[],
                                         size_t argc, uint32_t *pid_out,uint64_t policy) {
    process_slot_t *slot;
    pmm_stats_t ignored_baseline;
    size_t index;
    uint32_t task_id = 0u;
    chos_process_error_t error;
    uint64_t flags;
    if (pid_out != NULL) *pid_out = 0u;
    if (pid_out == NULL || argc > CHOS_PROCESS_MAX_ARGS || (argc != 0u && argv == NULL))
        return PROCESS_ERR_ARGUMENT;
    if (!timer_is_ready() || user_mode_is_active() ||
        vmm_current_root_table_address() != vmm_kernel_root_table_address() ||
        (scheduler_is_running() && !scheduler_current_is_kernel_task()))
        return PROCESS_ERR_CONTEXT;
    error = process_load_image(name, file_bytes, file_size, argv, argc, &index, &ignored_baseline);
    if (error != PROCESS_OK) return error;
    slot = &process_slots[index];
    flags = chos_spin_lock_irqsave(&process_lock);
    slot->allowed_syscalls=policy;
    if (!scheduler_create_task_in_address_space(slot->info.name, process_scheduled_entry,
                                                (void *)(uintptr_t)index, slot->space, &task_id)) {
        chos_spin_unlock_irqrestore(&process_lock, flags);
        return fail_and_cleanup(index, PROCESS_ERR_CAPACITY);
    }
    slot->info.scheduler_task_id = task_id;
    slot->info.state = CHOS_PROCESS_QUEUED;
    *pid_out = slot->info.pid;
    chos_spin_unlock_irqrestore(&process_lock, flags);
    return PROCESS_OK;
}

chos_process_error_t process_spawn_image_args(const char *name,const void *bytes,size_t size,
 const char *const argv[],size_t argc,uint32_t *pid){
 return process_spawn_image_args_policy(name,bytes,size,argv,argc,pid,CHOS_PROCESS_CAP_ALL);
}

int process_preemptive_isolation_test(void) {
    static uint8_t good_image[CHOS_EXEC_HEADER_SIZE + PROCESS_PAGE_SIZE]
        __attribute__((aligned(16)));
    static uint8_t fault_image[CHOS_EXEC_HEADER_SIZE + PROCESS_PAGE_SIZE]
        __attribute__((aligned(16)));
    size_t good_size = (size_t)(chos_user_preempt_good_end - chos_user_preempt_good_start);
    size_t fault_size = (size_t)(chos_user_preempt_fault_end - chos_user_preempt_fault_start);
    pmm_stats_t pmm_before, pmm_after;
    scheduler_stats_t sched_before, sched_after;
    chos_process_stats_t proc_before, proc_after;
    chos_process_info_t good_info = {0}, fault_info = {0}, info;
    uint32_t good_pid = 0u, fault_pid = 0u;
    size_t i;
    size_t spaces_before;
    int have_good = 0, have_fault = 0, ok;
    uint64_t kernel_root = vmm_kernel_root_table_address();

    if (scheduler_is_running() || !timer_is_ready() || user_mode_is_active() ||
        vmm_current_root_table_address() != kernel_root || good_size == 0u ||
        fault_size == 0u || good_size > PROCESS_PAGE_SIZE || fault_size > PROCESS_PAGE_SIZE) {
        uart_puts("[FAIL] process-preempt-test requires idle scheduler, timer and canonical kernel root\n");
        return 0;
    }
    process_reap_pending();
    spaces_before = address_space_count();
    pmm_get_stats(&pmm_before);
    scheduler_get_stats(&sched_before);
    process_get_stats(&proc_before);

    if (!process_image_write_header(good_image, sizeof(good_image), (uint32_t)good_size, 0u, 1u) ||
        !process_image_write_header(fault_image, sizeof(fault_image), (uint32_t)fault_size, 0u, 1u)) {
        uart_puts("[FAIL] could not construct CHCX stress images\n");
        return 0;
    }
    copy_bytes(good_image + CHOS_EXEC_HEADER_SIZE, chos_user_preempt_good_start, good_size);
    copy_bytes(fault_image + CHOS_EXEC_HEADER_SIZE, chos_user_preempt_fault_start, fault_size);

    uart_puts("Spawning two EL0 tasks: one CPU-bound normal exit, one deliberate user data fault.\n");
    if (process_spawn_image("user-good", good_image, CHOS_EXEC_HEADER_SIZE + good_size, &good_pid) != PROCESS_OK)
        goto cleanup_partial;
    have_good = 1;
    if (process_spawn_image("user-fault", fault_image, CHOS_EXEC_HEADER_SIZE + fault_size, &fault_pid) != PROCESS_OK)
        goto cleanup_partial;
    have_fault = 1;
    if (!scheduler_set_preemption_enabled(1) || !scheduler_run()) {
        (void)scheduler_set_preemption_enabled(0);
        process_reap_pending();
        uart_puts("[FAIL] preemptive user-process scheduler did not complete\n");
        return 0;
    }
    (void)scheduler_set_preemption_enabled(0);
    process_reap_pending();

    for (i = 0u; i < CHOS_PROCESS_MAX; ++i) {
        if (process_get_info_at(i, &info)) {
            if (info.pid == good_pid) good_info = info;
            if (info.pid == fault_pid) fault_info = info;
        }
    }
    pmm_get_stats(&pmm_after);
    scheduler_get_stats(&sched_after);
    process_get_stats(&proc_after);
    ok = have_good && have_fault && good_info.pid == good_pid && fault_info.pid == fault_pid &&
         good_info.state == CHOS_PROCESS_EXITED && good_info.exit_status == 0 &&
         fault_info.state == CHOS_PROCESS_FAILED && fault_info.cleanup_pending == 0 &&
         good_info.cleanup_pending == 0 && sched_after.preemption_switches > sched_before.preemption_switches &&
         sched_after.completed_tasks >= sched_before.completed_tasks + 2u &&
         sched_after.reaped_tasks >= sched_before.reaped_tasks + 2u && sched_after.pending_reaps == 0u &&
         sched_after.address_space_switch_failures == sched_before.address_space_switch_failures &&
         sched_after.address_space_tasks == 0u && !sched_after.running && !sched_after.preemption_enabled &&
         proc_after.started >= proc_before.started + 2u && proc_after.exited >= proc_before.exited + 1u &&
         proc_after.failed >= proc_before.failed + 1u &&
         vmm_current_root_table_address() == kernel_root && address_space_count() == spaces_before &&
         pmm_after.free_pages == pmm_before.free_pages &&
         pmm_after.allocated_pages == pmm_before.allocated_pages;
    uart_puts("Good process pid/status: ");
    { uint64_t flags = uart_write_begin(); uart_put_u64_locked(good_info.pid); uart_puts_locked("/");
      uart_put_u64_locked((uint64_t)good_info.exit_status); uart_puts_locked("; fault process pid/state: ");
      uart_put_u64_locked(fault_info.pid); uart_puts_locked("/"); uart_puts_locked(process_state_name(fault_info.state));
      uart_puts_locked("\n"); uart_write_end(flags); }
    uart_puts("Preemption switches: ");
    { uint64_t flags = uart_write_begin(); uart_put_u64_locked(sched_after.preemption_switches - sched_before.preemption_switches); uart_puts_locked("\n"); uart_write_end(flags); }
    if (ok) uart_puts("[ OK ] timer-preempted EL0 tasks: normal process survived, user fault isolated, cleanup returned to baseline\n");
    else uart_puts("[FAIL] EL0 task preemption, fault isolation, task cleanup or memory baseline check failed\n");
    return ok;

cleanup_partial:
    /* Run any task already queued so the scheduler drops its address-space pin. */
    if (have_good) {
        (void)scheduler_set_preemption_enabled(1);
        (void)scheduler_run();
        (void)scheduler_set_preemption_enabled(0);
        process_reap_pending();
    }
    uart_puts("[FAIL] could not queue both EL0 test processes\n");
    return 0;
}

/* Prefer mutable disk-backed files; the embedded ROMFS remains a recovery source.
 * A found disk entry with a bad data checksum fails closed instead of silently
 * executing an older ROMFS copy at the same path. */
static int resolve_program_file(const char *path, chos_romfs_file_t *file, void **owned_data) {
    chos_chfs_entry_t entry;
    size_t bytes_read = 0u;
    void *buffer;
    if (owned_data != NULL) *owned_data = NULL;
    if (storage_lookup(path, &entry)) {
        if(entry.size_bytes>CHOS_ELF_MAX_FILE)return -1;
        buffer = kheap_alloc(entry.size_bytes == 0u ? 1u : entry.size_bytes);
        if (buffer == NULL) return -2;
        if (!storage_read_file(path, buffer, entry.size_bytes, &bytes_read) || bytes_read != entry.size_bytes) {
            (void)kheap_free(buffer);
            return -1;
        }
        file->path = path; file->data = (const uint8_t *)buffer; file->size = bytes_read;
        if (owned_data != NULL) *owned_data = buffer;
        return 1;
    }
    if (romfs_lookup(path, file)) return 1;
    return 0;
}
static chos_process_error_t resolve_file_error(int result) {
    if (result == -2) return PROCESS_ERR_OUT_OF_MEMORY;
    if (result < 0) return PROCESS_ERR_STORAGE_IO;
    return PROCESS_ERR_NOT_FOUND;
}

static int space_equal(address_space_handle_t left, address_space_handle_t right) {
    return left.slot == right.slot && left.generation == right.generation && left.id == right.id;
}
static size_t find_process_slot_for_space_locked(address_space_handle_t space) {
    size_t i;
    for (i = 0u; i < CHOS_PROCESS_MAX; ++i) {
        if (process_slots[i].info.state != CHOS_PROCESS_EMPTY &&
            process_slots[i].address_space_live && space_equal(process_slots[i].space, space)) return i;
    }
    return CHOS_PROCESS_MAX;
}

int process_main_stack_bounds(address_space_handle_t space,uint64_t *base,uint64_t *bytes){
 if(!base||!bytes)return -CHOS_ERRNO_EINVAL;
 uint64_t flags=chos_spin_lock_irqsave(&process_lock);size_t index=find_process_slot_for_space_locked(space);
 int rc=-CHOS_ERRNO_ESRCH;
 if(index<CHOS_PROCESS_MAX){
  process_slot_t *slot=&process_slots[index];uint32_t pages=slot->info.stack_pages;
  if(slot->info.state==CHOS_PROCESS_RUNNING&&pages&&pages<=CHOS_EXEC_MAX_STACK_PAGES&&
     slot->stack_mapped_mask==((UINT32_C(1)<<pages)-1u)){
   *base=slot->stack_base_va;*bytes=(uint64_t)pages*PROCESS_PAGE_SIZE;rc=0;
  }
 }
 chos_spin_unlock_irqrestore(&process_lock,flags);return rc;
}

static size_t file_owned_bytes_locked(const process_slot_t *slot){
    size_t bytes=0;
    for(size_t i=0;i<CHOS_PROCESS_MAX_OPEN_FILES;i++)bytes+=slot->open_files[i].capacity;
    return bytes;
}
/* Reserve a table slot and its complete owned-buffer budget before allocation
 * or storage I/O. A reserved slot is invisible to sibling descriptor calls and
 * blocks terminal reaping until publication or cancellation. */
static int reserve_file_buffer(address_space_handle_t space,size_t capacity,size_t *owner,size_t *descriptor){
    uint64_t flags=chos_spin_lock_irqsave(&process_lock);
    size_t index=find_process_slot_for_space_locked(space),i;int result=0;
    if(index==CHOS_PROCESS_MAX||process_slots[index].info.state!=CHOS_PROCESS_RUNNING||process_slots[index].info.cleanup_pending){result=-CHOS_ERRNO_ESRCH;goto done;}
    process_slot_t *slot=&process_slots[index];
    for(i=0;i<CHOS_PROCESS_MAX_OPEN_FILES;i++)if(!slot->open_files[i].active)break;
    if(i==CHOS_PROCESS_MAX_OPEN_FILES){result=-CHOS_ERRNO_EMFILE;goto done;}
    size_t used=file_owned_bytes_locked(slot);
    if(used>CHOS_FILE_OWNED_BUDGET||capacity>CHOS_FILE_OWNED_BUDGET-used){result=-CHOS_ERRNO_ENOMEM;goto done;}
    process_file_handle_t *file=&slot->open_files[i];
    zero_bytes(file,sizeof(*file));file->active=2;file->capacity=capacity;
    *owner=index;*descriptor=i;
done:
    chos_spin_unlock_irqrestore(&process_lock,flags);return result;
}
int process_image_protect(address_space_handle_t space,uint64_t address,uint64_t bytes,uint64_t operation){
 uint64_t irq=chos_spin_lock_irqsave(&process_lock);
 size_t index=find_process_slot_for_space_locked(space);int result=-CHOS_ERRNO_ESRCH;
 if(index==CHOS_PROCESS_MAX)goto done;
 process_slot_t *slot=&process_slots[index];uint32_t first,count;
 if(!slot->address_space_live||slot->info.state!=CHOS_PROCESS_RUNNING||slot->info.cleanup_pending)goto done;
 result=chos_image_memory_range(PROCESS_CODE_VA,slot->info.image_pages,slot->code_mapped_mask,
  slot->image_inaccessible_mask,address,bytes,operation,&first,&count);
 if(result)goto done;
 /* The AS primitive independently validates every already-owned normal data
  * descriptor before its allocation-free PTE transaction. RX/kernel/device
  * frames are refused; no write/execute promotion is requested. Lock order is
  * process->AS, matching owned-file user copies. No scheduling or I/O here. */
 address_space_error_t error=address_space_protect_user_range(space,address,(size_t)count*PROCESS_PAGE_SIZE,
  operation==CHOS_MEMORY_PROTECT_NONE?AS_PERM_USER_NONE:AS_PERM_USER_RO);
 if(error!=AS_OK){result=-CHOS_ERRNO_EINVAL;goto done;}
 if(operation==CHOS_MEMORY_PROTECT_NONE)for(uint32_t i=first;i<first+count;i++)
  slot->image_inaccessible_mask[i/64u]|=UINT64_C(1)<<(i%64u);
 result=0;
 done:
 chos_spin_unlock_irqrestore(&process_lock,irq);return result;
}

int64_t process_file_open_for_address_space(address_space_handle_t space,const char *path){
    if(!path||!romfs_path_is_valid(path))return -CHOS_ERRNO_EINVAL;
    chos_chfs_entry_t entry;chos_romfs_file_t embedded={0};
    size_t capacity=0,bytes=0,index,i;void *owned=NULL;chos_storage_lease_t lease={0};
    int persistent=storage_lookup(path,&entry);
    if(persistent){
        if(chfs_entry_is_directory(&entry))return -CHOS_ERRNO_EISDIR;
        if(entry.size_bytes>CHOS_ELF_MAX_FILE)return -CHOS_ERRNO_E2BIG;
        capacity=entry.size_bytes?entry.size_bytes:1u;
    }else if(!romfs_lookup(path,&embedded))return -CHOS_ERRNO_ENOENT;
    int result=reserve_file_buffer(space,capacity,&index,&i);
    if(result)return result;
    if(persistent){
        owned=kheap_alloc(capacity);
        if(!owned)result=-CHOS_ERRNO_ENOMEM;
        else result=storage_read_view(path,owned,capacity,&bytes,&lease);
    }else{
        bytes=embedded.size;
        for(size_t ordinal=0;ordinal<romfs_file_count();ordinal++){
            chos_romfs_file_t candidate;
            if(romfs_get_at(ordinal,&candidate)&&romfs_path_equal(candidate.path,path)){
                lease.object=(uint64_t)ordinal+1u;break;
            }
        }
    }
    uint64_t flags=chos_spin_lock_irqsave(&process_lock);
    process_slot_t *slot=&process_slots[index];process_file_handle_t *file=&slot->open_files[i];
    if(!result&&(!space_equal(slot->space,space)||slot->info.state!=CHOS_PROCESS_RUNNING||slot->info.cleanup_pending))result=-CHOS_ERRNO_ESRCH;
    if(!result){file->data=persistent?owned:embedded.data;file->owned_data=owned;file->size=bytes;file->lease=lease;file->active=1;}
    else zero_bytes(file,sizeof(*file));
    chos_spin_unlock_irqrestore(&process_lock,flags);
    if(result&&owned)(void)kheap_free(owned);
    return result?result:(int64_t)(CHOS_PROCESS_FD_BASE+i);
}

int64_t process_file_read_for_address_space(address_space_handle_t space, int fd,
                                            uint64_t user_destination, size_t length) {
    size_t index, file_index, available, amount, offset; const uint8_t *data;
    uint64_t flags; process_slot_t *slot;
    if (fd < CHOS_PROCESS_FD_BASE || fd >= CHOS_PROCESS_FD_BASE + (int)CHOS_PROCESS_MAX_OPEN_FILES)
        return -(int64_t)CHOS_ERRNO_EBADF;
    if (length > CHOS_SYS_MAX_READ) return -(int64_t)CHOS_ERRNO_E2BIG;
    if (length == 0u) return 0;
    file_index = (size_t)(fd - CHOS_PROCESS_FD_BASE);
    flags = chos_spin_lock_irqsave(&process_lock);
    index = find_process_slot_for_space_locked(space);
    if (index == CHOS_PROCESS_MAX || process_slots[index].open_files[file_index].active!=1) {
        chos_spin_unlock_irqrestore(&process_lock, flags);
        return -(int64_t)CHOS_ERRNO_EBADF;
    }
    slot = &process_slots[index];
    if(slot->open_files[file_index].busy){chos_spin_unlock_irqrestore(&process_lock,flags);return -CHOS_ERRNO_EBUSY;}
    data = slot->open_files[file_index].data; offset = slot->open_files[file_index].offset;
    available = offset<slot->open_files[file_index].size ? slot->open_files[file_index].size-offset : 0u;
    amount = length < available ? length : available;
    if (amount == 0u) {chos_spin_unlock_irqrestore(&process_lock,flags);return 0;}
    if (!address_space_copy_to_user(space,user_destination,data+offset,amount)){
        chos_spin_unlock_irqrestore(&process_lock,flags);return -(int64_t)CHOS_ERRNO_EFAULT;
    }
    process_slots[index].open_files[file_index].offset += amount;
    chos_spin_unlock_irqrestore(&process_lock, flags);
    return (int64_t)amount;
}

/* Positional reads share the same process-owned view and ownership lock as
 * sequential reads/writes. No seek/save/restore window is exposed to siblings. */
int64_t process_file_read_at_for_address_space(address_space_handle_t space,int fd,
        uint64_t destination,size_t length,uint64_t offset) {
    if(fd<CHOS_PROCESS_FD_BASE||fd>=CHOS_PROCESS_FD_BASE+(int)CHOS_PROCESS_MAX_OPEN_FILES)
        return -CHOS_ERRNO_EBADF;
    if(length>CHOS_SYS_MAX_READ)return -CHOS_ERRNO_E2BIG;
    if(offset>INT64_MAX)return -CHOS_ERRNO_EINVAL;
    uint64_t flags=chos_spin_lock_irqsave(&process_lock);
    size_t index=find_process_slot_for_space_locked(space),file_index=(size_t)(fd-CHOS_PROCESS_FD_BASE);
    if(index==CHOS_PROCESS_MAX||process_slots[index].open_files[file_index].active!=1){
        chos_spin_unlock_irqrestore(&process_lock,flags);return -CHOS_ERRNO_EBADF;
    }
    process_file_handle_t *file=&process_slots[index].open_files[file_index];
    if(file->busy){chos_spin_unlock_irqrestore(&process_lock,flags);return -CHOS_ERRNO_EBUSY;}
    size_t available=offset<file->size?file->size-(size_t)offset:0;
    size_t amount=length<available?length:available;
    int64_t result=(int64_t)amount;
    if(amount&&!address_space_copy_to_user(space,destination,file->data+(size_t)offset,amount))result=-CHOS_ERRNO_EFAULT;
    chos_spin_unlock_irqrestore(&process_lock,flags);return result;
}
int process_file_info_for_address_space(address_space_handle_t space,int fd,chos_file_info_t *out) {
    if(!out)return -CHOS_ERRNO_EINVAL;
    if(fd<CHOS_PROCESS_FD_BASE||fd>=CHOS_PROCESS_FD_BASE+(int)CHOS_PROCESS_MAX_OPEN_FILES)
        return -CHOS_ERRNO_EBADF;
    uint64_t flags=chos_spin_lock_irqsave(&process_lock);
    size_t index=find_process_slot_for_space_locked(space),file_index=(size_t)(fd-CHOS_PROCESS_FD_BASE);
    if(index==CHOS_PROCESS_MAX||process_slots[index].open_files[file_index].active!=1){
        chos_spin_unlock_irqrestore(&process_lock,flags);return -CHOS_ERRNO_EBADF;
    }
    process_file_handle_t *file=&process_slots[index].open_files[file_index];
    if(file->busy){chos_spin_unlock_irqrestore(&process_lock,flags);return -CHOS_ERRNO_EBUSY;}
    chos_file_info_t value={.version=CHOS_FILE_INFO_VERSION,.size_bytes=sizeof(value),
        .flags=file->writable?(CHOS_FILE_INFO_WORKING_COPY|(file->dirty?CHOS_FILE_INFO_DIRTY:0)):
            (CHOS_FILE_INFO_SNAPSHOT|CHOS_FILE_INFO_READ_ONLY),
        .length=file->size,.position=file->offset};
    if(file->owned_data)value.flags|=CHOS_FILE_INFO_CHFS_SOURCE;
    *out=value;chos_spin_unlock_irqrestore(&process_lock,flags);return 0;
}

int process_file_metadata(address_space_handle_t space,int fd,chos_file_metadata_t *out){
    if(!out)return -CHOS_ERRNO_EINVAL;
    if(fd<CHOS_PROCESS_FD_BASE||fd>=CHOS_PROCESS_FD_BASE+(int)CHOS_PROCESS_MAX_OPEN_FILES)return -CHOS_ERRNO_EBADF;
    uint64_t flags=chos_spin_lock_irqsave(&process_lock);
    size_t index=find_process_slot_for_space_locked(space),i=(size_t)(fd-CHOS_PROCESS_FD_BASE);
    int result=-CHOS_ERRNO_EBADF;
    if(index==CHOS_PROCESS_MAX||process_slots[index].open_files[i].active!=1)goto done;
    process_file_handle_t *file=&process_slots[index].open_files[i];
    if(file->busy){result=-CHOS_ERRNO_EBUSY;goto done;}
    chos_file_metadata_t value={.version=CHOS_FILE_METADATA_VERSION,.size_bytes=sizeof(value),
        .flags=CHOS_FILE_META_REGULAR|CHOS_FILE_META_SIZE_VALID,.length=file->size,
        .identity={.source=file->owned_data?CHOS_FILE_SOURCE_CHFS:CHOS_FILE_SOURCE_ROMFS}};
    if(file->writable)value.flags|=CHOS_FILE_META_WORKING_COPY|(file->dirty?CHOS_FILE_META_DIRTY:0);
    else value.flags|=CHOS_FILE_META_READ_ONLY;
    if(file->lease.object&&(file->owned_data?file->lease.mount!=0:1)){
        value.flags|=CHOS_FILE_META_ID_VALID;value.identity.mount=file->lease.mount;
        value.identity.object=file->lease.object;value.source_revision=file->lease.revision;
    }
    *out=value;result=0;
done:
    chos_spin_unlock_irqrestore(&process_lock,flags);return result;
}
static int embedded_directory_exists(const char *path){
    size_t length=0;while(path[length])++length;
    for(size_t ordinal=0;ordinal<romfs_file_count();ordinal++){
        chos_romfs_file_t candidate;if(!romfs_get_at(ordinal,&candidate))continue;
        size_t i=0;
        for(;i<length;i++){
            int a=(unsigned char)path[i],b=(unsigned char)candidate.path[i];
            if(a>='A'&&a<='Z')a+='a'-'A';if(b>='A'&&b<='Z')b+='a'-'A';
            if(a!=b)break;
        }
        if(i==length&&candidate.path[i]=='/')return 1;
    }
    return 0;
}
int process_path_metadata(const char *path,chos_file_metadata_t *out){
    if(!path||!out)return -CHOS_ERRNO_EINVAL;
    if(!(path[0]=='/'&&!path[1])&&!romfs_path_is_valid(path))return -CHOS_ERRNO_EINVAL;
    int result=storage_file_metadata(path,out);
    if(result!=-CHOS_ERRNO_ENOENT)return result;
    chos_romfs_file_t embedded;
    if(romfs_lookup(path,&embedded)){
        chos_file_metadata_t value={.version=CHOS_FILE_METADATA_VERSION,.size_bytes=sizeof(value),
            .flags=CHOS_FILE_META_REGULAR|CHOS_FILE_META_SIZE_VALID|CHOS_FILE_META_READ_ONLY,
            .identity={.source=CHOS_FILE_SOURCE_ROMFS},.length=embedded.size};
        for(size_t ordinal=0;ordinal<romfs_file_count();ordinal++){
            chos_romfs_file_t candidate;
            if(romfs_get_at(ordinal,&candidate)&&romfs_path_equal(candidate.path,path)){
                value.identity.object=(uint64_t)ordinal+1u;value.flags|=CHOS_FILE_META_ID_VALID;break;
            }
        }
        *out=value;return 0;
    }
    if((path[0]=='/'&&!path[1])||embedded_directory_exists(path)){
        /* The merged/implicit namespace is real but has no physical file ID. */
        *out=(chos_file_metadata_t){.version=CHOS_FILE_METADATA_VERSION,.size_bytes=sizeof(*out),
            .flags=CHOS_FILE_META_DIRECTORY|CHOS_FILE_META_READ_ONLY,
            .identity={.source=CHOS_FILE_SOURCE_NAMESPACE}};
        return 0;
    }
    return -CHOS_ERRNO_ENOENT;
}

int64_t process_file_seek_for_address_space(address_space_handle_t space,int fd,int64_t offset,unsigned origin){
    if(fd<CHOS_PROCESS_FD_BASE || fd>=CHOS_PROCESS_FD_BASE+(int)CHOS_PROCESS_MAX_OPEN_FILES)return -(int64_t)CHOS_ERRNO_EBADF;
    size_t file_index=(size_t)(fd-CHOS_PROCESS_FD_BASE);uint64_t flags=chos_spin_lock_irqsave(&process_lock);
    size_t index=find_process_slot_for_space_locked(space);
    if(index==CHOS_PROCESS_MAX || process_slots[index].open_files[file_index].active!=1){chos_spin_unlock_irqrestore(&process_lock,flags);return -(int64_t)CHOS_ERRNO_EBADF;}
    process_file_handle_t *file=&process_slots[index].open_files[file_index];uint64_t position;
    if(file->busy){chos_spin_unlock_irqrestore(&process_lock,flags);return -CHOS_ERRNO_EBUSY;}
    int result=chos_file_position(file->offset,file->size,offset,origin,&position);
    if(!result)file->offset=(size_t)position;
    chos_spin_unlock_irqrestore(&process_lock,flags);return result?(int64_t)result:(int64_t)position;
}

int64_t process_file_close_for_address_space(address_space_handle_t space, int fd) {
    return process_file_finish(space,fd,1,0);
}

int64_t process_file_open_write(address_space_handle_t space,const char *path,unsigned options,size_t capacity){
    if(!path||!capacity||capacity>CHOS_FILE_WRITE_MAX_BYTES||
       (options&~CHOS_FILE_WRITE_FLAGS)||
       ((options&CHOS_FILE_WRITE_EXCLUSIVE)&&!(options&CHOS_FILE_WRITE_CREATE)))return -CHOS_ERRNO_EINVAL;
    if(path[0]=='/'&&!path[1])return -CHOS_ERRNO_EISDIR;
    if(!romfs_path_is_valid(path))return -CHOS_ERRNO_EINVAL;
    if(chos_network_profile_reserved(path))return -CHOS_ERRNO_EPERM;
    size_t length=0;while(path[length])if(++length>=CHOS_CHFS_PATH_MAX)return -CHOS_ERRNO_EINVAL;
    chos_romfs_file_t embedded;
    if(romfs_lookup(path,&embedded)){
        /* Existing disk shadows may be edited, but deletion between lookup and
         * open must not turn CREATE into permission to shadow immutable ROMFS. */
        chos_chfs_entry_t existing;
        if(!storage_lookup(path,&existing))return -CHOS_ERRNO_EPERM;
        options&=~CHOS_FILE_WRITE_CREATE;
        if(options&CHOS_FILE_WRITE_EXCLUSIVE)return -CHOS_ERRNO_EEXIST;
    }
    size_t index,i;int reserved=reserve_file_buffer(space,capacity,&index,&i);
    if(reserved)return reserved;
    process_slot_t *slot=&process_slots[index];process_file_handle_t *file=&slot->open_files[i];
    void *buffer=kheap_alloc(capacity);size_t bytes=0;chos_storage_lease_t lease;
    int result=buffer?storage_open_snapshot(path,options,buffer,capacity,&bytes,&lease):-CHOS_ERRNO_ENOMEM;
    uint64_t flags=chos_spin_lock_irqsave(&process_lock);
    if(!result&&(!space_equal(slot->space,space)||slot->info.state!=CHOS_PROCESS_RUNNING||slot->info.cleanup_pending))result=-CHOS_ERRNO_ESRCH;
    if(!result){
        file->owned_data=buffer;file->data=buffer;file->size=bytes;file->lease=lease;file->writable=1;
        for(size_t n=0;n<=length;n++)file->path[n]=path[n];
        file->active=1;
    }else zero_bytes(file,sizeof(*file));
    chos_spin_unlock_irqrestore(&process_lock,flags);
    if(result&&buffer)(void)kheap_free(buffer);
    return result?result:(int64_t)(CHOS_PROCESS_FD_BASE+i);
}

int64_t process_file_write(address_space_handle_t space,int fd,uint64_t source,size_t length){
    if(fd<CHOS_PROCESS_FD_BASE||fd>=CHOS_PROCESS_FD_BASE+(int)CHOS_PROCESS_MAX_OPEN_FILES)return -CHOS_ERRNO_EBADF;
    if(length>CHOS_SYS_MAX_WRITE)return -CHOS_ERRNO_E2BIG;
    uint8_t buffer[CHOS_SYS_MAX_WRITE];
    uint64_t flags=chos_spin_lock_irqsave(&process_lock);
    size_t index=find_process_slot_for_space_locked(space),i=(size_t)(fd-CHOS_PROCESS_FD_BASE);
    int64_t result=-CHOS_ERRNO_EBADF;
    if(index==CHOS_PROCESS_MAX||process_slots[index].open_files[i].active!=1)goto done;
    process_file_handle_t *file=&process_slots[index].open_files[i];
    if(!file->writable)goto done;
    if(file->busy){result=-CHOS_ERRNO_EBUSY;goto done;}
    if(!length){result=0;goto done;}
    if(file->offset>file->capacity||length>file->capacity-file->offset){result=-CHOS_ERRNO_ENOSPC;goto done;}
    /* Discard a partial private copy on EFAULT, without changing file bytes,
     * length or cursor. No user pointer survives this bounded lock interval. */
    if(!address_space_copy_from_user(space,buffer,source,length)){result=-CHOS_ERRNO_EFAULT;goto done;}
    if(file->offset>file->size)zero_bytes((uint8_t *)file->owned_data+file->size,file->offset-file->size);
    copy_bytes((uint8_t *)file->owned_data+file->offset,buffer,length);
    file->offset+=length;if(file->offset>file->size)file->size=file->offset;
    file->dirty=1;result=(int64_t)length;
done:
    chos_spin_unlock_irqrestore(&process_lock,flags);return result;
}

int process_file_truncate(address_space_handle_t space,int fd,uint64_t length){
    if(fd<CHOS_PROCESS_FD_BASE||fd>=CHOS_PROCESS_FD_BASE+(int)CHOS_PROCESS_MAX_OPEN_FILES)return -CHOS_ERRNO_EBADF;
    uint64_t flags=chos_spin_lock_irqsave(&process_lock);
    size_t index=find_process_slot_for_space_locked(space),i=(size_t)(fd-CHOS_PROCESS_FD_BASE);
    int result=-CHOS_ERRNO_EBADF;
    if(index==CHOS_PROCESS_MAX||process_slots[index].open_files[i].active!=1)goto done;
    process_file_handle_t *file=&process_slots[index].open_files[i];
    if(!file->writable)goto done;
    if(file->busy){result=-CHOS_ERRNO_EBUSY;goto done;}
    if(length>INT64_MAX){result=-CHOS_ERRNO_EINVAL;goto done;}
    if(length>file->capacity){result=-CHOS_ERRNO_ENOSPC;goto done;}
    if(length>file->size)zero_bytes((uint8_t *)file->owned_data+file->size,(size_t)length-file->size);
    file->size=(size_t)length;file->dirty=1;result=0;
done:
    chos_spin_unlock_irqrestore(&process_lock,flags);return result;
}

int64_t process_file_finish(address_space_handle_t space,int fd,int closing,int discard){
    size_t index, file_index; uint64_t flags; void *owned;
    if (fd < CHOS_PROCESS_FD_BASE || fd >= CHOS_PROCESS_FD_BASE + (int)CHOS_PROCESS_MAX_OPEN_FILES)
        return -(int64_t)CHOS_ERRNO_EBADF;
    file_index = (size_t)(fd - CHOS_PROCESS_FD_BASE);
    flags = chos_spin_lock_irqsave(&process_lock);
    index = find_process_slot_for_space_locked(space);
    if (index == CHOS_PROCESS_MAX || process_slots[index].open_files[file_index].active!=1) {
        chos_spin_unlock_irqrestore(&process_lock, flags);
        return -(int64_t)CHOS_ERRNO_EBADF;
    }
    process_slot_t *slot=&process_slots[index];process_file_handle_t *file=&slot->open_files[file_index];
    if(file->busy){chos_spin_unlock_irqrestore(&process_lock,flags);return -CHOS_ERRNO_EBUSY;}
    owned=file->owned_data;
    if(!discard&&file->writable&&file->dirty){
        file->busy=1;
        /* The reserved busy handle is a reaper fence. No close/read/seek/write
         * can change this private working buffer while storage I/O is unlocked. */
        chos_storage_lease_t updated;
        chos_spin_unlock_irqrestore(&process_lock,flags);
        int result=storage_commit_snapshot(file->path,&file->lease,owned,file->size,&updated);
        flags=chos_spin_lock_irqsave(&process_lock);
        file->busy=0;
        if(result){chos_spin_unlock_irqrestore(&process_lock,flags);return result;}
        file->lease=updated;file->dirty=0;
    }
    if(closing)zero_bytes(file,sizeof(*file));
    chos_spin_unlock_irqrestore(&process_lock, flags);
    if (closing&&owned != NULL && !kheap_free(owned)) return -(int64_t)CHOS_ERRNO_EIO;
    return 0;
}

chos_process_error_t process_run_path(const char *path, chos_process_result_t *result) {
    chos_romfs_file_t file; void *owned = NULL; int resolved; const char *name, *cursor;
    chos_process_error_t status;
    if (path == NULL || result == NULL) return PROCESS_ERR_ARGUMENT;
    if (!romfs_path_is_valid(path)) return PROCESS_ERR_ARGUMENT;
    resolved = resolve_program_file(path, &file, &owned);
    if (resolved != 1) return resolve_file_error(resolved);
    name = path;
    for (cursor = path; *cursor != '\0'; ++cursor) if (*cursor == '/') name = cursor + 1;
    status = process_run_image(name, file.data, file.size, result);
    if (owned != NULL) (void)kheap_free(owned);
    return status;
}

chos_process_error_t process_spawn_path(const char *path, uint32_t *pid_out) {
    chos_romfs_file_t file; void *owned = NULL; int resolved; const char *name, *cursor;
    chos_process_error_t status;
    if (path == NULL || pid_out == NULL) return PROCESS_ERR_ARGUMENT;
    if (!romfs_path_is_valid(path)) return PROCESS_ERR_ARGUMENT;
    resolved = resolve_program_file(path, &file, &owned);
    if (resolved != 1) return resolve_file_error(resolved);
    name = path;
    for (cursor = path; *cursor != '\0'; ++cursor) if (*cursor == '/') name = cursor + 1;
    status = process_spawn_image(name, file.data, file.size, pid_out);
    if (owned != NULL) (void)kheap_free(owned);
    return status;
}

static chos_process_error_t process_spawn_path_args_policy(const char *path, const char *arguments, uint32_t *pid_out,uint64_t policy) {
    chos_romfs_file_t file;
    const char *base, *cursor;
    const char *argv[CHOS_PROCESS_MAX_ARGS];
    char storage[CHOS_PROCESS_MAX_ARGS - 1u][CHOS_PROCESS_ARG_MAX_LENGTH];
    size_t argc = 1u, i;
    void *owned = NULL;
    int resolved;
    chos_process_error_t spawn_result;
    if (path == NULL || pid_out == NULL || !romfs_path_is_valid(path)) return PROCESS_ERR_ARGUMENT;
    base = path;
    for (cursor = path; *cursor != '\0'; ++cursor) if (*cursor == '/') base = cursor + 1;
    argv[0] = base;
    cursor = arguments;
    while (cursor != NULL && *cursor != '\0') {
        size_t length = 0u;
        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        if (*cursor == '\0') break;
        if (argc >= CHOS_PROCESS_MAX_ARGS) return PROCESS_ERR_ARGUMENT;
        while (cursor[length] != '\0' && cursor[length] != ' ' && cursor[length] != '\t') {
            if (length + 1u >= CHOS_PROCESS_ARG_MAX_LENGTH) return PROCESS_ERR_ARGUMENT;
            storage[argc - 1u][length] = cursor[length];
            ++length;
        }
        if (length == 0u) break;
        storage[argc - 1u][length] = '\0';
        argv[argc] = storage[argc - 1u];
        ++argc;
        cursor += length;
    }
    for (i = 0u; i < argc; ++i) if (argv[i] == NULL) return PROCESS_ERR_ARGUMENT;
    resolved = resolve_program_file(path, &file, &owned);
    if (resolved != 1) return resolve_file_error(resolved);
    spawn_result = process_spawn_image_args_policy(base, file.data, file.size, argv, argc, pid_out,policy);
    if (owned != NULL) (void)kheap_free(owned);
    return spawn_result;
}

chos_process_error_t process_spawn_path_args(const char *path,const char *args,uint32_t *pid){
 return process_spawn_path_args_policy(path,args,pid,CHOS_PROCESS_CAP_ALL);
}

static size_t bounded_string_length(const char *text, size_t capacity) {
    size_t n = 0u;
    if (text == NULL) return capacity;
    while (n < capacity && text[n] != '\0') ++n;
    return n;
}

uint32_t process_spawn_request_enqueue_policy(const char *path, const char *arguments, int with_arguments,uint64_t policy) {
    size_t i, path_len, args_len = 0u;
    uint64_t flags;
    uint32_t ticket = 0u;
    if (!scheduler_is_running() || !romfs_path_is_valid(path) ||
        (policy&~CHOS_PROCESS_CAP_ALL) || (policy&CHOS_PROCESS_CAP_REQUIRED)!=CHOS_PROCESS_CAP_REQUIRED) return 0u;
    path_len = bounded_string_length(path, PROCESS_SPAWN_REQUEST_PATH_CAPACITY);
    if (path_len == 0u || path_len >= PROCESS_SPAWN_REQUEST_PATH_CAPACITY) return 0u;
    if (with_arguments) {
        args_len = bounded_string_length(arguments, PROCESS_SPAWN_REQUEST_ARGS_CAPACITY);
        if (arguments == NULL || args_len >= PROCESS_SPAWN_REQUEST_ARGS_CAPACITY) return 0u;
    }
    flags = chos_spin_lock_irqsave(&process_lock);
    for (i = 0u; i < PROCESS_SPAWN_REQUEST_CAPACITY; ++i) {
        if (spawn_requests[i].state == SPAWN_REQUEST_FREE) break;
    }
    if (i < PROCESS_SPAWN_REQUEST_CAPACITY) {
        process_spawn_request_t *request = &spawn_requests[i];
        zero_bytes(request, sizeof(*request));
        ticket = next_spawn_request_ticket++;
        if (ticket == 0u) ticket = next_spawn_request_ticket++;
        request->ticket = ticket;
        request->allowed_syscalls=policy;
        request->with_arguments = with_arguments ? 1u : 0u;
        copy_bytes((uint8_t *)request->path, (const uint8_t *)path, path_len);
        request->path[path_len] = '\0';
        if (with_arguments) {
            copy_bytes((uint8_t *)request->arguments, (const uint8_t *)arguments, args_len);
            request->arguments[args_len] = '\0';
        }
        request->state = SPAWN_REQUEST_PENDING;
    }
    chos_spin_unlock_irqrestore(&process_lock, flags);
    return ticket;
}

uint32_t process_spawn_request_enqueue(const char *path,const char *args,int with_args){
 address_space_handle_t space;uint64_t policy=CHOS_PROCESS_CAP_ALL;
 if(user_mode_get_address_space(&space) && !process_get_capabilities(space,&policy))return 0;
 return process_spawn_request_enqueue_policy(path,args,with_args,policy);
}

int process_spawn_request_status(uint32_t ticket, chos_process_error_t *error_out, uint32_t *pid_out) {
    size_t i;
    uint64_t flags;
    int result = -1;
    if (ticket == 0u || error_out == NULL || pid_out == NULL) return -1;
    flags = chos_spin_lock_irqsave(&process_lock);
    for (i = 0u; i < PROCESS_SPAWN_REQUEST_CAPACITY; ++i) {
        process_spawn_request_t *request = &spawn_requests[i];
        if (request->ticket != ticket || request->state == SPAWN_REQUEST_FREE) continue;
        if (request->state == SPAWN_REQUEST_DONE) {
            *error_out = request->error;
            *pid_out = request->pid;
            result = 1;
        } else result = 0;
        break;
    }
    chos_spin_unlock_irqrestore(&process_lock, flags);
    return result;
}

int process_spawn_request_cancel(uint32_t ticket) {
    size_t i;
    uint64_t flags;
    int cancelled = 0;
    flags = chos_spin_lock_irqsave(&process_lock);
    for (i = 0u; i < PROCESS_SPAWN_REQUEST_CAPACITY; ++i) {
        process_spawn_request_t *request = &spawn_requests[i];
        if (request->ticket != ticket || request->state == SPAWN_REQUEST_FREE) continue;
        if (request->state == SPAWN_REQUEST_PENDING || request->state == SPAWN_REQUEST_DONE) {
            zero_bytes(request, sizeof(*request));
            cancelled = 1;
        }
        break;
    }
    chos_spin_unlock_irqrestore(&process_lock, flags);
    return cancelled;
}

void process_spawn_request_release(uint32_t ticket) {
    size_t i;
    uint64_t flags = chos_spin_lock_irqsave(&process_lock);
    for (i = 0u; i < PROCESS_SPAWN_REQUEST_CAPACITY; ++i) {
        process_spawn_request_t *request = &spawn_requests[i];
        if (request->ticket == ticket && request->state == SPAWN_REQUEST_DONE) {
            zero_bytes(request, sizeof(*request));
            break;
        }
    }
    chos_spin_unlock_irqrestore(&process_lock, flags);
}

int process_service_pending_spawn_request(void) {
    size_t i;
    uint64_t flags;
    uint32_t ticket;
    uint64_t policy;
    int with_arguments;
    char path[PROCESS_SPAWN_REQUEST_PATH_CAPACITY];
    char arguments[PROCESS_SPAWN_REQUEST_ARGS_CAPACITY];
    uint32_t pid = 0u;
    chos_process_error_t error;
    if (!scheduler_current_is_kernel_task() ||
        vmm_current_root_table_address() != vmm_kernel_root_table_address()) return 0;
    flags = chos_spin_lock_irqsave(&process_lock);
    for (i = 0u; i < PROCESS_SPAWN_REQUEST_CAPACITY; ++i) {
        if (spawn_requests[i].state == SPAWN_REQUEST_PENDING) break;
    }
    if (i == PROCESS_SPAWN_REQUEST_CAPACITY) {
        chos_spin_unlock_irqrestore(&process_lock, flags);
        return 0;
    }
    spawn_requests[i].state = SPAWN_REQUEST_RUNNING;
    ticket = spawn_requests[i].ticket;
    policy=spawn_requests[i].allowed_syscalls;
    with_arguments = spawn_requests[i].with_arguments != 0u;
    copy_bytes((uint8_t *)path, (const uint8_t *)spawn_requests[i].path, sizeof(path));
    copy_bytes((uint8_t *)arguments, (const uint8_t *)spawn_requests[i].arguments, sizeof(arguments));
    chos_spin_unlock_irqrestore(&process_lock, flags);

    error = process_spawn_path_args_policy(path,with_arguments?arguments:NULL,&pid,policy);

    flags = chos_spin_lock_irqsave(&process_lock);
    for (i = 0u; i < PROCESS_SPAWN_REQUEST_CAPACITY; ++i) {
        process_spawn_request_t *request = &spawn_requests[i];
        if (request->ticket == ticket && request->state == SPAWN_REQUEST_RUNNING) {
            request->error = error;
            request->pid = pid;
            request->state = SPAWN_REQUEST_DONE;
            break;
        }
    }
    chos_spin_unlock_irqrestore(&process_lock, flags);
    return 1;
}

int process_async_self_test(void) {
    pmm_stats_t pmm_before, pmm_after;
    scheduler_stats_t sched_before, sched_after;
    chos_process_stats_t proc_before, proc_after;
    chos_process_info_t async_info = {0}, hello_info = {0}, info;
    size_t i, spaces_before;
    uint32_t async_pid = 0u, hello_pid = 0u;
    int ok = 0;

    if (scheduler_is_running() || !timer_is_ready() || user_mode_is_active() ||
        vmm_current_root_table_address() != vmm_kernel_root_table_address()) {
        uart_puts("[FAIL] async-test requires the boot shell, timer and canonical kernel root\n");
        return 0;
    }
    process_reap_pending();
    pmm_get_stats(&pmm_before);
    scheduler_get_stats(&sched_before);
    process_get_stats(&proc_before);
    spaces_before = address_space_count();

    uart_puts("Queueing /BIN/ASYNC.CHX and /BIN/HELLO.CHX under the same scheduler runtime.\n");
    if (process_spawn_path("/BIN/ASYNC.CHX", &async_pid) != PROCESS_OK ||
        process_spawn_path("/BIN/HELLO.CHX", &hello_pid) != PROCESS_OK ||
        async_pid == 0u || hello_pid == 0u || async_pid == hello_pid) goto done;

    if (!scheduler_set_preemption_enabled(1) || !scheduler_run()) {
        (void)scheduler_set_preemption_enabled(0);
        goto done;
    }
    (void)scheduler_set_preemption_enabled(0);
    process_reap_pending();

    for (i = 0u; i < CHOS_PROCESS_MAX; ++i) {
        if (!process_get_info_at(i, &info)) continue;
        if (info.pid == async_pid) async_info = info;
        if (info.pid == hello_pid) hello_info = info;
    }
    pmm_get_stats(&pmm_after);
    scheduler_get_stats(&sched_after);
    process_get_stats(&proc_after);
    ok = async_info.pid == async_pid && hello_info.pid == hello_pid &&
         async_info.state == CHOS_PROCESS_EXITED && async_info.exit_status == 0 &&
         hello_info.state == CHOS_PROCESS_EXITED && hello_info.exit_status == 0 &&
         !async_info.cleanup_pending && !hello_info.cleanup_pending &&
         proc_after.started == proc_before.started + 2u &&
         proc_after.exited == proc_before.exited + 2u &&
         proc_after.failed == proc_before.failed &&
         /* This scenario validates sleep/yield-driven asynchronous scheduling.
          * A tick can wake a sleeping task without forcibly preempting a running
          * task, so preemption_switches is not required to increase here.
          * CPU-bound timer preemption is tested by process-preempt-test. */
         sched_after.context_switches > sched_before.context_switches &&
         sched_after.sleep_calls > sched_before.sleep_calls &&
         sched_after.yield_calls > sched_before.yield_calls &&
         sched_after.wakeups > sched_before.wakeups &&
         pmm_after.free_pages == pmm_before.free_pages &&
         pmm_after.allocated_pages == pmm_before.allocated_pages &&
         address_space_count() == spaces_before &&
         vmm_current_root_table_address() == vmm_kernel_root_table_address();

done:
    if (ok) {
        uart_puts("[ OK ] async launch, GETPID/WRITE/YIELD/SLEEP/EXIT runtime calls and cleanup baseline passed\n");
    } else {
        chos_process_table_stats_t table_after;
        /* A partial spawn failure can leave the first task queued with an address-
         * space reference. Drain queued work when safe so the diagnostic itself
         * does not leak tasks or process mappings on its failure path. */
        if (!scheduler_is_running() && scheduler_get_task_count() != 0u &&
            scheduler_set_preemption_enabled(1)) {
            (void)scheduler_run();
            (void)scheduler_set_preemption_enabled(0);
        } else {
            (void)scheduler_set_preemption_enabled(0);
        }
        process_reap_pending();
        process_get_table_stats(&table_after);
        scheduler_get_stats(&sched_after);
        pmm_get_stats(&pmm_after);
        process_get_stats(&proc_after);
        uart_puts("[DIAG] async-test counters: started +"); put_unsigned64((size_t)(proc_after.started - proc_before.started));
        uart_puts(" exited +"); put_unsigned64((size_t)(proc_after.exited - proc_before.exited));
        uart_puts(" failed +"); put_unsigned64((size_t)(proc_after.failed - proc_before.failed)); uart_puts("\n");
        uart_puts("[DIAG] scheduler: context switches +"); put_unsigned64((size_t)(sched_after.context_switches - sched_before.context_switches));
        uart_puts(" sleep calls +"); put_unsigned64((size_t)(sched_after.sleep_calls - sched_before.sleep_calls));
        uart_puts(" yield calls +"); put_unsigned64((size_t)(sched_after.yield_calls - sched_before.yield_calls));
        uart_puts(" preemptions +"); put_unsigned64((size_t)(sched_after.preemption_switches - sched_before.preemption_switches));
        uart_puts(" wakeups +"); put_unsigned64((size_t)(sched_after.wakeups - sched_before.wakeups));
        uart_puts(" pending reaps="); put_unsigned64(sched_after.pending_reaps); uart_puts("\n");
        uart_puts("[DIAG] cleanup pending="); put_unsigned64(table_after.pending_cleanup);
        uart_puts(" address spaces="); put_unsigned64(address_space_count());
        uart_puts(" free-pages delta=");
        if (pmm_after.free_pages >= pmm_before.free_pages) put_unsigned64(pmm_after.free_pages - pmm_before.free_pages);
        else { uart_putc('-'); put_unsigned64(pmm_before.free_pages - pmm_after.free_pages); }
        uart_puts(" allocated-pages delta=");
        if (pmm_after.allocated_pages >= pmm_before.allocated_pages) put_unsigned64(pmm_after.allocated_pages - pmm_before.allocated_pages);
        else { uart_putc('-'); put_unsigned64(pmm_before.allocated_pages - pmm_after.allocated_pages); }
        uart_puts("\n[FAIL] async launch/runtime syscalls or process cleanup did not pass\n");
    }
    return ok;
}

int process_get_capabilities(address_space_handle_t space,uint64_t *out){
 if(!out)return 0;
 uint64_t f=chos_spin_lock_irqsave(&process_lock);int found=0;
 for(unsigned i=0;i<CHOS_PROCESS_MAX;i++){
  process_slot_t *s=&process_slots[i];
  if(s->address_space_live && s->info.state==CHOS_PROCESS_RUNNING && s->space.slot==space.slot &&
     s->space.generation==space.generation && s->space.id==space.id){*out=s->allowed_syscalls;found=1;break;}
 }
 chos_spin_unlock_irqrestore(&process_lock,f);return found;
}

uint32_t process_pid_for_address_space(address_space_handle_t space) {
    size_t i;
    uint32_t pid = 0u;
    uint64_t flags = chos_spin_lock_irqsave(&process_lock);
    for (i = 0u; i < CHOS_PROCESS_MAX; ++i) {
        const process_slot_t *slot = &process_slots[i];
        if (slot->address_space_live && slot->info.state == CHOS_PROCESS_RUNNING &&
            slot->space.slot == space.slot && slot->space.generation == space.generation &&
            slot->space.id == space.id) {
            pid = slot->info.pid;
            break;
        }
    }
    chos_spin_unlock_irqrestore(&process_lock, flags);
    return pid;
}

chos_process_error_t process_run_demo(void) {
    chos_process_result_t result;
    chos_process_error_t error = process_run_path("/BIN/HELLO.CHX", &result);
    if (error == PROCESS_OK) {
        uint64_t flags = uart_write_begin();
        uart_puts_locked("[PROCESS] pid=");
        uart_put_u64_locked(result.pid);
        uart_puts_locked(" exited status=");
        put_signed64_locked(result.exit_status);
        uart_puts_locked("\n");
        uart_write_end(flags);
        if (!result.completed || result.exit_status != 0) return PROCESS_ERR_EXECUTION;
    }
    return error;
}

chos_process_error_t process_self_test(void) {
    pmm_stats_t pmm_before, pmm_after;
    size_t spaces_before = address_space_count();
    uint64_t writes_before = user_mode_write_syscalls();
    uint64_t rejects_before = user_mode_rejected_writes();
    uint8_t invalid[CHOS_EXEC_HEADER_SIZE] = {0};
    chos_process_error_t error;
    chos_process_result_t invalid_result;
    if (!vmm_is_enabled() || scheduler_is_running() || user_mode_is_active() ||
        vmm_current_root_table_address() != vmm_kernel_root_table_address()) return PROCESS_ERR_CONTEXT;
    pmm_get_stats(&pmm_before);
    if (process_image_validate(invalid, sizeof(invalid), &(chos_exec_image_info_t){0}) != 0 ||
        process_run_image("bad-image", invalid, sizeof(invalid), &invalid_result) != PROCESS_ERR_FORMAT)
        return PROCESS_ERR_FORMAT;
    error = process_run_demo();
    pmm_get_stats(&pmm_after);
    if (error != PROCESS_OK || address_space_count() != spaces_before ||
        pmm_before.free_pages != pmm_after.free_pages ||
        pmm_before.allocated_pages != pmm_after.allocated_pages ||
        user_mode_write_syscalls() != writes_before + 1u ||
        user_mode_rejected_writes() != rejects_before + 1u ||
        user_mode_is_active() || vmm_current_root_table_address() != vmm_kernel_root_table_address())
        return PROCESS_ERR_CLEANUP;
    return PROCESS_OK;
}

void process_record_user_fault(uint64_t esr, uint64_t elr, uint64_t far) {
    uint32_t current_task_id = 0u;
    size_t current_index = CHOS_SCHED_NO_INDEX;
    size_t i;
    uint64_t flags;
    address_space_handle_t fault_space={0};int has_space=user_mode_get_address_space(&fault_space);
    if (scheduler_is_running()) {
        scheduler_task_info_t task_info;
        current_index = scheduler_current_task_index();
        if (current_index != CHOS_SCHED_NO_INDEX &&
            scheduler_get_task_info(current_index, &task_info)) current_task_id = task_info.id;
    }
    flags = chos_spin_lock_irqsave(&process_lock);
    for (i = 0u; i < CHOS_PROCESS_MAX; ++i) {
        process_slot_t *slot = &process_slots[i];
        int match = 0;
        if (slot->info.state != CHOS_PROCESS_RUNNING) continue;
        if (has_space) match=space_equal(slot->space,fault_space);
        else if (current_task_id != 0u) match = slot->info.scheduler_task_id == current_task_id;
        else match = foreground_process_slot == i;
        if (match) {
            slot->info.fault_esr = esr;
            slot->info.fault_elr = elr;
            slot->info.fault_far = far;
            break;
        }
    }
    chos_spin_unlock_irqrestore(&process_lock, flags);
}

int process_romfs_self_test(void) {
    pmm_stats_t pmm_before, pmm_after;
    chos_process_result_t hello = {0}, about = {0}, fault = {0};
    chos_process_info_t info;
    chos_process_stats_t stats_before, stats_after;
    size_t spaces_before, i;
    int fault_recorded = 0;
    int ok;
    if (scheduler_is_running() || user_mode_is_active() ||
        vmm_current_root_table_address() != vmm_kernel_root_table_address()) {
        uart_puts("[FAIL] romfs-test requires idle scheduler and canonical kernel root\n");
        return 0;
    }
    process_reap_pending();
    if (!romfs_validate_all() || romfs_file_count() < 3u) {
        uart_puts("[FAIL] packaged read-only ROMFS failed CHCX validation\n");
        return 0;
    }
    { chos_romfs_file_t missing; if (romfs_lookup("/BIN/NOPE.CHX", &missing)) return 0; }
    spaces_before = address_space_count();
    pmm_get_stats(&pmm_before);
    process_get_stats(&stats_before);
    uart_puts("ROMFS: running two distinct packaged CHCX programs through process_run_path().\n");
    if (process_run_path("/bin/hello.chx", &hello) != PROCESS_OK ||
        !hello.completed || hello.exit_status != 0 ||
        process_run_path("/BIN/ABOUT.CHX", &about) != PROCESS_OK ||
        !about.completed || about.exit_status != 0) {
        uart_puts("[FAIL] a packaged ROMFS program did not exit successfully\n");
        return 0;
    }
    uart_puts("ROMFS fault-isolation probe: the next packaged program deliberately reads an unmapped address.\n");
    if (process_run_path("/BIN/FAULT.CHX", &fault) != PROCESS_ERR_EXECUTION ||
        !fault.completed || fault.pid == 0u) return 0;
    process_reap_pending();
    for (i = 0u; i < CHOS_PROCESS_MAX; ++i) {
        if (process_get_info_at(i, &info) && info.pid == fault.pid &&
            info.state == CHOS_PROCESS_FAILED && !info.cleanup_pending &&
            info.fault_esr != 0u && info.fault_elr != 0u && info.fault_far == 0x1000u)
            fault_recorded = 1;
    }
    pmm_get_stats(&pmm_after);
    process_get_stats(&stats_after);
    ok = fault_recorded && spaces_before == address_space_count() &&
         pmm_before.free_pages == pmm_after.free_pages &&
         pmm_before.allocated_pages == pmm_after.allocated_pages &&
         stats_after.started == stats_before.started + 3u &&
         stats_after.exited == stats_before.exited + 2u &&
         stats_after.failed == stats_before.failed + 1u &&
         vmm_current_root_table_address() == vmm_kernel_root_table_address();
    if (ok) uart_puts("[ OK ] packaged CHCX lookup, common loader, distinct applications, user-fault diagnostics and cleanup passed\n");
    else uart_puts("[FAIL] ROMFS loader, fault record or cleanup baseline failed\n");
    return ok;
}

void process_get_stats(chos_process_stats_t *out) {
    uint64_t flags;
    if (out == NULL) return;
    flags = chos_spin_lock_irqsave(&process_lock);
    out->started = processes_started;
    out->exited = processes_exited;
    out->failed = processes_failed;
    chos_spin_unlock_irqrestore(&process_lock, flags);
}

void process_get_table_stats(chos_process_table_stats_t *out) {
    size_t i;
    uint64_t flags;
    if (out == NULL) return;
    zero_bytes(out, sizeof(*out));
    flags = chos_spin_lock_irqsave(&process_lock);
    for (i = 0u; i < CHOS_PROCESS_MAX; ++i) {
        const process_slot_t *slot = &process_slots[i];
        if (slot->info.state == CHOS_PROCESS_EMPTY) continue;
        ++out->history_records;
        if (slot->info.state == CHOS_PROCESS_LOADING || slot->info.state == CHOS_PROCESS_QUEUED ||
            slot->info.state == CHOS_PROCESS_RUNNING)
            ++out->active_processes;
        if (slot->info.cleanup_pending) ++out->pending_cleanup;
        if (state_reusable(slot)) ++out->reusable_terminal_records;
    }
    chos_spin_unlock_irqrestore(&process_lock, flags);
}

int process_slot_reuse_test(void) {
    enum { RUNS = CHOS_PROCESS_MAX + 2u };
    static uint8_t image[CHOS_EXEC_HEADER_SIZE + PROCESS_PAGE_SIZE]
        __attribute__((aligned(16)));
    uint32_t pids[RUNS];
    size_t image_size = (size_t)(chos_user_exit_end - chos_user_exit_start);
    size_t i, j, recorded = 0u;
    int ok = 1;
    pmm_stats_t pmm_before, pmm_after;
    chos_process_stats_t proc_before, proc_after;
    chos_process_table_stats_t table;
    chos_process_info_t info;
    size_t spaces_before;

    if (scheduler_is_running() || user_mode_is_active() ||
        vmm_current_root_table_address() != vmm_kernel_root_table_address() ||
        image_size == 0u || image_size > PROCESS_PAGE_SIZE) return 0;
    process_reap_pending();
    pmm_get_stats(&pmm_before);
    process_get_stats(&proc_before);
    spaces_before = address_space_count();
    if (!process_image_write_header(image, sizeof(image), (uint32_t)image_size, 0u, 1u))
        return 0;
    copy_bytes(image + CHOS_EXEC_HEADER_SIZE, chos_user_exit_start, image_size);

    uart_puts("Running ");
    { uint64_t flags = uart_write_begin(); uart_put_u64_locked(RUNS); uart_puts_locked(" short EL0 exits through the bounded process table.\n"); uart_write_end(flags); }
    for (i = 0u; i < RUNS; ++i) {
        chos_process_result_t result;
        if (process_run_image("slot-reuse", image, CHOS_EXEC_HEADER_SIZE + image_size, &result) != PROCESS_OK ||
            !result.completed || result.exit_status != 0 || result.pid == 0u) {
            ok = 0;
            break;
        }
        pids[i] = result.pid;
        for (j = 0u; j < i; ++j) if (pids[j] == pids[i]) ok = 0;
        if (!ok) break;
    }

    process_reap_pending();
    process_get_stats(&proc_after);
    pmm_get_stats(&pmm_after);
    process_get_table_stats(&table);
    if (i != RUNS || proc_after.started != proc_before.started + RUNS ||
        proc_after.exited != proc_before.exited + RUNS || proc_after.failed != proc_before.failed ||
        pmm_after.free_pages != pmm_before.free_pages ||
        pmm_after.allocated_pages != pmm_before.allocated_pages ||
        address_space_count() != spaces_before || table.active_processes != 0u ||
        table.pending_cleanup != 0u || table.reusable_terminal_records != CHOS_PROCESS_MAX ||
        table.history_records != CHOS_PROCESS_MAX) ok = 0;

    /* More runs than slots must leave only the newest records in the table. */
    if (i == RUNS) for (j = 0u; j < CHOS_PROCESS_MAX; ++j) {
        size_t k;
        int found = 0;
        uint32_t expected_pid = pids[RUNS - CHOS_PROCESS_MAX + j];
        for (k = 0u; k < CHOS_PROCESS_MAX; ++k) {
            if (process_get_info_at(k, &info) && info.pid == expected_pid &&
                info.state == CHOS_PROCESS_EXITED && info.exit_status == 0 &&
                !info.cleanup_pending) found = 1;
        }
        if (!found) ok = 0;
    }
    for (j = 0u; j < CHOS_PROCESS_MAX; ++j) {
        if (process_get_info_at(j, &info)) ++recorded;
    }
    if (recorded != CHOS_PROCESS_MAX) ok = 0;

    if (ok) uart_puts("[ OK ] process slots recycled; unique PIDs, clean EL0 exits, bounded history and PMM baseline passed\n");
    else uart_puts("[FAIL] process-slot reuse, exit status, cleanup or memory baseline check failed\n");
    return ok;
}

size_t process_count(void) {
    size_t i, count = 0u;
    uint64_t flags = chos_spin_lock_irqsave(&process_lock);
    for (i = 0u; i < CHOS_PROCESS_MAX; ++i)
        if (process_slots[i].info.state != CHOS_PROCESS_EMPTY) ++count;
    chos_spin_unlock_irqrestore(&process_lock, flags);
    return count;
}

int process_get_info_at(size_t index, chos_process_info_t *out) {
    uint64_t flags;
    if (index >= CHOS_PROCESS_MAX || out == NULL) return 0;
    flags = chos_spin_lock_irqsave(&process_lock);
    if (process_slots[index].info.state == CHOS_PROCESS_EMPTY) {
        chos_spin_unlock_irqrestore(&process_lock, flags);
        return 0;
    }
    out->pid = process_slots[index].info.pid;
    out->state = process_slots[index].info.state;
    out->exit_status = process_slots[index].info.exit_status;
    out->address_space_id = process_slots[index].info.address_space_id;
    out->image_pages = process_slots[index].info.image_pages;
    out->stack_pages = process_slots[index].info.stack_pages;
    out->entry_address = process_slots[index].info.entry_address;
    out->user_stack_top = process_slots[index].info.user_stack_top;
    out->scheduler_task_id = process_slots[index].info.scheduler_task_id;
    out->cleanup_pending = process_slots[index].info.cleanup_pending;
    out->fault_esr = process_slots[index].info.fault_esr;
    out->fault_elr = process_slots[index].info.fault_elr;
    out->fault_far = process_slots[index].info.fault_far;
    { size_t i; for (i = 0u; i < CHOS_PROCESS_NAME_CAPACITY; ++i)
          out->name[i] = process_slots[index].info.name[i]; }
    chos_spin_unlock_irqrestore(&process_lock, flags);
    return 1;
}


int process_get_info_by_pid(uint32_t pid, chos_process_info_t *out) {
    size_t i;
    if (pid == 0u || out == NULL) return 0;
    for (i = 0u; i < CHOS_PROCESS_MAX; ++i) {
        chos_process_info_t candidate;
        if (process_get_info_at(i, &candidate) && candidate.pid == pid) {
            *out = candidate;
            return 1;
        }
    }
    return 0;
}

int process_query_pid(uint32_t pid, chos_process_state_t *state_out, int64_t *status_out) {
    size_t i;
    int result = -1;
    uint64_t flags;
    if (pid == 0u) return -1;
    flags = chos_spin_lock_irqsave(&process_lock);
    for (i = 0u; i < CHOS_PROCESS_MAX; ++i) {
        const chos_process_info_t *info = &process_slots[i].info;
        if (info->state == CHOS_PROCESS_EMPTY || info->pid != pid) continue;
        if (state_out != NULL) *state_out = info->state;
        if (status_out != NULL) *status_out = info->exit_status;
        result = (info->state == CHOS_PROCESS_EXITED || info->state == CHOS_PROCESS_FAILED) ? 0 : 1;
        break;
    }
    chos_spin_unlock_irqrestore(&process_lock, flags);
    return result;
}

const char *process_state_name(chos_process_state_t state) {
    switch (state) {
        case CHOS_PROCESS_EMPTY: return "empty";
        case CHOS_PROCESS_LOADING: return "loading";
        case CHOS_PROCESS_QUEUED: return "queued";
        case CHOS_PROCESS_RUNNING: return "running";
        case CHOS_PROCESS_EXITED: return "exited";
        case CHOS_PROCESS_FAILED: return "failed";
        default: return "unknown";
    }
}

const char *process_error_string(chos_process_error_t error) {
    switch (error) {
        case PROCESS_OK: return "success";
        case PROCESS_ERR_ARGUMENT: return "invalid argument";
        case PROCESS_ERR_FORMAT: return "invalid CHOS executable format";
        case PROCESS_ERR_CONTEXT: return "requires a timer and canonical kernel-root task context";
        case PROCESS_ERR_CAPACITY: return "process table full or cleanup pending";
        case PROCESS_ERR_ADDRESS_SPACE: return "could not create process address space";
        case PROCESS_ERR_OUT_OF_MEMORY: return "could not allocate user pages";
        case PROCESS_ERR_MAPPING: return "could not map user image/stack";
        case PROCESS_ERR_EXECUTION: return "could not enter or resume EL0";
        case PROCESS_ERR_CLEANUP: return "process cleanup did not complete";
        case PROCESS_ERR_NOT_FOUND: return "file not found in storage or ROMFS";
        case PROCESS_ERR_ROMFS_CORRUPT: return "packaged ROMFS image failed validation";
        case PROCESS_ERR_STORAGE_IO: return "storage file read or checksum failure";
        default: return "unknown process error";
    }
}

#include "chos/ipc.h"
#include "chos/clock.h"
#include <stddef.h>
#include <stdint.h>
#include "chos/atomic.h"
#include "chos/address_space.h"
#include "chos/exception.h"
#include "chos/irq_context.h"
#include "chos/syscall.h"
#include "chos/network_policy.h"
#include "chos/net_manager.h"
#include "chos/net6.h"
#include "chos/user_memory.h"
#include "chos/uart.h"
#include "chos/user_mode.h"
#include "chos/process.h"
#include "chos/scheduler.h"
#include "chos/gui.h"
#include "chos/gui_protocol.h"
#include "chos/file_api.h"
#include "chos/storage.h"
#include "chos/chfs.h"
#include "chos/romfs.h"
#include "chos/timer.h"
#include "chos/kheap.h"
#include "chos/net_tcp.h"
#include "chos/net_stack.h"
#include "chos/net_dns_api.h"
#include "chos/entropy.h"

static volatile uint64_t syscall_write_count;
static volatile uint64_t syscall_rejected_write_count;

static uint64_t neg_errno(int error) {
    return (uint64_t)(int64_t)-error;
}

static uint64_t read_daif(void) {
    uint64_t value;
    __asm__ volatile("mrs %0, daif" : "=r"(value));
    return value;
}

static void restore_daif(uint64_t value) {
    __asm__ volatile("msr daif, %0\n\tisb" :: "r"(value) : "memory");
}

static void print_hex64(uint64_t value) {
    static const char digits[] = "0123456789ABCDEF";
    int shift;
    uart_puts("0x");
    for (shift = 60; shift >= 0; shift -= 4) {
        uart_putc(digits[(value >> (unsigned)shift) & 0x0fu]);
    }
}

static uint64_t read_far_el1(void) {
    uint64_t value;
    __asm__ volatile("mrs %0, far_el1" : "=r"(value));
    return value;
}

uint64_t user_mode_write_syscalls(void) {
    return chos_atomic_load_u64(&syscall_write_count);
}

uint64_t user_mode_rejected_writes(void) {
    return chos_atomic_load_u64(&syscall_rejected_write_count);
}

static void syscall_write(chos_irq_context_t *frame) {
    address_space_handle_t space;
    uint64_t user_pointer = frame->x[0];
    uint64_t length64 = frame->x[1];
    uint8_t buffer[CHOS_SYS_MAX_WRITE];
    size_t length, i;
    uint64_t flags;

    if (length64 > CHOS_SYS_MAX_WRITE) {
        frame->x[0] = neg_errno(CHOS_ERRNO_E2BIG);
        return;
    }
    length = (size_t)length64;
    if (!user_mode_get_address_space(&space) ||
        !address_space_copy_from_user(space, buffer, user_pointer, length)) {
        (void)chos_atomic_fetch_add_u64(&syscall_rejected_write_count, 1u);
        frame->x[0] = neg_errno(CHOS_ERRNO_EFAULT);
        return;
    }

    if (chos_gui_console_output_enabled()) {
        chos_gui_console_write((const char *)buffer, length);
    } else {
        flags = uart_write_begin();
        for (i = 0u; i < length; ++i) uart_putc_locked((char)buffer[i]);
        uart_write_end(flags);
    }
    (void)chos_atomic_fetch_add_u64(&syscall_write_count, 1u);
    frame->x[0] = (uint64_t)length;
}


static int syscall_copy_user_path(address_space_handle_t space, uint64_t user_pointer,
                                  char *path, size_t capacity) {
    size_t i;
    if (path == NULL || capacity == 0u) return 0;
    for (i = 0u; i < capacity; ++i) {
        char ch;
        if (user_pointer > UINT64_MAX - i ||
            !address_space_copy_from_user(space, &ch, user_pointer + i, 1u)) return 0;
        path[i] = ch;
        if (ch == '\0') return i > 0u;
    }
    path[capacity - 1u] = '\0';
    return 0;
}

static int syscall_copy_user_optional_args(address_space_handle_t space,uint64_t pointer,char *out,size_t capacity){
 char first;
 if(!capacity || !address_space_copy_from_user(space,&first,pointer,1u))return 0;
 if(!first){out[0]=0;return 1;}
 return syscall_copy_user_path(space,pointer,out,capacity);
}

static int syscall_user_range_writable(address_space_handle_t space, uint64_t user_pointer,
                                        size_t length) {
    size_t checked = 0u;
    if (length == 0u) return 1;
    if (user_pointer > UINT64_MAX - (uint64_t)length) return 0;
    while (checked < length) {
        uint64_t va = user_pointer + (uint64_t)checked;
        size_t offset = (size_t)(va & (VMM_PAGE_SIZE - 1u));
        size_t chunk = VMM_PAGE_SIZE - offset;
        vmm_mapping_info_t mapping;
        if (chunk > length - checked) chunk = length - checked;
        if (!address_space_query(space, va, &mapping) || !mapping.accessible_at_el0 ||
            !mapping.writable || mapping.memory_type != VMM_MEMORY_NORMAL) return 0;
        checked += chunk;
    }
    return 1;
}

static int syscall_path_ends_with(const char *path, const char *suffix) {
    size_t path_len = 0u, suffix_len = 0u, i;
    while (path[path_len] != '\0') ++path_len;
    while (suffix[suffix_len] != '\0') ++suffix_len;
    if (suffix_len > path_len) return 0;
    path += path_len - suffix_len;
    for (i = 0u; i < suffix_len; ++i) {
        char a = path[i], b = suffix[i];
        if (a >= 'a' && a <= 'z') a = (char)(a - ('a' - 'A'));
        if (b >= 'a' && b <= 'z') b = (char)(b - ('a' - 'A'));
        if (a != b) return 0;
    }
    return 1;
}

static int syscall_is_protected_root_directory(const char *path) {
    static const char *const protected_paths[] = {
        "/BIN", "/CFG", "/DOCS", "/DESKTOP", "/DOCUMENTS", "/TRASH"
    };
    size_t i;
    if (path == NULL) return 1;
    for (i = 0u; i < sizeof(protected_paths) / sizeof(protected_paths[0]); ++i)
        if (romfs_path_equal(path, protected_paths[i])) return 1;
    return 0;
}

static uint32_t syscall_file_flags(const char *path, int persistent) {
    uint32_t flags = persistent ? CHOS_FILE_FLAG_PERSISTENT : CHOS_FILE_FLAG_READ_ONLY;
    if (syscall_path_ends_with(path, ".CHX") || syscall_path_ends_with(path, ".ELF")) flags |= CHOS_FILE_FLAG_EXECUTABLE;
    else if (syscall_path_ends_with(path, ".TXT") || syscall_path_ends_with(path, ".MD") ||
             syscall_path_ends_with(path, ".LOG")) flags |= CHOS_FILE_FLAG_TEXT;
    return flags;
}

static int syscall_catalog_find(const chos_file_list_entry_t *entries, size_t count,
                                const char *path) {
    size_t i;
    for (i = 0u; i < count; ++i) if (romfs_path_equal(entries[i].path, path)) return (int)i;
    return -1;
}

static void syscall_catalog_add(chos_file_list_entry_t *entries, size_t *count,
                                const char *path, uint32_t size, uint32_t flags) {
    int found;
    size_t length = 0u;
    if (entries == NULL || count == NULL || path == NULL || !romfs_path_is_valid(path)) return;
    found = syscall_catalog_find(entries, *count, path);
    if (found >= 0) {
        /* Persistent entries override same-named embedded defaults. */
        if ((flags & CHOS_FILE_FLAG_PERSISTENT) != 0u) {
            entries[found].size_bytes = size;
            entries[found].flags = flags;
        }
        return;
    }
    if (*count >= CHOS_FILE_LIST_MAX) return;
    while (path[length] != '\0' && length + 1u < sizeof(entries[*count].path)) {
        entries[*count].path[length] = path[length];
        ++length;
    }
    entries[*count].path[length] = '\0';
    entries[*count].size_bytes = size;
    entries[*count].flags = flags;
    ++*count;
}

static void syscall_catalog_add_parents(chos_file_list_entry_t *entries, size_t *count,
                                        const char *path, uint32_t source_flags) {
    char parent[CHOS_FILE_PATH_CAPACITY];
    size_t i, n = 0u;
    if (path == NULL) return;
    while (path[n] != '\0') ++n;
    for (i = 1u; i < n; ++i) {
        if (path[i] != '/') continue;
        if (i >= sizeof(parent)) break;
        {
            size_t j;
            for (j = 0u; j < i; ++j) parent[j] = path[j];
            parent[i] = '\0';
        }
        syscall_catalog_add(entries, count, parent, 0u,
            (source_flags & CHOS_FILE_FLAG_PERSISTENT) ?
             (CHOS_FILE_FLAG_PERSISTENT | CHOS_FILE_FLAG_DIRECTORY) :
             (CHOS_FILE_FLAG_READ_ONLY | CHOS_FILE_FLAG_DIRECTORY));
    }
}

static int syscall_path_compare(const char *a, const char *b) {
    size_t i = 0u;
    while (a[i] != '\0' && b[i] != '\0') {
        int ac = (unsigned char)a[i], bc = (unsigned char)b[i];
        if (ac >= 'A' && ac <= 'Z') ac += 'a' - 'A';
        if (bc >= 'A' && bc <= 'Z') bc += 'a' - 'A';
        if (ac != bc) return ac - bc;
        ++i;
    }
    return (unsigned char)a[i] - (unsigned char)b[i];
}

static void syscall_copy_bytes(void *destination, const void *source, size_t length) {
    uint8_t *dst = (uint8_t *)destination;
    const uint8_t *src = (const uint8_t *)source;
    size_t i;
    for (i = 0u; i < length; ++i) dst[i] = src[i];
}

static void syscall_catalog_sort(chos_file_list_entry_t *entries, size_t count) {
    size_t i, j;
    for (i = 1u; i < count; ++i) {
        chos_file_list_entry_t key;
        syscall_copy_bytes(&key, &entries[i], sizeof(key));
        j = i;
        while (j > 0u) {
            int key_dir = (key.flags & CHOS_FILE_FLAG_DIRECTORY) != 0u;
            int prev_dir = (entries[j - 1u].flags & CHOS_FILE_FLAG_DIRECTORY) != 0u;
            int comes_first = key_dir != prev_dir ? key_dir > prev_dir :
                syscall_path_compare(key.path, entries[j - 1u].path) < 0;
            if (!comes_first) break;
            syscall_copy_bytes(&entries[j], &entries[j - 1u], sizeof(entries[j]));
            --j;
        }
        syscall_copy_bytes(&entries[j], &key, sizeof(entries[j]));
    }
}

static size_t syscall_build_catalog(chos_file_list_entry_t *catalog) {
    size_t count = 0u, i;
    if (catalog == NULL) return 0u;
    for (i = 0u; i < CHOS_FILE_LIST_MAX; ++i) {
        size_t j;
        for (j = 0u; j < sizeof(catalog[i]); ++j) ((uint8_t *)&catalog[i])[j] = 0u;
    }
    if (storage_mounted()) {
        for (i = 0u; i < CHOS_CHFS_MAX_FILES; ++i) {
            chos_chfs_entry_t entry;
            uint32_t flags;
            if (!storage_get_at(i, &entry)) break;
            if (entry.path[0] == '\0') continue;
            if (chfs_entry_is_directory(&entry)) flags = CHOS_FILE_FLAG_PERSISTENT | CHOS_FILE_FLAG_DIRECTORY;
            else flags = syscall_file_flags(entry.path, 1);
            syscall_catalog_add_parents(catalog, &count, entry.path, flags);
            syscall_catalog_add(catalog, &count, entry.path, entry.size_bytes, flags);
        }
    }
    for (i = 0u; i < romfs_file_count(); ++i) {
        chos_romfs_file_t embedded;
        if (!romfs_get_at(i, &embedded) || embedded.path == NULL || embedded.size > UINT32_MAX) continue;
        if (storage_mounted()) {
            chos_chfs_entry_t persistent;
            if (storage_lookup(embedded.path, &persistent)) continue;
        }
        {
            uint32_t flags = syscall_file_flags(embedded.path, 0);
            syscall_catalog_add_parents(catalog, &count, embedded.path, flags);
            syscall_catalog_add(catalog, &count, embedded.path, (uint32_t)embedded.size, flags);
        }
    }
    syscall_catalog_sort(catalog, count);
    return count;
}

static int syscall_list_files(address_space_handle_t space, uint64_t user_destination,
                              uint64_t capacity64) {
    chos_file_list_entry_t catalog[CHOS_FILE_LIST_MAX];
    size_t count, capacity;
    count = syscall_build_catalog(catalog);
    capacity = capacity64 > CHOS_FILE_LIST_MAX ? CHOS_FILE_LIST_MAX : (size_t)capacity64;
    if (count > capacity) count = capacity;
    if (count != 0u && !address_space_copy_to_user(space, user_destination, catalog,
                                                   count * sizeof(catalog[0]))) return -CHOS_ERRNO_EFAULT;
    return (int)count;
}

static int syscall_path_is_direct_child(const char *directory, const char *path) {
    const char *tail;
    size_t n = 0u;
    if (directory[0] == '/' && directory[1] == '\0') {
        if (path[0] != '/') return 0;
        tail = path + 1;
    } else {
        while (directory[n] != '\0') ++n;
        if (!romfs_path_equal(directory, path) && path[n] == '/') {
            size_t i;
            for (i = 0u; i < n; ++i) {
                int a = directory[i], b = path[i];
                if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
                if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
                if (a != b) return 0;
            }
        } else return 0;
        tail = path + n + 1u;
    }
    if (*tail == '\0') return 0;
    while (*tail != '\0') { if (*tail == '/') return 0; ++tail; }
    return 1;
}

static int syscall_parent_path(const char *path, char parent[CHOS_FILE_PATH_CAPACITY]) {
    size_t length = 0u, slash = 0u, i;
    int found = 0;
    if (path == NULL || parent == NULL) return 0;
    while (path[length] != '\0') {
        if (path[length] == '/') { slash = length; found = 1; }
        ++length;
    }
    if (!found || slash + 1u >= CHOS_FILE_PATH_CAPACITY) return 0;
    if (slash == 0u) { parent[0] = '/'; parent[1] = '\0'; return 1; }
    for (i = 0u; i < slash; ++i) parent[i] = path[i];
    parent[slash] = '\0';
    return 1;
}

static int syscall_catalog_has_path(const char *path, uint32_t required_flags) {
    chos_file_list_entry_t catalog[CHOS_FILE_LIST_MAX];
    size_t count = syscall_build_catalog(catalog), i;
    for (i = 0u; i < count; ++i) {
        if (romfs_path_equal(catalog[i].path, path) &&
            (required_flags == 0u || (catalog[i].flags & required_flags) == required_flags)) return 1;
    }
    return 0;
}

static int syscall_directory_has_children(const char *path) {
    chos_file_list_entry_t catalog[CHOS_FILE_LIST_MAX];
    size_t count = syscall_build_catalog(catalog), i;
    for (i = 0u; i < count; ++i)
        if (syscall_path_is_direct_child(path, catalog[i].path)) return 1;
    return 0;
}

static int syscall_list_directory(address_space_handle_t space, uint64_t user_path,
                                  uint64_t user_destination, uint64_t capacity64) {
    char directory[CHOS_ROMFS_PATH_CAPACITY];
    chos_file_list_entry_t catalog[CHOS_FILE_LIST_MAX], result[CHOS_FILE_LIST_MAX];
    size_t count, out_count = 0u, i, capacity;
    int known_directory = 0;
    if (!syscall_copy_user_path(space, user_path, directory, sizeof(directory))) return -CHOS_ERRNO_EFAULT;
    if (!(directory[0] == '/' && directory[1] == '\0') && !storage_is_directory(directory))
        return -CHOS_ERRNO_ENOTDIR;
    count = syscall_build_catalog(catalog);
    for (i = 0u; i < count; ++i) {
        if (romfs_path_equal(catalog[i].path, directory) &&
            (catalog[i].flags & CHOS_FILE_FLAG_DIRECTORY) != 0u) known_directory = 1;
    }
    if (!(directory[0] == '/' && directory[1] == '\0') && !known_directory && !storage_is_directory(directory))
        return -CHOS_ERRNO_ENOTDIR;
    for (i = 0u; i < count && out_count < CHOS_FILE_LIST_MAX; ++i) {
        if (syscall_path_is_direct_child(directory, catalog[i].path)) {
            syscall_copy_bytes(&result[out_count], &catalog[i], sizeof(result[out_count]));
            ++out_count;
        }
    }
    capacity = capacity64 > CHOS_FILE_LIST_MAX ? CHOS_FILE_LIST_MAX : (size_t)capacity64;
    if (out_count > capacity) out_count = capacity;
    if (out_count != 0u && !address_space_copy_to_user(space, user_destination, result,
                                                       out_count * sizeof(result[0]))) return -CHOS_ERRNO_EFAULT;
    return (int)out_count;
}
static int syscall_spawn_errno(chos_process_error_t error) {
    switch (error) {
        case PROCESS_OK: return 0;
        case PROCESS_ERR_ARGUMENT: return CHOS_ERRNO_EINVAL;
        case PROCESS_ERR_FORMAT: case PROCESS_ERR_ROMFS_CORRUPT: return CHOS_ERRNO_EINVAL;
        case PROCESS_ERR_CAPACITY: return CHOS_ERRNO_EBUSY;
        case PROCESS_ERR_OUT_OF_MEMORY: case PROCESS_ERR_ADDRESS_SPACE: return CHOS_ERRNO_ENOMEM;
        case PROCESS_ERR_NOT_FOUND: return CHOS_ERRNO_ENOENT;
        case PROCESS_ERR_STORAGE_IO: return CHOS_ERRNO_EIO;
        default: return CHOS_ERRNO_EIO;
    }
}

static uint64_t syscall_spawn_via_kernel_service(const char *path, const char *arguments,
                                                int with_arguments,uint64_t policy) {
    uint32_t ticket, pid = 0u;
    uint64_t start_tick;
    chos_process_error_t error = PROCESS_ERR_CONTEXT;
    int status;
    if (!scheduler_is_running()) return neg_errno(CHOS_ERRNO_ENOSYS);
    ticket = process_spawn_request_enqueue_policy(path, arguments, with_arguments,policy);
    if (ticket == 0u) return neg_errno(CHOS_ERRNO_EBUSY);
    start_tick = timer_ticks();
    for (;;) {
        status = process_spawn_request_status(ticket, &error, &pid);
        if (status == 1) {
            process_spawn_request_release(ticket);
            if (error != PROCESS_OK) return neg_errno(syscall_spawn_errno(error));
            return (uint64_t)pid;
        }
        if (status < 0) return neg_errno(CHOS_ERRNO_EIO);
        if (timer_ticks() - start_tick >= 500u) {
            /* If the request never reached the kernel runtime worker, cancel it
             * instead of leaving an EL0 caller blocked indefinitely. A running
             * load is allowed to finish so its resources remain well-owned. */
            if (process_spawn_request_cancel(ticket)) return neg_errno(CHOS_ERRNO_EAGAIN);
            start_tick = timer_ticks();
        }
        {
            uint64_t daif = read_daif();
            __asm__ volatile("msr daifclr, #2\n\tisb" ::: "memory");
            scheduler_yield();
            restore_daif(daif);
        }
    }
}

chos_irq_context_t *exception_lower_sync_dispatch(chos_irq_context_t *frame) {
    uint64_t ec, far;
    address_space_handle_t fault_space;
    if (frame == NULL) {
        for (;;) __asm__ volatile("wfe");
    }
    /* This vector is the lower-EL AArch64 synchronous slot. Resolve its owner
     * from the currently selected task when scheduled, or the legacy session
     * handle when the shell is running the original synchronous smoke test. */
    if ((frame->spsr & 0x0fu) != 0u || !user_mode_get_address_space(&fault_space)) {
        far = read_far_el1();
        exception_panic(frame->esr, frame->elr, far, frame->spsr, 0u);
    }

    ec = (frame->esr >> 26) & 0x3fu;
    if (ec == 0x15u && (frame->esr & 0xffffu) == 0u) { /* SVC #0 from AArch64 EL0. */
        uint64_t permissions=CHOS_PROCESS_CAP_ALL;
        int registered=process_get_capabilities(fault_space,&permissions);
        uint32_t number=(uint32_t)frame->x[8];
        if(!number || number>CHOS_PROCESS_SYSCALL_LIMIT){frame->x[0]=neg_errno(CHOS_ERRNO_ENOSYS);return frame;}
        /* Legacy kernel-created EL0 diagnostic payloads have no process record;
         * permit only their established WRITE/EXIT path, never a full profile. */
        if(!registered && number>=1u && number<=CHOS_PROCESS_SYSCALL_LIMIT &&
           number!=CHOS_SYS_WRITE && number!=CHOS_SYS_EXIT){frame->x[0]=neg_errno(CHOS_ERRNO_ESRCH);return frame;}
        if(number>=1u && number<=CHOS_PROCESS_SYSCALL_LIMIT && !(permissions&CHOS_PROCESS_CAP(number))){
            frame->x[0]=neg_errno(CHOS_ERRNO_EPERM);return frame;
        }
        switch ((uint32_t)frame->x[8]) {
            case CHOS_SYS_WRITE:
                syscall_write(frame);
                return frame;
            case CHOS_SYS_EXIT:
                user_mode_request_exit(frame, (int64_t)frame->x[0], 0);
                return frame;
            case CHOS_SYS_CAPABILITIES:
                frame->x[0]=process_pid_for_address_space(fault_space)?permissions:neg_errno(CHOS_ERRNO_ESRCH);return frame;
            case CHOS_SYS_SPAWN_LIMITED: {
                uint64_t reduced;char path[CHOS_ROMFS_PATH_CAPACITY],args[128];
                if(!process_pid_for_address_space(fault_space)){frame->x[0]=neg_errno(CHOS_ERRNO_ESRCH);return frame;}
                if(!chos_process_cap_reduction(permissions,frame->x[2],&reduced)){frame->x[0]=neg_errno(CHOS_ERRNO_EPERM);return frame;}
                if(!syscall_copy_user_path(fault_space,frame->x[0],path,sizeof(path)) ||
                   !syscall_copy_user_optional_args(fault_space,frame->x[1],args,sizeof(args))){frame->x[0]=neg_errno(CHOS_ERRNO_EFAULT);return frame;}
                frame->x[0]=syscall_spawn_via_kernel_service(path,args,1,reduced);return frame;
            }
            case CHOS_SYS_GETPID: {
                uint32_t pid = process_pid_for_address_space(fault_space);
                frame->x[0] = pid != 0u ? (uint64_t)pid : neg_errno(CHOS_ERRNO_ESRCH);
                return frame;
            }
            case CHOS_SYS_GETSTATUS: {
                chos_process_state_t state;
                int query = process_query_pid((uint32_t)frame->x[0], &state, NULL);
                if (query < 0) frame->x[0] = neg_errno(CHOS_ERRNO_ESRCH);
                else frame->x[0] = (uint64_t)state;
                return frame;
            }
            case CHOS_SYS_WAIT: {
                uint32_t target_pid = (uint32_t)frame->x[0];
                uint32_t current_pid = process_pid_for_address_space(fault_space);
                chos_process_state_t state;
                int64_t exit_status = -1;
                int query;
                if (target_pid == 0u) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EINVAL);
                    return frame;
                }
                if (target_pid == current_pid) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EDEADLK);
                    return frame;
                }
                query = process_query_pid(target_pid, &state, &exit_status);
                if (query < 0) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_ESRCH);
                    return frame;
                }
                if (query == 0) {
                    frame->x[0] = (uint64_t)exit_status;
                    return frame;
                }
                if (!scheduler_is_running() || !user_mode_get_address_space(&fault_space)) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_ENOSYS);
                    return frame;
                }
                /* Waiting is implemented by scheduling other runnable tasks. The
                 * target's terminal record remains queryable until its slot is reused. */
                for (;;) {
                    uint64_t daif = read_daif();
                    __asm__ volatile("msr daifclr, #2\n\tisb" ::: "memory");
                    scheduler_yield();
                    restore_daif(daif);
                    query = process_query_pid(target_pid, &state, &exit_status);
                    if (query < 0) {
                        frame->x[0] = neg_errno(CHOS_ERRNO_ESRCH);
                        return frame;
                    }
                    if (query == 0) {
                        frame->x[0] = (uint64_t)exit_status;
                        return frame;
                    }
                }
            }
            case CHOS_SYS_OPEN: {
                char path[64]; size_t i; int terminated = 0;
                for (i = 0u; i < sizeof(path); ++i) {
                    char ch;
                    if (!address_space_copy_from_user(fault_space, &ch, frame->x[0] + i, 1u)) {
                        frame->x[0] = neg_errno(CHOS_ERRNO_EFAULT); return frame;
                    }
                    path[i] = ch;
                    if (ch == '\0') { terminated = 1; break; }
                }
                if (!terminated) frame->x[0] = neg_errno(CHOS_ERRNO_EINVAL);
                else frame->x[0] = (uint64_t)process_file_open_for_address_space(fault_space, path);
                return frame;
            }
            case CHOS_SYS_READ: {
                uint64_t length = frame->x[2];
                if (length > CHOS_SYS_MAX_READ) frame->x[0] = neg_errno(CHOS_ERRNO_E2BIG);
                else frame->x[0] = (uint64_t)process_file_read_for_address_space(
                    fault_space, (int)frame->x[0], frame->x[1], (size_t)length);
                return frame;
            }
            case CHOS_SYS_CLOSE:
                frame->x[0] = (uint64_t)process_file_close_for_address_space(fault_space, (int)frame->x[0]);
                return frame;
            case CHOS_SYS_SPAWN: {
                char path[CHOS_ROMFS_PATH_CAPACITY];
                if (!syscall_copy_user_path(fault_space, frame->x[0], path, sizeof(path))) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EFAULT); return frame;
                }
                frame->x[0] = syscall_spawn_via_kernel_service(path, "", 0,permissions);
                return frame;
            }
            case CHOS_SYS_LIST_FILES: {
                int result;
                if (frame->x[1] > CHOS_FILE_LIST_MAX) frame->x[1] = CHOS_FILE_LIST_MAX;
                result = syscall_list_files(fault_space, frame->x[0], frame->x[1]);
                frame->x[0] = result < 0 ? neg_errno(-result) : (uint64_t)result;
                return frame;
            }
            case CHOS_SYS_SPAWN_ARGS: {
                char path[CHOS_ROMFS_PATH_CAPACITY];
                char arguments[128];
                if (!syscall_copy_user_path(fault_space, frame->x[0], path, sizeof(path)) ||
                    !syscall_copy_user_path(fault_space, frame->x[1], arguments, sizeof(arguments))) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EFAULT); return frame;
                }
                frame->x[0] = syscall_spawn_via_kernel_service(path, arguments, 1,permissions);
                return frame;
            }
            case CHOS_SYS_CONSOLE_READ: {
                char buffer[CHOS_SYS_MAX_READ];
                char validation[CHOS_SYS_MAX_READ];
                uint64_t user_destination = frame->x[0];
                uint64_t capacity = frame->x[1];
                uint32_t pid = process_pid_for_address_space(fault_space);
                int64_t got;
                if (pid == 0u) { frame->x[0] = neg_errno(CHOS_ERRNO_ESRCH); return frame; }
                if (capacity == 0u) { frame->x[0] = neg_errno(CHOS_ERRNO_EINVAL); return frame; }
                if (capacity > sizeof(buffer)) { frame->x[0] = neg_errno(CHOS_ERRNO_E2BIG); return frame; }
                /* Validate the whole destination before consuming stream bytes so
                 * a bad pointer cannot silently discard queued application output. */
                if (!address_space_copy_from_user(fault_space, validation, user_destination, (size_t)capacity)) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EFAULT); return frame;
                }
                got = chos_gui_console_session_read(pid, buffer, (size_t)capacity);
                if (got < 0) { frame->x[0] = neg_errno(CHOS_ERRNO_EPERM); return frame; }
                if (got == 0) { frame->x[0] = neg_errno(CHOS_ERRNO_EAGAIN); return frame; }
                if (!address_space_copy_to_user(fault_space, user_destination, buffer, (size_t)got)) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EFAULT); return frame;
                }
                frame->x[0] = (uint64_t)got;
                return frame;
            }
            case CHOS_SYS_GUI_SUBMIT: {
                chos_gui_message_header_t header;
                chos_gui_message_t message;
                uint64_t user_pointer = frame->x[0];
                uint64_t size64 = frame->x[1];
                size_t i, size;
                uint32_t pid = process_pid_for_address_space(fault_space);
                if (pid == 0u) { frame->x[0] = neg_errno(CHOS_ERRNO_ESRCH); return frame; }
                if (size64 < sizeof(header) || size64 > sizeof(message)) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EINVAL); return frame;
                }
                size = (size_t)size64;
                if (!address_space_copy_from_user(fault_space, &header, user_pointer, sizeof(header))) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EFAULT); return frame;
                }
                if (!chos_gui_message_header_valid(&header, size) || header.size_bytes != size) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EINVAL); return frame;
                }
                for (i = 0u; i < sizeof(message); ++i) ((uint8_t *)&message)[i] = 0u;
                if (!address_space_copy_from_user(fault_space, &message, user_pointer, size)) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EFAULT); return frame;
                }
                frame->x[0] = (uint64_t)chos_gui_submit_message(pid, &message, size);
                return frame;
            }
            case CHOS_SYS_GUI_POLL_EVENT: {
                chos_gui_message_t event;
                uint64_t user_destination = frame->x[0];
                uint64_t capacity = frame->x[1];
                uint32_t pid = process_pid_for_address_space(fault_space);
                int has_event;
                if (pid == 0u) { frame->x[0] = neg_errno(CHOS_ERRNO_ESRCH); return frame; }
                if (capacity < sizeof(event)) { frame->x[0] = neg_errno(CHOS_ERRNO_EINVAL); return frame; }
                has_event = chos_gui_pop_event(pid, &event);
                if (has_event <= 0) { frame->x[0] = neg_errno(CHOS_ERRNO_EAGAIN); return frame; }
                /* Copy the complete fixed-size envelope. header.size_bytes identifies
                 * the active payload; unused union bytes are zeroed by the server. */
                if (!address_space_copy_to_user(fault_space, user_destination, &event, sizeof(event))) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EFAULT); return frame;
                }
                frame->x[0] = sizeof(event);
                return frame;
            }
            case CHOS_SYS_MKDIR: {
                char path[CHOS_ROMFS_PATH_CAPACITY], parent[CHOS_FILE_PATH_CAPACITY];
                chos_chfs_entry_t existing;
                chos_romfs_file_t embedded;
                if (!syscall_copy_user_path(fault_space, frame->x[0], path, sizeof(path)) ||
                    !syscall_parent_path(path, parent)) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EFAULT); return frame;
                }
                if(chos_network_profile_reserved(path)){frame->x[0]=neg_errno(CHOS_ERRNO_EPERM);return frame;}
                if (!storage_mounted()) { frame->x[0] = neg_errno(CHOS_ERRNO_EIO); return frame; }
                if (romfs_path_equal(path, "/")) { frame->x[0] = neg_errno(CHOS_ERRNO_EEXIST); return frame; }
                if (storage_lookup(path, &existing) || romfs_lookup(path, &embedded) ||
                    storage_is_directory(path)) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EEXIST); return frame;
                }
                if (!storage_is_directory(parent)) { frame->x[0] = neg_errno(CHOS_ERRNO_ENOENT); return frame; }
                frame->x[0] = storage_make_directory(path) ? 0u : neg_errno(CHOS_ERRNO_ENOSPC);
                return frame;
            }
            case CHOS_SYS_WRITE_FILE: {
                char path[CHOS_ROMFS_PATH_CAPACITY], parent[CHOS_FILE_PATH_CAPACITY];
                chos_chfs_entry_t existing;
                chos_romfs_file_t embedded;
                uint64_t length64 = frame->x[2];
                void *buffer = NULL;
                int ok;
                if (!syscall_copy_user_path(fault_space, frame->x[0], path, sizeof(path))) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EFAULT); return frame;
                }
                if(chos_network_profile_reserved(path)){frame->x[0]=neg_errno(CHOS_ERRNO_EPERM);return frame;}
                if (length64 > CHOS_SYS_MAX_FILE_WRITE) { frame->x[0] = neg_errno(CHOS_ERRNO_E2BIG); return frame; }
                if (!storage_mounted()) { frame->x[0] = neg_errno(CHOS_ERRNO_EIO); return frame; }
                if (storage_is_directory(path)) { frame->x[0] = neg_errno(CHOS_ERRNO_EISDIR); return frame; }
                if (!syscall_parent_path(path, parent) || !storage_is_directory(parent)) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_ENOENT); return frame;
                }
                if (romfs_lookup(path, &embedded) && !storage_lookup(path, &existing)) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EPERM); return frame;
                }
                if (length64 != 0u) {
                    buffer = kheap_alloc((size_t)length64);
                    if (buffer == NULL) { frame->x[0] = neg_errno(CHOS_ERRNO_ENOMEM); return frame; }
                    if (!address_space_copy_from_user(fault_space, buffer, frame->x[1], (size_t)length64)) {
                        (void)kheap_free(buffer); frame->x[0] = neg_errno(CHOS_ERRNO_EFAULT); return frame;
                    }
                }
                ok = storage_write_file(path, buffer, (size_t)length64);
                if (buffer != NULL) (void)kheap_free(buffer);
                if (!ok) { frame->x[0] = neg_errno(CHOS_ERRNO_ENOSPC); return frame; }
                frame->x[0] = length64;
                return frame;
            }
            case CHOS_SYS_UNLINK: {
                char path[CHOS_ROMFS_PATH_CAPACITY];
                chos_chfs_entry_t entry;
                if (!syscall_copy_user_path(fault_space, frame->x[0], path, sizeof(path))) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EFAULT); return frame;
                }
                if(chos_network_profile_reserved(path)){frame->x[0]=neg_errno(CHOS_ERRNO_EPERM);return frame;}
                if (!storage_mounted()) { frame->x[0] = neg_errno(CHOS_ERRNO_EIO); return frame; }
                if (storage_is_directory(path)) { frame->x[0] = neg_errno(CHOS_ERRNO_EISDIR); return frame; }
                if (!storage_lookup(path, &entry)) {
                    frame->x[0] = syscall_catalog_has_path(path, 0u) ? neg_errno(CHOS_ERRNO_EPERM) : neg_errno(CHOS_ERRNO_ENOENT);
                    return frame;
                }
                frame->x[0] = storage_remove_file(path) ? 0u : neg_errno(CHOS_ERRNO_EIO);
                return frame;
            }
            case CHOS_SYS_RENAME: {
                char old_path[CHOS_ROMFS_PATH_CAPACITY], new_path[CHOS_ROMFS_PATH_CAPACITY], parent[CHOS_FILE_PATH_CAPACITY];
                chos_chfs_entry_t entry;
                chos_romfs_file_t embedded;
                int old_is_directory;
                size_t old_length = 0u, new_length = 0u;
                if (!syscall_copy_user_path(fault_space, frame->x[0], old_path, sizeof(old_path)) ||
                    !syscall_copy_user_path(fault_space, frame->x[1], new_path, sizeof(new_path)) ||
                    !syscall_parent_path(new_path, parent)) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EFAULT); return frame;
                }
                if(chos_network_profile_reserved(old_path)||chos_network_profile_reserved(new_path)){frame->x[0]=neg_errno(CHOS_ERRNO_EPERM);return frame;}
                if (!storage_mounted()) { frame->x[0] = neg_errno(CHOS_ERRNO_EIO); return frame; }
                while (old_path[old_length] != '\0') ++old_length;
                while (new_path[new_length] != '\0') ++new_length;
                if (old_length >= CHOS_CHFS_PATH_MAX || new_length >= CHOS_CHFS_PATH_MAX ||
                    (old_path[0] == '/' && old_path[1] == '\0') ||
                    (new_path[0] == '/' && new_path[1] == '\0')) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EINVAL); return frame;
                }
                old_is_directory = storage_is_directory(old_path);
                if (old_is_directory && syscall_is_protected_root_directory(old_path)) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EPERM); return frame;
                }
                if (!old_is_directory && !storage_lookup(old_path, &entry)) {
                    frame->x[0] = syscall_catalog_has_path(old_path, 0u) ? neg_errno(CHOS_ERRNO_EPERM) : neg_errno(CHOS_ERRNO_ENOENT);
                    return frame;
                }
                /* Case-insensitive same-path rename is a successful no-op. */
                if (romfs_path_equal(old_path, new_path)) { frame->x[0] = 0u; return frame; }
                if (!storage_is_directory(parent)) { frame->x[0] = neg_errno(CHOS_ERRNO_ENOENT); return frame; }
                if (storage_lookup(new_path, &entry) || storage_is_directory(new_path)) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EEXIST); return frame;
                }
                if (romfs_lookup(new_path, &embedded)) { frame->x[0] = neg_errno(CHOS_ERRNO_EEXIST); return frame; }
                if (old_is_directory)
                    frame->x[0] = storage_rename_directory(old_path, new_path) ? 0u : neg_errno(CHOS_ERRNO_EINVAL);
                else
                    frame->x[0] = storage_rename_file(old_path, new_path) ? 0u : neg_errno(CHOS_ERRNO_EIO);
                return frame;
            }
            case CHOS_SYS_LIST_DIR: {
                int result = syscall_list_directory(fault_space, frame->x[0], frame->x[1], frame->x[2]);
                frame->x[0] = result < 0 ? neg_errno(-result) : (uint64_t)result;
                return frame;
            }
            case CHOS_SYS_RMDIR: {
                char path[CHOS_ROMFS_PATH_CAPACITY];
                if (!syscall_copy_user_path(fault_space, frame->x[0], path, sizeof(path))) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EFAULT); return frame;
                }
                if(chos_network_profile_reserved(path)){frame->x[0]=neg_errno(CHOS_ERRNO_EPERM);return frame;}
                if (path[0] == '/' && path[1] == '\0') { frame->x[0] = neg_errno(CHOS_ERRNO_EBUSY); return frame; }
                if (!storage_is_directory(path)) {
                    frame->x[0] = syscall_catalog_has_path(path, 0u) ? neg_errno(CHOS_ERRNO_ENOTDIR) : neg_errno(CHOS_ERRNO_ENOENT);
                    return frame;
                }
                if (syscall_directory_has_children(path)) { frame->x[0] = neg_errno(CHOS_ERRNO_ENOTEMPTY); return frame; }
                if (!storage_mounted()) { frame->x[0] = neg_errno(CHOS_ERRNO_EIO); return frame; }
                frame->x[0] = storage_remove_directory(path) ? 0u : neg_errno(CHOS_ERRNO_EPERM);
                return frame;
            }
            case CHOS_SYS_NET_OPEN: {
                uint32_t owner = process_pid_for_address_space(fault_space);
                uint64_t local_port = frame->x[0];
                int handle;
                if (owner == 0u) { frame->x[0] = neg_errno(CHOS_ERRNO_ESRCH); return frame; }
                if (local_port > 65535u) { frame->x[0] = neg_errno(CHOS_ERRNO_EINVAL); return frame; }
                handle = chos_tcp_open_for_owner(owner, (uint16_t)local_port);
                frame->x[0] = handle > 0 ? (uint64_t)handle : neg_errno(CHOS_ERRNO_EMFILE);
                return frame;
            }
            case CHOS_SYS_NET_CONNECT: {
                uint32_t owner = process_pid_for_address_space(fault_space);
                uint64_t remote_ip64 = frame->x[1], remote_port64 = frame->x[2];
                chos_tcp_socket_info_t info;
                int handle = (int)frame->x[0];
                if (owner == 0u) { frame->x[0] = neg_errno(CHOS_ERRNO_ESRCH); return frame; }
                if (remote_ip64 > UINT32_MAX || remote_port64 == 0u || remote_port64 > 65535u ||
                    remote_ip64 == 0u || remote_ip64 == UINT32_MAX) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EINVAL); return frame;
                }
                if (!chos_tcp_get_info_for_owner(owner, handle, &info)) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EBADF); return frame;
                }
                frame->x[0] = chos_tcp_connect_for_owner(owner, handle, (uint32_t)remote_ip64,
                                                         (uint16_t)remote_port64) == 1u
                    ? 0u : neg_errno(CHOS_ERRNO_EAGAIN);
                return frame;
            }
            case CHOS_SYS_NET_STATUS: {
                uint32_t owner = process_pid_for_address_space(fault_space);
                chos_tcp_socket_info_t info;
                uint64_t out_pointer = frame->x[1];
                if (owner == 0u) { frame->x[0] = neg_errno(CHOS_ERRNO_ESRCH); return frame; }
                if (!chos_tcp_get_info_for_owner(owner, (int)frame->x[0], &info)) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EBADF); return frame;
                }
                if (!syscall_user_range_writable(fault_space, out_pointer, sizeof(info)) ||
                    !address_space_copy_to_user(fault_space, out_pointer, &info, sizeof(info))) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EFAULT); return frame;
                }
                frame->x[0] = 0u;
                return frame;
            }
            case CHOS_SYS_NET_SEND: {
                uint32_t owner = process_pid_for_address_space(fault_space);
                int handle = (int)frame->x[0];
                uint64_t length64 = frame->x[2];
                uint8_t buffer[CHOS_SYS_MAX_NET_TRANSFER];
                chos_tcp_socket_info_t info;
                int amount;
                if (owner == 0u) { frame->x[0] = neg_errno(CHOS_ERRNO_ESRCH); return frame; }
                if (length64 > CHOS_SYS_MAX_NET_TRANSFER) { frame->x[0] = neg_errno(CHOS_ERRNO_E2BIG); return frame; }
                if (!chos_tcp_get_info_for_owner(owner, handle, &info)) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EBADF); return frame;
                }
                if (length64 == 0u) { frame->x[0] = 0u; return frame; }
                if (!address_space_copy_from_user(fault_space, buffer, frame->x[1], (size_t)length64)) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EFAULT); return frame;
                }
                amount = chos_tcp_send_for_owner(owner, handle, buffer, (size_t)length64);
                frame->x[0] = amount < 0 ? neg_errno(CHOS_ERRNO_EBADF) :
                              (amount == 0 ? neg_errno(CHOS_ERRNO_EAGAIN) : (uint64_t)amount);
                return frame;
            }
            case CHOS_SYS_NET_RECV: {
                uint32_t owner = process_pid_for_address_space(fault_space);
                int handle = (int)frame->x[0];
                uint64_t capacity64 = frame->x[2];
                uint8_t buffer[CHOS_SYS_MAX_NET_TRANSFER];
                chos_tcp_socket_info_t info;
                int amount;
                if (owner == 0u) { frame->x[0] = neg_errno(CHOS_ERRNO_ESRCH); return frame; }
                if (capacity64 > CHOS_SYS_MAX_NET_TRANSFER) { frame->x[0] = neg_errno(CHOS_ERRNO_E2BIG); return frame; }
                if (!chos_tcp_get_info_for_owner(owner, handle, &info)) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EBADF); return frame;
                }
                if (capacity64 == 0u) { frame->x[0] = 0u; return frame; }
                if (!syscall_user_range_writable(fault_space, frame->x[1], (size_t)capacity64)) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EFAULT); return frame;
                }
                amount = chos_tcp_recv_for_owner(owner, handle, buffer, (size_t)capacity64);
                if (amount < 0) { frame->x[0] = neg_errno(CHOS_ERRNO_EBADF); return frame; }
                if (amount == 0) {
                    if (info.state == CHOS_TCP_CLOSE_WAIT || info.state == CHOS_TCP_FIN_WAIT_2 ||
                        info.state == CHOS_TCP_TIME_WAIT || info.state == CHOS_TCP_CLOSED) {
                        frame->x[0] = 0u; /* peer EOF */
                    } else if (info.state == CHOS_TCP_FAILED) {
                        frame->x[0] = neg_errno(CHOS_ERRNO_EIO);
                    } else {
                        frame->x[0] = neg_errno(CHOS_ERRNO_EAGAIN);
                    }
                    return frame;
                }
                if (!address_space_copy_to_user(fault_space, frame->x[1], buffer, (size_t)amount)) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EFAULT); return frame;
                }
                frame->x[0] = (uint64_t)amount;
                return frame;
            }
            case CHOS_SYS_NET_CLOSE: {
                uint32_t owner = process_pid_for_address_space(fault_space);
                chos_tcp_socket_info_t info;
                int result;
                if (owner == 0u) { frame->x[0] = neg_errno(CHOS_ERRNO_ESRCH); return frame; }
                if (!chos_tcp_get_info_for_owner(owner, (int)frame->x[0], &info)) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EBADF); return frame;
                }
                result = chos_tcp_close_for_owner(owner, (int)frame->x[0]);
                frame->x[0] = result < 0 ? neg_errno(CHOS_ERRNO_EBADF) :
                              (result == 0 ? neg_errno(CHOS_ERRNO_EAGAIN) : 0u);
                return frame;
            }
            case CHOS_SYS_NET_ABORT: {
                uint32_t owner = process_pid_for_address_space(fault_space);
                int result;
                if (owner == 0u) { frame->x[0] = neg_errno(CHOS_ERRNO_ESRCH); return frame; }
                result = chos_tcp_abort_for_owner(owner, (int)frame->x[0]);
                frame->x[0] = result < 0 ? neg_errno(CHOS_ERRNO_EBADF) : 0u;
                return frame;
            }
            case CHOS_SYS_NET_DNS_START: {
                uint32_t owner = process_pid_for_address_space(fault_space);
                char hostname[CHOS_NET_DNS_API_HOST_MAX];
                chos_net_dns_info_t info;
                if (owner == 0u) { frame->x[0] = neg_errno(CHOS_ERRNO_ESRCH); return frame; }
                if (!syscall_copy_user_path(fault_space, frame->x[0], hostname, sizeof(hostname))) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EFAULT); return frame;
                }
                chos_net_dns_get_info(&info);
                if (info.state == CHOS_NET_DNS_PENDING) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EBUSY); return frame;
                }
                frame->x[0] = chos_net_dns_lookup_start(hostname) ? 0u : neg_errno(CHOS_ERRNO_EAGAIN);
                return frame;
            }
            case CHOS_SYS_NET_DNS_STATUS: {
                uint32_t owner = process_pid_for_address_space(fault_space);
                uint64_t out_pointer = frame->x[0];
                chos_net_dns_info_t info;
                chos_dns_api_info_t result;
                size_t i;
                if (owner == 0u) { frame->x[0] = neg_errno(CHOS_ERRNO_ESRCH); return frame; }
                if (!syscall_user_range_writable(fault_space, out_pointer, sizeof(result))) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EFAULT); return frame;
                }
                chos_net_dns_get_info(&info);
                result.state = (chos_dns_api_state_t)info.state;
                for (i = 0u; i < sizeof(result.hostname); ++i) result.hostname[i] = info.hostname[i];
                result.ipv4_address = info.ipv4_address;
                result.ttl_seconds = info.ttl_seconds;
                result.query_id = info.query_id;
                result.retries = info.retries;
                if (!address_space_copy_to_user(fault_space, out_pointer, &result, sizeof(result))) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EFAULT); return frame;
                }
                frame->x[0] = 0u;
                return frame;
            }
            case CHOS_SYS_MAP_MEMORY: {
                if(process_pid_for_address_space(fault_space)==0u){frame->x[0]=neg_errno(CHOS_ERRNO_ESRCH);return frame;}
                if(frame->x[1]){frame->x[0]=neg_errno(CHOS_ERRNO_EINVAL);return frame;}
                frame->x[0]=(uint64_t)chos_user_memory_map(fault_space,(size_t)frame->x[0],0);return frame;
            }
            case CHOS_SYS_UNMAP_MEMORY: {
                if(process_pid_for_address_space(fault_space)==0u){frame->x[0]=neg_errno(CHOS_ERRNO_ESRCH);return frame;}
                frame->x[0]=(uint64_t)(int64_t)chos_user_memory_unmap(fault_space,frame->x[0],(size_t)frame->x[1]);return frame;
            }
            case CHOS_SYS_NET_INFO: {
                chos_net_status_t status;
                if(process_pid_for_address_space(fault_space)==0u){frame->x[0]=neg_errno(CHOS_ERRNO_ESRCH);return frame;}
                if(frame->x[1]<sizeof(status)){frame->x[0]=neg_errno(CHOS_ERRNO_EINVAL);return frame;}
                if(!syscall_user_range_writable(fault_space,frame->x[0],sizeof(status))){frame->x[0]=neg_errno(CHOS_ERRNO_EFAULT);return frame;}
                chos_net_manager_status(&status);
                frame->x[0]=address_space_copy_to_user(fault_space,frame->x[0],&status,sizeof(status))?0u:neg_errno(CHOS_ERRNO_EFAULT);
                return frame;
            }
            case CHOS_SYS_IPC_OPEN: {
                uint32_t owner=process_pid_for_address_space(fault_space);
                chos_process_info_t peer;
                if(!owner){frame->x[0]=neg_errno(CHOS_ERRNO_ESRCH);return frame;}
                if(frame->x[0]>UINT32_MAX || !process_get_info_by_pid((uint32_t)frame->x[0],&peer) ||
                   (peer.state!=CHOS_PROCESS_RUNNING && peer.state!=CHOS_PROCESS_QUEUED)){
                    frame->x[0]=neg_errno(CHOS_ERRNO_ESRCH);return frame;
                }
                frame->x[0]=(uint64_t)chos_ipc_open(owner,peer.pid);return frame;
            }
            case CHOS_SYS_IPC_STATUS: {
                uint32_t owner=process_pid_for_address_space(fault_space);chos_ipc_info_t info;
                if(!owner){frame->x[0]=neg_errno(CHOS_ERRNO_ESRCH);return frame;}
                if(frame->x[2]<sizeof(info)){frame->x[0]=neg_errno(CHOS_ERRNO_EINVAL);return frame;}
                if(!syscall_user_range_writable(fault_space,frame->x[1],sizeof(info))){frame->x[0]=neg_errno(CHOS_ERRNO_EFAULT);return frame;}
                int rc=chos_ipc_status(owner,frame->x[0],&info);
                if(!rc && !address_space_copy_to_user(fault_space,frame->x[1],&info,sizeof(info)))rc=-CHOS_ERRNO_EFAULT;
                frame->x[0]=(uint64_t)(int64_t)rc;return frame;
            }
            case CHOS_SYS_IPC_SEND:
            case CHOS_SYS_IPC_RECV: {
                uint32_t owner=process_pid_for_address_space(fault_space);uint8_t data[CHOS_IPC_MESSAGE_MAX];
                uint64_t handle=frame->x[0],pointer=frame->x[1],length=frame->x[2];int rc;
                if(!owner){frame->x[0]=neg_errno(CHOS_ERRNO_ESRCH);return frame;}
                if(!length||length>sizeof(data)){frame->x[0]=neg_errno(CHOS_ERRNO_EINVAL);return frame;}
                if((uint32_t)frame->x[8]==CHOS_SYS_IPC_SEND){
                    if(!address_space_copy_from_user(fault_space,data,pointer,(size_t)length))rc=-CHOS_ERRNO_EFAULT;
                    else rc=chos_ipc_send(owner,handle,data,(size_t)length);
                }else{
                    if(!syscall_user_range_writable(fault_space,pointer,(size_t)length))rc=-CHOS_ERRNO_EFAULT;
                    else{rc=chos_ipc_receive(owner,handle,data,(size_t)length);
                        if(rc>0 && !address_space_copy_to_user(fault_space,pointer,data,(size_t)rc))rc=-CHOS_ERRNO_EFAULT;
                    }
                }
                frame->x[0]=(uint64_t)(int64_t)rc;return frame;
            }
            case CHOS_SYS_IPC_CLOSE: {
                uint32_t owner=process_pid_for_address_space(fault_space);
                frame->x[0]=owner?(uint64_t)(int64_t)chos_ipc_close(owner,frame->x[0]):neg_errno(CHOS_ERRNO_ESRCH);return frame;
            }
            case CHOS_SYS_CLOCK_INFO: {
                chos_clock_info_t status;
                if(!process_pid_for_address_space(fault_space)){frame->x[0]=neg_errno(CHOS_ERRNO_ESRCH);return frame;}
                if(frame->x[1]<sizeof(status)){frame->x[0]=neg_errno(CHOS_ERRNO_EINVAL);return frame;}
                if(!syscall_user_range_writable(fault_space,frame->x[0],sizeof(status))){frame->x[0]=neg_errno(CHOS_ERRNO_EFAULT);return frame;}
                chos_clock_get_info(&status);
                frame->x[0]=address_space_copy_to_user(fault_space,frame->x[0],&status,sizeof(status))?0u:neg_errno(CHOS_ERRNO_EFAULT);
                return frame;
            }
            case CHOS_SYS_NET6_INFO: {
                chos_net6_info_t status;
                if(!process_pid_for_address_space(fault_space)){frame->x[0]=neg_errno(CHOS_ERRNO_ESRCH);return frame;}
                if(frame->x[1]<sizeof(status)){frame->x[0]=neg_errno(CHOS_ERRNO_EINVAL);return frame;}
                if(!syscall_user_range_writable(fault_space,frame->x[0],sizeof(status))){frame->x[0]=neg_errno(CHOS_ERRNO_EFAULT);return frame;}
                chos_net6_get_info(&status);
                frame->x[0]=address_space_copy_to_user(fault_space,frame->x[0],&status,sizeof(status))?0:neg_errno(CHOS_ERRNO_EFAULT);
                return frame;
            }
            case CHOS_SYS_NET6_PING: {
                char literal[46];
                if(!process_pid_for_address_space(fault_space)){frame->x[0]=neg_errno(CHOS_ERRNO_ESRCH);return frame;}
                if(!syscall_copy_user_path(fault_space,frame->x[0],literal,sizeof(literal))){frame->x[0]=neg_errno(CHOS_ERRNO_EFAULT);return frame;}
                frame->x[0]=chos_net6_ping(literal)?0:neg_errno(CHOS_ERRNO_EAGAIN);return frame;
            }
            case CHOS_SYS_NET6_OPEN: {
                uint32_t owner=process_pid_for_address_space(fault_space);
                if(!owner){frame->x[0]=neg_errno(CHOS_ERRNO_ESRCH);return frame;}
                if(frame->x[0]>65535u){frame->x[0]=neg_errno(CHOS_ERRNO_EINVAL);return frame;}
                int handle=chos_tcp6_open(owner,(uint16_t)frame->x[0]);
                frame->x[0]=handle>0?(uint64_t)handle:neg_errno(CHOS_ERRNO_EMFILE);return frame;
            }
            case CHOS_SYS_NET6_CONNECT: {
                uint32_t owner=process_pid_for_address_space(fault_space);uint8_t remote[16];chos_tcp6_socket_info_t info;
                if(!owner){frame->x[0]=neg_errno(CHOS_ERRNO_ESRCH);return frame;}
                if(!frame->x[2] || frame->x[2]>65535u){frame->x[0]=neg_errno(CHOS_ERRNO_EINVAL);return frame;}
                if(!chos_tcp6_info(owner,(int)frame->x[0],&info)){frame->x[0]=neg_errno(CHOS_ERRNO_EBADF);return frame;}
                if(!address_space_copy_from_user(fault_space,remote,frame->x[1],sizeof(remote))){frame->x[0]=neg_errno(CHOS_ERRNO_EFAULT);return frame;}
                frame->x[0]=chos_tcp6_connect(owner,(int)frame->x[0],remote,(uint16_t)frame->x[2])?0:neg_errno(CHOS_ERRNO_EAGAIN);return frame;
            }
            case CHOS_SYS_NET6_STATUS: {
                uint32_t owner=process_pid_for_address_space(fault_space);chos_tcp6_socket_info_t info;
                if(!owner){frame->x[0]=neg_errno(CHOS_ERRNO_ESRCH);return frame;}
                if(frame->x[2]<sizeof(info)){frame->x[0]=neg_errno(CHOS_ERRNO_EINVAL);return frame;}
                if(!chos_tcp6_info(owner,(int)frame->x[0],&info)){frame->x[0]=neg_errno(CHOS_ERRNO_EBADF);return frame;}
                if(!syscall_user_range_writable(fault_space,frame->x[1],sizeof(info)) ||
                   !address_space_copy_to_user(fault_space,frame->x[1],&info,sizeof(info))){frame->x[0]=neg_errno(CHOS_ERRNO_EFAULT);return frame;}
                frame->x[0]=0;return frame;
            }
            case CHOS_SYS_NET6_SEND: {
                uint32_t owner=process_pid_for_address_space(fault_space);chos_tcp6_socket_info_t info;uint8_t bytes[CHOS_TCP6_MAX_TRANSFER];
                if(!owner){frame->x[0]=neg_errno(CHOS_ERRNO_ESRCH);return frame;}
                if(frame->x[2]>sizeof(bytes)){frame->x[0]=neg_errno(CHOS_ERRNO_E2BIG);return frame;}
                if(!chos_tcp6_info(owner,(int)frame->x[0],&info)){frame->x[0]=neg_errno(CHOS_ERRNO_EBADF);return frame;}
                if(!frame->x[2]){frame->x[0]=0;return frame;}
                if(!address_space_copy_from_user(fault_space,bytes,frame->x[1],(size_t)frame->x[2])){frame->x[0]=neg_errno(CHOS_ERRNO_EFAULT);return frame;}
                int n=chos_tcp6_send(owner,(int)frame->x[0],bytes,(size_t)frame->x[2]);
                frame->x[0]=n<0?neg_errno(CHOS_ERRNO_EBADF):n?(uint64_t)n:neg_errno(info.state==CHOS_TCP_FAILED?CHOS_ERRNO_EIO:CHOS_ERRNO_EAGAIN);return frame;
            }
            case CHOS_SYS_NET6_RECV: {
                uint32_t owner=process_pid_for_address_space(fault_space);chos_tcp6_socket_info_t info;uint8_t bytes[CHOS_TCP6_MAX_TRANSFER];
                if(!owner){frame->x[0]=neg_errno(CHOS_ERRNO_ESRCH);return frame;}
                if(frame->x[2]>sizeof(bytes)){frame->x[0]=neg_errno(CHOS_ERRNO_E2BIG);return frame;}
                if(!chos_tcp6_info(owner,(int)frame->x[0],&info)){frame->x[0]=neg_errno(CHOS_ERRNO_EBADF);return frame;}
                if(!frame->x[2]){frame->x[0]=0;return frame;}
                if(!syscall_user_range_writable(fault_space,frame->x[1],(size_t)frame->x[2])){frame->x[0]=neg_errno(CHOS_ERRNO_EFAULT);return frame;}
                int n=chos_tcp6_recv(owner,(int)frame->x[0],bytes,(size_t)frame->x[2]);
                if(n<0){frame->x[0]=neg_errno(CHOS_ERRNO_EBADF);return frame;}
                if(!n){frame->x[0]=info.receive_eof || info.state==CHOS_TCP_CLOSED?0:neg_errno(info.state==CHOS_TCP_FAILED?CHOS_ERRNO_EIO:CHOS_ERRNO_EAGAIN);return frame;}
                frame->x[0]=address_space_copy_to_user(fault_space,frame->x[1],bytes,(size_t)n)?(uint64_t)n:neg_errno(CHOS_ERRNO_EFAULT);return frame;
            }
            case CHOS_SYS_NET6_CLOSE:
            case CHOS_SYS_NET6_ABORT: {
                uint32_t owner=process_pid_for_address_space(fault_space);
                if(!owner){frame->x[0]=neg_errno(CHOS_ERRNO_ESRCH);return frame;}
                int n=(uint32_t)frame->x[8]==CHOS_SYS_NET6_CLOSE?chos_tcp6_close(owner,(int)frame->x[0]):chos_tcp6_abort(owner,(int)frame->x[0]);
                frame->x[0]=n<0?neg_errno(CHOS_ERRNO_EBADF):n?0:neg_errno(CHOS_ERRNO_EAGAIN);return frame;
            }
            case CHOS_SYS_NET6_DNS_START: {
                uint32_t owner=process_pid_for_address_space(fault_space);char host[CHOS_DNS6_HOST_MAX];chos_dns6_info_t info;
                if(!owner){frame->x[0]=neg_errno(CHOS_ERRNO_ESRCH);return frame;}
                if(!syscall_copy_user_path(fault_space,frame->x[0],host,sizeof(host))){frame->x[0]=neg_errno(CHOS_ERRNO_EFAULT);return frame;}
                chos_net6_dns_info(owner,&info);
                if(info.state==CHOS_DNS6_PENDING){frame->x[0]=neg_errno(CHOS_ERRNO_EBUSY);return frame;}
                frame->x[0]=chos_net6_dns_start(owner,host)?0:neg_errno(CHOS_ERRNO_EAGAIN);return frame;
            }
            case CHOS_SYS_NET6_DNS_STATUS: {
                uint32_t owner=process_pid_for_address_space(fault_space);chos_dns6_info_t info;
                if(!owner){frame->x[0]=neg_errno(CHOS_ERRNO_ESRCH);return frame;}
                if(frame->x[1]<sizeof(info)){frame->x[0]=neg_errno(CHOS_ERRNO_EINVAL);return frame;}
                if(!syscall_user_range_writable(fault_space,frame->x[0],sizeof(info))){frame->x[0]=neg_errno(CHOS_ERRNO_EFAULT);return frame;}
                chos_net6_dns_info(owner,&info);
                frame->x[0]=address_space_copy_to_user(fault_space,frame->x[0],&info,sizeof(info))?0:neg_errno(CHOS_ERRNO_EFAULT);return frame;
            }
            case CHOS_SYS_RANDOM_BYTES: {
                uint32_t owner = process_pid_for_address_space(fault_space);
                uint64_t out_pointer = frame->x[0];
                uint64_t capacity64 = frame->x[1];
                uint8_t random_bytes[CHOS_SYS_MAX_RANDOM_BYTES];
                size_t amount;
                _Static_assert(CHOS_SYS_MAX_RANDOM_BYTES == CHOS_ENTROPY_MAX_REQUEST,
                               "user ABI and entropy driver request bounds must match");
                if (owner == 0u) { frame->x[0] = neg_errno(CHOS_ERRNO_ESRCH); return frame; }
                if (capacity64 > CHOS_SYS_MAX_RANDOM_BYTES) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_E2BIG); return frame;
                }
                if (capacity64 == 0u) { frame->x[0] = 0u; return frame; }
                if (!syscall_user_range_writable(fault_space, out_pointer, (size_t)capacity64)) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EFAULT); return frame;
                }
                amount = chos_entropy_read(random_bytes, (size_t)capacity64);
                if (amount == 0u) { frame->x[0] = neg_errno(CHOS_ERRNO_EAGAIN); return frame; }
                if (!address_space_copy_to_user(fault_space, out_pointer, random_bytes, amount)) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EFAULT); return frame;
                }
                frame->x[0] = (uint64_t)amount;
                return frame;
            }
            case CHOS_SYS_FSYNC:
                frame->x[0] = storage_sync() ? 0u : neg_errno(CHOS_ERRNO_EIO);
                return frame;
            case CHOS_SYS_YIELD: {
                uint64_t daif;
                if (!scheduler_is_running() || !user_mode_get_address_space(&fault_space)) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_ENOSYS);
                    return frame;
                }
                /* Lower-EL synchronous exception entry may mask IRQs on some
                 * implementations. Yield waits for a timer-selected resumption. */
                daif = read_daif();
                __asm__ volatile("msr daifclr, #2\n\tisb" ::: "memory");
                scheduler_yield();
                restore_daif(daif);
                frame->x[0] = 0u;
                return frame;
            }
            case CHOS_SYS_SLEEP: {
                uint64_t ticks = frame->x[0];
                uint64_t daif;
                if (!scheduler_is_running() || !user_mode_get_address_space(&fault_space)) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_ENOSYS);
                    return frame;
                }
                if (ticks > CHOS_SYS_MAX_SLEEP_TICKS) {
                    frame->x[0] = neg_errno(CHOS_ERRNO_EINVAL);
                    return frame;
                }
                daif = read_daif();
                __asm__ volatile("msr daifclr, #2\n\tisb" ::: "memory");
                scheduler_sleep_ticks(ticks);
                restore_daif(daif);
                frame->x[0] = 0u;
                return frame;
            }
            default:
                frame->x[0] = neg_errno(CHOS_ERRNO_ENOSYS);
                return frame;
        }
    }

    /* An invalid instruction/data access from this test process must not bring down
       the kernel. Report it and redirect exception return to the EL1 cleanup trampoline. */
    far = read_far_el1();
    process_record_user_fault(frame->esr, frame->elr, far);
    uart_puts("\n[USER FAULT] EL0 context trapped; ESR=");
    print_hex64(frame->esr);
    uart_puts(" ELR=");
    print_hex64(frame->elr);
    uart_puts(" FAR=");
    print_hex64(far);
    uart_puts("\n");
    user_mode_force_return(frame, (int64_t)(-128 - (int64_t)ec));
    return frame;
}

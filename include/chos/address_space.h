#ifndef CHOS_ADDRESS_SPACE_H
#define CHOS_ADDRESS_SPACE_H

#include <stddef.h>
#include <stdint.h>
#include "chos/vmm.h"

#define CHOS_AS_MAX_SPACES 4u
#define CHOS_AS_USER_VA_BASE  0x20000000ull
#define CHOS_AS_USER_VA_LIMIT 0x30000000ull
#define CHOS_AS_USER_L2_FIRST ((uint32_t)(CHOS_AS_USER_VA_BASE >> 21))
#define CHOS_AS_USER_L2_LIMIT ((uint32_t)(CHOS_AS_USER_VA_LIMIT >> 21))
#define CHOS_AS_USER_L2_SLOTS (CHOS_AS_USER_L2_LIMIT - CHOS_AS_USER_L2_FIRST)
#define CHOS_AS_USER_PAGE_LIMIT ((CHOS_AS_USER_VA_LIMIT - CHOS_AS_USER_VA_BASE) / VMM_PAGE_SIZE)
#define CHOS_AS_MAX_USER_FRAMES 2048u /* 8 MiB of zero-filled frames per address space. */

typedef struct {
    uint32_t slot;
    uint32_t generation;
    uint32_t id;
} address_space_handle_t;

typedef enum {
    AS_PERM_USER_RO = 0,       /* EL0 read-only, execute-never */
    AS_PERM_USER_RW = 1,       /* EL0 read/write, execute-never */
    AS_PERM_USER_RX = 2,       /* EL0 read/execute, EL1 execute-never */
    AS_PERM_USER_NONE = 3      /* Owned normal storage, no EL0 access, PXN/UXN */
} address_space_user_permissions_t;

typedef enum {
    AS_OK = 0,
    AS_ERR_ARGUMENT,
    AS_ERR_VMM_NOT_READY,
    AS_ERR_CAPACITY,
    AS_ERR_OUT_OF_MEMORY,
    AS_ERR_BAD_HANDLE,
    AS_ERR_VA_RANGE,
    AS_ERR_ALIGNMENT,
    AS_ERR_PAGE_NOT_OWNED,
    AS_ERR_PAGE_NOT_MAPPED,
    AS_ERR_PAGE_MAPPED,
    AS_ERR_FRAME_CAPACITY,
    AS_ERR_MAPPING_CONFLICT,
    AS_ERR_KERNEL_TEMPLATE,
    AS_ERR_HAS_MAPPINGS,
    AS_ERR_BAD_DESCRIPTOR,
    AS_ERR_ACTIVE,
    AS_ERR_REFERENCED,
    AS_ERR_BAD_REFERENCE,
    AS_ERR_SWITCH_FAILED
} address_space_error_t;

typedef struct {
    int live;
    uint32_t slot;
    uint32_t id;
    uint32_t generation;
    uint32_t mapped_user_pages;
    uint32_t owned_user_frames;
    uint32_t user_l3_tables;
    uint32_t task_references;
    uint64_t root_table_physical;
} address_space_info_t;

/* Pure policy/layout helpers, host-tested independently of the EL1 table walker. */
int address_space_user_va_valid(uint64_t virtual_address);
int address_space_user_range_valid(uint64_t virtual_address, size_t length);
uint32_t address_space_user_l2_index(uint64_t virtual_address);
uint32_t address_space_user_l3_index(uint64_t virtual_address);
int address_space_prepare_l2_templates(uint64_t *device_l2, uint64_t *ram_l2,
                                       size_t entry_count);
int address_space_user_descriptor_matches(uint64_t descriptor,
                                          uint64_t physical_address,
                                          address_space_user_permissions_t permissions);

/* Roots share EL1-only kernel/device mappings and keep private EL0 user mappings.
 * The scheduler can activate task roots and return into isolated EL0 application contexts.
 * Because ASID support is not enabled, every switch performs a full stage-1 TLB flush. */
address_space_error_t address_space_create(address_space_handle_t *out);
address_space_error_t address_space_activate(address_space_handle_t handle);
address_space_error_t address_space_deactivate(void);
/* Scheduler-only address-space transitions. These are valid only while the scheduler
 * is running and are called with local IRQs masked (including from the timer IRQ path).
 * Every root retains the same privileged kernel mappings. */
address_space_error_t address_space_scheduler_activate(address_space_handle_t handle);
address_space_error_t address_space_scheduler_activate_kernel(void);
/* Task references pin a space so it cannot be destroyed while a scheduler task names it. */
address_space_error_t address_space_task_retain(address_space_handle_t handle);
address_space_error_t address_space_task_release(address_space_handle_t handle);
int address_space_is_active(address_space_handle_t handle);
int address_space_activation_self_test(void);
address_space_error_t address_space_destroy(address_space_handle_t handle);
/* Allocates a zero-filled, address-space-owned data frame from PMM. */
address_space_error_t address_space_alloc_user_page(address_space_handle_t handle, uint64_t *physical_address_out);
/* Frees an owned frame only after it has been unmapped. */
address_space_error_t address_space_free_user_page(address_space_handle_t handle, uint64_t physical_address);
address_space_error_t address_space_map_user_page(address_space_handle_t handle,
                                                   uint64_t virtual_address,
                                                   uint64_t physical_address,
                                                   address_space_user_permissions_t permissions);
address_space_error_t address_space_unmap_user_page(address_space_handle_t handle,
                                                     uint64_t virtual_address,
                                                     uint64_t *physical_address_out);
/* Atomic, allocation-free changes to at most 256 already-owned normal pages.
 * Protection never promotes data to executable. Caller must establish its
 * anonymous-region ownership/pin policy before invoking these primitives. */
address_space_error_t address_space_protect_user_range(address_space_handle_t,
                                                       uint64_t, size_t,
                                                       address_space_user_permissions_t);
address_space_error_t address_space_discard_user_range(address_space_handle_t,
                                                       uint64_t, size_t);
int address_space_query(address_space_handle_t handle, uint64_t virtual_address,
                        vmm_mapping_info_t *out);
/* Copies only from currently mapped EL0-accessible normal pages owned by handle. */
int address_space_copy_from_user(address_space_handle_t handle, void *destination,
                                 uint64_t user_source, size_t length);
int address_space_copy_to_user(address_space_handle_t handle, uint64_t user_destination,
                               const void *source, size_t length);
int address_space_get_info(address_space_handle_t handle, address_space_info_t *out);
size_t address_space_count(void);
int address_space_get_info_at(size_t index, address_space_info_t *out);
const char *address_space_error_string(address_space_error_t error);
int address_space_self_test(void);

#endif

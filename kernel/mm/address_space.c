#include <stddef.h>
#include <stdint.h>
#include "chos/address_space.h"
#include "chos/pmm.h"
#include "chos/platform.h"
#include "chos/spinlock.h"
#include "chos/scheduler.h"

#define DESC_VALID       (1ull << 0)
#define DESC_TABLE_PAGE  3ull
#define DESC_ADDR_MASK   0x0000fffffffff000ull
#define ATTRIDX_SHIFT    2u
#define ATTRIDX_MASK     (7ull << ATTRIDX_SHIFT)
#define AP_SHIFT         6u
#define AP_MASK          (3ull << AP_SHIFT)
#define DESC_PXN         (1ull << 53)
#define DESC_UXN         (1ull << 54)
#define ADDRESS_L2_COUNT 512u

/* Tables are physical pages allocated from PMM and currently identity-mapped.
 * The L2 templates share immutable kernel/device L3 tables; user L3 tables are private. */
typedef struct {
    uint64_t physical_address;
    uint64_t mapped_virtual_address; /* zero means owned but currently unmapped */
} address_space_user_frame_t;

typedef struct {
    int live;
    uint32_t id;
    uint32_t generation;
    uint32_t mapped_user_pages;
    uint32_t owned_user_frames;
    uint32_t task_references;
    uint64_t root_pa;
    uint64_t device_l2_pa;
    uint64_t ram_l2_pa;
    uint64_t low_l1_pa;
    uint64_t user_l3_pa[CHOS_AS_USER_L2_SLOTS];
    address_space_user_frame_t user_frames[CHOS_AS_MAX_USER_FRAMES];
} address_space_slot_t;

static address_space_slot_t spaces[CHOS_AS_MAX_SPACES];
static uint32_t slot_generations[CHOS_AS_MAX_SPACES];
static uint32_t next_address_space_id = 1u;
static chos_spinlock_t address_space_lock = CHOS_SPINLOCK_INITIALIZER;
static int32_t active_address_space_slot = -1;

extern uint8_t __text_start;

static void zero_bytes(void *memory, size_t length) {
    uint8_t *bytes = (uint8_t *)memory;
    size_t i;
    for (i = 0u; i < length; ++i) bytes[i] = 0u;
}

static int handle_slot(address_space_handle_t handle, address_space_slot_t **out) {
    address_space_slot_t *slot;
    if (out == NULL || handle.slot >= CHOS_AS_MAX_SPACES || handle.generation == 0u || handle.id == 0u) return 0;
    slot = &spaces[handle.slot];
    if (!slot->live || slot->generation != handle.generation || slot->id != handle.id) return 0;
    *out = slot;
    return 1;
}

static address_space_error_t switch_address_space_root(int32_t target_slot,
                                                        address_space_handle_t handle,
                                                        int scheduler_owned) {
    address_space_slot_t *slot = NULL;
    uint64_t irq_flags, expected_root, current_root, target_root;
    vmm_stats_t vmm_stats;

    if (scheduler_owned) {
        if (!scheduler_is_running()) return AS_ERR_SWITCH_FAILED;
    } else if (scheduler_is_running()) {
        return AS_ERR_SWITCH_FAILED;
    }
    if (!vmm_is_enabled()) return AS_ERR_VMM_NOT_READY;

    irq_flags = chos_spin_lock_irqsave(&address_space_lock);
    if (target_slot >= 0 && !handle_slot(handle, &slot)) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_BAD_HANDLE;
    }
    current_root = vmm_current_root_table_address();
    expected_root = active_address_space_slot < 0
        ? vmm_kernel_root_table_address()
        : spaces[(uint32_t)active_address_space_slot].root_pa;
    target_root = target_slot < 0 ? vmm_kernel_root_table_address() : slot->root_pa;

    /* Refuse to layer software state over an unexpected hardware root. */
    if (current_root != expected_root ||
        (target_slot >= 0 && !pmm_is_allocated_page(target_root))) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_SWITCH_FAILED;
    }
    if (current_root != target_root) {
        /* VMM aliases exist only in the canonical kernel root. Never switch away
         * while a caller-owned dynamic alias could still be in use. */
        vmm_get_stats(&vmm_stats);
        if (vmm_stats.dynamic_mappings != 0u || !vmm_switch_root_table(target_root)) {
            chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
            return AS_ERR_SWITCH_FAILED;
        }
    }
    active_address_space_slot = target_slot;
    chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
    return AS_OK;
}

address_space_error_t address_space_activate(address_space_handle_t handle) {
    if (handle.slot >= CHOS_AS_MAX_SPACES) return AS_ERR_BAD_HANDLE;
    return switch_address_space_root((int32_t)handle.slot, handle, 0);
}

address_space_error_t address_space_deactivate(void) {
    return switch_address_space_root(-1, (address_space_handle_t){0u, 0u, 0u}, 0);
}

address_space_error_t address_space_scheduler_activate(address_space_handle_t handle) {
    if (handle.slot >= CHOS_AS_MAX_SPACES) return AS_ERR_BAD_HANDLE;
    return switch_address_space_root((int32_t)handle.slot, handle, 1);
}

address_space_error_t address_space_scheduler_activate_kernel(void) {
    return switch_address_space_root(-1, (address_space_handle_t){0u, 0u, 0u}, 1);
}

address_space_error_t address_space_task_retain(address_space_handle_t handle) {
    address_space_slot_t *slot;
    uint64_t irq_flags = chos_spin_lock_irqsave(&address_space_lock);
    if (!handle_slot(handle, &slot)) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_BAD_HANDLE;
    }
    if (slot->task_references == UINT32_MAX) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_REFERENCED;
    }
    ++slot->task_references;
    chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
    return AS_OK;
}

address_space_error_t address_space_task_release(address_space_handle_t handle) {
    address_space_slot_t *slot;
    uint64_t irq_flags = chos_spin_lock_irqsave(&address_space_lock);
    if (!handle_slot(handle, &slot)) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_BAD_HANDLE;
    }
    if (slot->task_references == 0u) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_BAD_REFERENCE;
    }
    --slot->task_references;
    chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
    return AS_OK;
}

int address_space_is_active(address_space_handle_t handle) {
    address_space_slot_t *slot;
    uint64_t irq_flags = chos_spin_lock_irqsave(&address_space_lock);
    int result = handle_slot(handle, &slot) &&
                 active_address_space_slot == (int32_t)handle.slot &&
                 vmm_current_root_table_address() == slot->root_pa;
    chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
    return result;
}

static void fill_info(size_t index, const address_space_slot_t *slot, address_space_info_t *out) {
    size_t i;
    uint32_t table_count = 0u;
    for (i = 0u; i < CHOS_AS_USER_L2_SLOTS; ++i) {
        if (slot->user_l3_pa[i] != 0u) ++table_count;
    }
    out->live = slot->live;
    out->slot = (uint32_t)index;
    out->id = slot->id;
    out->generation = slot->generation;
    out->mapped_user_pages = slot->mapped_user_pages;
    out->owned_user_frames = slot->owned_user_frames;
    out->user_l3_tables = table_count;
    out->task_references = slot->task_references;
    out->root_table_physical = slot->root_pa;
}

static int query_root(const address_space_slot_t *slot, uint64_t virtual_address,
                      vmm_mapping_info_t *out) {
    return slot && vmm_query_root_mapping(slot->root_pa,virtual_address,out);
}

static int find_owned_frame(address_space_slot_t *slot, uint64_t physical_address,
                            uint32_t *index_out) {
    uint32_t i;
    if (slot == NULL) return 0;
    for (i = 0u; i < slot->owned_user_frames; ++i) {
        if (slot->user_frames[i].physical_address == physical_address) {
            if (index_out != NULL) *index_out = i;
            return 1;
        }
    }
    return 0;
}

static int backing_page_is_suitable(address_space_slot_t *slot, uint64_t physical_address,
                                    uint32_t *owned_index) {
    vmm_mapping_info_t backing;
    return find_owned_frame(slot, physical_address, owned_index) &&
           pmm_is_allocated_page(physical_address) &&
           vmm_query_mapping(physical_address, &backing) &&
           backing.physical_address == physical_address &&
           backing.memory_type == VMM_MEMORY_NORMAL && backing.writable &&
           !backing.executable_at_el1 && !backing.accessible_at_el0;
}

address_space_error_t address_space_create(address_space_handle_t *out) {
    uint64_t irq_flags, root_pa = 0u, device_pa = 0u, ram_pa = 0u, low_pa = 0u;
    uint64_t *root, *device_l2, *ram_l2;
    size_t index;
    uint32_t generation, id;

    if (out == NULL) return AS_ERR_ARGUMENT;
    out->slot = out->generation = out->id = 0u;
    if (!vmm_is_enabled()) return AS_ERR_VMM_NOT_READY;

    irq_flags = chos_spin_lock_irqsave(&address_space_lock);
    for (index = 0u; index < CHOS_AS_MAX_SPACES; ++index) if (!spaces[index].live) break;
    if (index == CHOS_AS_MAX_SPACES) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_CAPACITY;
    }
    if (!pmm_alloc_page(&root_pa) || !pmm_alloc_page(&device_pa) || !pmm_alloc_page(&ram_pa) ||
        (vmm_uefi_profile() && !pmm_alloc_page(&low_pa))) {
        if (low_pa != 0u) (void)pmm_free_page(low_pa);
        if (ram_pa != 0u) (void)pmm_free_page(ram_pa);
        if (device_pa != 0u) (void)pmm_free_page(device_pa);
        if (root_pa != 0u) (void)pmm_free_page(root_pa);
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_OUT_OF_MEMORY;
    }

    root = (uint64_t *)(uintptr_t)root_pa;
    device_l2 = (uint64_t *)(uintptr_t)device_pa;
    ram_l2 = (uint64_t *)(uintptr_t)ram_pa;
    zero_bytes(root, VMM_PAGE_SIZE);
    zero_bytes(device_l2, VMM_PAGE_SIZE);
    zero_bytes(ram_l2, VMM_PAGE_SIZE);
    if (!vmm_copy_kernel_root_template(root,512u) ||
        !vmm_copy_kernel_l2_template(0u, device_l2, ADDRESS_L2_COUNT) ||
        !vmm_copy_kernel_l2_template(1u, ram_l2, ADDRESS_L2_COUNT) ||
        !address_space_prepare_l2_templates(device_l2, ram_l2, ADDRESS_L2_COUNT)) {
        if (low_pa) (void)pmm_free_page(low_pa);
        (void)pmm_free_page(ram_pa);
        (void)pmm_free_page(device_pa);
        (void)pmm_free_page(root_pa);
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_KERNEL_TEMPLATE;
    }

    uint64_t *low=root;
    if(low_pa){
        low=(uint64_t *)(uintptr_t)low_pa;
        if(!vmm_copy_kernel_low_l1_template(low,512u)){
            (void)pmm_free_page(low_pa);(void)pmm_free_page(ram_pa);
            (void)pmm_free_page(device_pa);(void)pmm_free_page(root_pa);
            chos_spin_unlock_irqrestore(&address_space_lock,irq_flags);return AS_ERR_KERNEL_TEMPLATE;
        }
        root[0]=vmm_make_table_descriptor(low_pa);
    }
    low[0]=vmm_make_table_descriptor(device_pa);low[1]=vmm_make_table_descriptor(ram_pa);
    if(!low[0] || !low[1] || !root[0]){
        if(low_pa)(void)pmm_free_page(low_pa);
        (void)pmm_free_page(ram_pa);(void)pmm_free_page(device_pa);(void)pmm_free_page(root_pa);
        chos_spin_unlock_irqrestore(&address_space_lock,irq_flags);return AS_ERR_BAD_DESCRIPTOR;
    }
    vmm_clean_page_table_range(low,VMM_PAGE_SIZE);

    generation = slot_generations[index] + 1u;
    if (generation == 0u) generation = 1u;
    slot_generations[index] = generation;
    id = next_address_space_id++;
    if (next_address_space_id == 0u) next_address_space_id = 1u;
    zero_bytes(&spaces[index], sizeof(spaces[index]));
    spaces[index].live = 1;
    spaces[index].id = id;
    spaces[index].generation = generation;
    spaces[index].root_pa = root_pa;
    spaces[index].device_l2_pa = device_pa;
    spaces[index].ram_l2_pa = ram_pa;
    spaces[index].low_l1_pa = low_pa;
    vmm_clean_page_table_range(root, VMM_PAGE_SIZE);
    vmm_clean_page_table_range(device_l2, VMM_PAGE_SIZE);
    vmm_clean_page_table_range(ram_l2, VMM_PAGE_SIZE);
    out->slot = (uint32_t)index;
    out->generation = generation;
    out->id = id;
    chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
    return AS_OK;
}

address_space_error_t address_space_alloc_user_page(address_space_handle_t handle,
                                                       uint64_t *physical_address_out) {
    address_space_slot_t *slot;
    uint64_t irq_flags, physical_address = 0u;
    if (physical_address_out == NULL) return AS_ERR_ARGUMENT;
    *physical_address_out = 0u;
    irq_flags = chos_spin_lock_irqsave(&address_space_lock);
    if (!handle_slot(handle, &slot)) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_BAD_HANDLE;
    }
    if (slot->owned_user_frames >= CHOS_AS_MAX_USER_FRAMES) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_FRAME_CAPACITY;
    }
    if (!pmm_alloc_page(&physical_address)) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_OUT_OF_MEMORY;
    }
    /* Never expose stale kernel/previous-process contents to EL0. */
    zero_bytes((void *)(uintptr_t)physical_address, VMM_PAGE_SIZE);
    vmm_clean_page_table_range((const void *)(uintptr_t)physical_address, VMM_PAGE_SIZE);
    slot->user_frames[slot->owned_user_frames].physical_address = physical_address;
    slot->user_frames[slot->owned_user_frames].mapped_virtual_address = 0u;
    ++slot->owned_user_frames;
    *physical_address_out = physical_address;
    chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
    return AS_OK;
}

address_space_error_t address_space_free_user_page(address_space_handle_t handle,
                                                    uint64_t physical_address) {
    address_space_slot_t *slot;
    uint64_t irq_flags;
    uint32_t index;
    if ((physical_address & (VMM_PAGE_SIZE - 1u)) != 0u) return AS_ERR_ALIGNMENT;
    irq_flags = chos_spin_lock_irqsave(&address_space_lock);
    if (!handle_slot(handle, &slot)) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_BAD_HANDLE;
    }
    if (!find_owned_frame(slot, physical_address, &index)) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_PAGE_NOT_OWNED;
    }
    if (slot->user_frames[index].mapped_virtual_address != 0u) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_PAGE_MAPPED;
    }
    zero_bytes((void *)(uintptr_t)physical_address, VMM_PAGE_SIZE);
    vmm_clean_page_table_range((const void *)(uintptr_t)physical_address, VMM_PAGE_SIZE);
    if (!pmm_free_page(physical_address)) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_BAD_DESCRIPTOR;
    }
    --slot->owned_user_frames;
    slot->user_frames[index] = slot->user_frames[slot->owned_user_frames];
    slot->user_frames[slot->owned_user_frames].physical_address = 0u;
    slot->user_frames[slot->owned_user_frames].mapped_virtual_address = 0u;
    chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
    return AS_OK;
}

address_space_error_t address_space_map_user_page(address_space_handle_t handle,
                                                   uint64_t virtual_address,
                                                   uint64_t physical_address,
                                                   address_space_user_permissions_t permissions) {
    address_space_slot_t *slot;
    uint64_t irq_flags, *device_l2, *l3, descriptor;
    uint32_t l2_index, l3_index, owned_index = 0u;
    size_t relative_l2;
    uint64_t l3_pa;

    if (!address_space_user_va_valid(virtual_address)) {
        return ((virtual_address & (VMM_PAGE_SIZE - 1u)) != 0u) ? AS_ERR_ALIGNMENT : AS_ERR_VA_RANGE;
    }
    if ((physical_address & (VMM_PAGE_SIZE - 1u)) != 0u) return AS_ERR_ALIGNMENT;
    if (permissions != AS_PERM_USER_RO && permissions != AS_PERM_USER_RW &&
        permissions != AS_PERM_USER_RX) return AS_ERR_ARGUMENT;

    irq_flags = chos_spin_lock_irqsave(&address_space_lock);
    if (!handle_slot(handle, &slot)) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_BAD_HANDLE;
    }
    if (!backing_page_is_suitable(slot, physical_address, &owned_index)) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_PAGE_NOT_OWNED;
    }
    if (slot->user_frames[owned_index].mapped_virtual_address != 0u) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_MAPPING_CONFLICT;
    }
    l2_index = address_space_user_l2_index(virtual_address);
    l3_index = address_space_user_l3_index(virtual_address);
    relative_l2 = (size_t)(l2_index - CHOS_AS_USER_L2_FIRST);
    device_l2 = (uint64_t *)(uintptr_t)slot->device_l2_pa;
    l3_pa = slot->user_l3_pa[relative_l2];

    if (l3_pa == 0u) {
        if (device_l2[l2_index] != 0u) {
            chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
            return AS_ERR_MAPPING_CONFLICT;
        }
        if (!pmm_alloc_page(&l3_pa)) {
            chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
            return AS_ERR_OUT_OF_MEMORY;
        }
        l3 = (uint64_t *)(uintptr_t)l3_pa;
        zero_bytes(l3, VMM_PAGE_SIZE);
        descriptor = vmm_make_table_descriptor(l3_pa);
        if (descriptor == 0u) {
            (void)pmm_free_page(l3_pa);
            chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
            return AS_ERR_BAD_DESCRIPTOR;
        }
        device_l2[l2_index] = descriptor;
        vmm_clean_page_table_range(l3, VMM_PAGE_SIZE);
        vmm_clean_page_table_range(&device_l2[l2_index], sizeof(device_l2[l2_index]));
        slot->user_l3_pa[relative_l2] = l3_pa;
    } else if (device_l2[l2_index] != vmm_make_table_descriptor(l3_pa)) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_BAD_DESCRIPTOR;
    }

    l3 = (uint64_t *)(uintptr_t)l3_pa;
    if ((l3[l3_index] & DESC_VALID) != 0u) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_MAPPING_CONFLICT;
    }
    descriptor = permissions == AS_PERM_USER_RX
        ? vmm_make_user_code_page_descriptor(physical_address)
        : vmm_make_user_page_descriptor(physical_address, permissions == AS_PERM_USER_RW);
    if (descriptor == 0u || !address_space_user_descriptor_matches(descriptor, physical_address, permissions)) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_BAD_DESCRIPTOR;
    }
    l3[l3_index] = descriptor;
    vmm_clean_page_table_range(&l3[l3_index], sizeof(l3[l3_index]));
    /* All roots currently use ASID 0. Invalidate this VA whenever its root is live;
       switches also flush the complete EL1 stage-1 TLB before returning. */
    if (vmm_current_root_table_address() == slot->root_pa)
        vmm_tlb_invalidate_page(virtual_address);
    ++slot->mapped_user_pages;
    slot->user_frames[owned_index].mapped_virtual_address = virtual_address;
    chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
    return AS_OK;
}

address_space_error_t address_space_unmap_user_page(address_space_handle_t handle,
                                                     uint64_t virtual_address,
                                                     uint64_t *physical_address_out) {
    address_space_slot_t *slot;
    uint64_t irq_flags, *l3, descriptor, l3_pa;
    uint32_t l2_index, l3_index, owned_index = 0u;
    size_t relative_l2;
    if (!address_space_user_va_valid(virtual_address)) {
        return ((virtual_address & (VMM_PAGE_SIZE - 1u)) != 0u) ? AS_ERR_ALIGNMENT : AS_ERR_VA_RANGE;
    }
    irq_flags = chos_spin_lock_irqsave(&address_space_lock);
    if (!handle_slot(handle, &slot)) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_BAD_HANDLE;
    }
    l2_index = address_space_user_l2_index(virtual_address);
    l3_index = address_space_user_l3_index(virtual_address);
    relative_l2 = (size_t)(l2_index - CHOS_AS_USER_L2_FIRST);
    l3_pa = slot->user_l3_pa[relative_l2];
    if (l3_pa == 0u) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_PAGE_NOT_MAPPED;
    }
    l3 = (uint64_t *)(uintptr_t)l3_pa;
    descriptor = l3[l3_index];
    if ((descriptor & DESC_TABLE_PAGE) != DESC_TABLE_PAGE) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_PAGE_NOT_MAPPED;
    }
    if (!find_owned_frame(slot, descriptor & DESC_ADDR_MASK, &owned_index) ||
        slot->user_frames[owned_index].mapped_virtual_address != virtual_address) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_BAD_DESCRIPTOR;
    }
    if (physical_address_out != NULL) *physical_address_out = descriptor & DESC_ADDR_MASK;
    l3[l3_index] = 0u;
    vmm_clean_page_table_range(&l3[l3_index], sizeof(l3[l3_index]));
    if (vmm_current_root_table_address() == slot->root_pa)
        vmm_tlb_invalidate_page(virtual_address);
    slot->user_frames[owned_index].mapped_virtual_address = 0u;
    if (slot->mapped_user_pages > 0u) --slot->mapped_user_pages;

    /* With ASID 0, a VA invalidation is sufficient while this root is active; a later
       root switch flushes all EL1 stage-1 translations before the root is reused. */
    chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
    return AS_OK;
}

/* Single-CPU, IRQ-masked transactions: validate the complete range before any
 * PTE/data changes. No allocations, scheduling or partial successful result. */
typedef struct { uint64_t *pte, physical; } user_range_page_t;
#define USER_RANGE_MAX_PAGES 256u
static address_space_error_t collect_owned_data_range(address_space_handle_t handle,
        uint64_t start, size_t bytes, address_space_slot_t **owner_out,
        user_range_page_t *pages, uint32_t *count_out) {
    address_space_slot_t *slot;
    if (!bytes || bytes > USER_RANGE_MAX_PAGES * VMM_PAGE_SIZE ||
        (bytes & (VMM_PAGE_SIZE-1u)) || (start & (VMM_PAGE_SIZE-1u)) ||
        !address_space_user_range_valid(start, bytes)) return AS_ERR_ARGUMENT;
    if (!handle_slot(handle, &slot)) return AS_ERR_BAD_HANDLE;
    uint32_t count=(uint32_t)(bytes/VMM_PAGE_SIZE);
    for (uint32_t i=0;i<count;i++) {
        uint64_t va=start+(uint64_t)i*VMM_PAGE_SIZE;
        uint64_t pa=slot->user_l3_pa[address_space_user_l2_index(va)-CHOS_AS_USER_L2_FIRST];
        if (!pa) return AS_ERR_PAGE_NOT_MAPPED;
        uint64_t *l2=(uint64_t *)(uintptr_t)slot->device_l2_pa;
        if(l2[address_space_user_l2_index(va)]!=vmm_make_table_descriptor(pa))return AS_ERR_BAD_DESCRIPTOR;
        uint64_t *pte=&((uint64_t *)(uintptr_t)pa)[address_space_user_l3_index(va)];
        uint64_t physical=*pte&DESC_ADDR_MASK;uint32_t owned_index;
        if (!find_owned_frame(slot,physical,&owned_index) ||
            slot->user_frames[owned_index].mapped_virtual_address!=va ||
            !(address_space_user_descriptor_matches(*pte,physical,AS_PERM_USER_RW) ||
              address_space_user_descriptor_matches(*pte,physical,AS_PERM_USER_RO) ||
              address_space_user_descriptor_matches(*pte,physical,AS_PERM_USER_NONE)))
            return AS_ERR_BAD_DESCRIPTOR;
        pages[i]=(user_range_page_t){.pte=pte,.physical=physical};
    }
    *owner_out=slot;*count_out=count;return AS_OK;
}
address_space_error_t address_space_protect_user_range(address_space_handle_t handle,
        uint64_t start,size_t bytes,address_space_user_permissions_t permissions) {
    if (permissions!=AS_PERM_USER_RO && permissions!=AS_PERM_USER_RW &&
        permissions!=AS_PERM_USER_NONE) return AS_ERR_ARGUMENT;
    user_range_page_t pages[USER_RANGE_MAX_PAGES];address_space_slot_t *slot;uint32_t count;
    uint64_t irq=chos_spin_lock_irqsave(&address_space_lock);
    address_space_error_t error=collect_owned_data_range(handle,start,bytes,&slot,pages,&count);
    if (error!=AS_OK) {chos_spin_unlock_irqrestore(&address_space_lock,irq);return error;}
    int active=vmm_current_root_table_address()==slot->root_pa;
    /* Break before make, with cache maintenance and architected TLB barriers.
     * The data frames/ownership counts remain intact even for PROT_NONE. */
    for (uint32_t i=0;i<count;i++) {
        *pages[i].pte=0;vmm_clean_page_table_range(pages[i].pte,sizeof(uint64_t));
        if (active) vmm_tlb_invalidate_page(start+(uint64_t)i*VMM_PAGE_SIZE);
    }
    for (uint32_t i=0;i<count;i++) {
        *pages[i].pte=permissions==AS_PERM_USER_NONE
            ?vmm_make_user_inaccessible_page_descriptor(pages[i].physical)
            :vmm_make_user_page_descriptor(pages[i].physical,permissions==AS_PERM_USER_RW);
        vmm_clean_page_table_range(pages[i].pte,sizeof(uint64_t));
        if (active) vmm_tlb_invalidate_page(start+(uint64_t)i*VMM_PAGE_SIZE);
    }
    chos_spin_unlock_irqrestore(&address_space_lock,irq);return AS_OK;
}
address_space_error_t address_space_discard_user_range(address_space_handle_t handle,
        uint64_t start,size_t bytes) {
    user_range_page_t pages[USER_RANGE_MAX_PAGES];address_space_slot_t *slot;uint32_t count;
    uint64_t irq=chos_spin_lock_irqsave(&address_space_lock);
    address_space_error_t error=collect_owned_data_range(handle,start,bytes,&slot,pages,&count);
    if (error==AS_OK) for (uint32_t i=0;i<count;i++) {
        void *alias=(void *)(uintptr_t)pages[i].physical;
        zero_bytes(alias,VMM_PAGE_SIZE);vmm_clean_page_table_range(alias,VMM_PAGE_SIZE);
    }
    chos_spin_unlock_irqrestore(&address_space_lock,irq);return error;
}

address_space_error_t address_space_destroy(address_space_handle_t handle) {
    address_space_slot_t *slot;
    uint64_t irq_flags, root_pa, device_pa, ram_pa, low_pa;
    size_t i;
    irq_flags = chos_spin_lock_irqsave(&address_space_lock);
    if (!handle_slot(handle, &slot)) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_BAD_HANDLE;
    }
    if (slot->task_references != 0u) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_REFERENCED;
    }
    if (active_address_space_slot == (int32_t)handle.slot ||
        vmm_current_root_table_address() == slot->root_pa) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_ACTIVE;
    }
    if (slot->mapped_user_pages != 0u) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_HAS_MAPPINGS;
    }
    root_pa = slot->root_pa;
    device_pa = slot->device_l2_pa;
    ram_pa = slot->ram_l2_pa;
    low_pa = slot->low_l1_pa;
    for (i = 0u; i < CHOS_AS_USER_L2_SLOTS; ++i) {
        if (slot->user_l3_pa[i] != 0u && !pmm_is_allocated_page(slot->user_l3_pa[i])) {
            chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
            return AS_ERR_BAD_DESCRIPTOR;
        }
    }
    for (i = 0u; i < slot->owned_user_frames; ++i) {
        if (slot->user_frames[i].mapped_virtual_address != 0u ||
            !pmm_is_allocated_page(slot->user_frames[i].physical_address)) {
            chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
            return AS_ERR_BAD_DESCRIPTOR;
        }
    }
    if (!pmm_is_allocated_page(root_pa) || !pmm_is_allocated_page(device_pa) ||
        !pmm_is_allocated_page(ram_pa) || (low_pa && !pmm_is_allocated_page(low_pa))) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return AS_ERR_BAD_DESCRIPTOR;
    }

    for (i = 0u; i < CHOS_AS_USER_L2_SLOTS; ++i) {
        uint64_t l3_pa = slot->user_l3_pa[i];
        if (l3_pa != 0u) (void)pmm_free_page(l3_pa);
    }
    for (i = 0u; i < slot->owned_user_frames; ++i) {
        uint64_t user_pa = slot->user_frames[i].physical_address;
        zero_bytes((void *)(uintptr_t)user_pa, VMM_PAGE_SIZE);
        vmm_clean_page_table_range((const void *)(uintptr_t)user_pa, VMM_PAGE_SIZE);
        (void)pmm_free_page(user_pa);
    }
    slot->live = 0;
    slot->id = 0u;
    slot->root_pa = 0u;
    slot->device_l2_pa = 0u;
    slot->ram_l2_pa = 0u;
    slot->low_l1_pa = 0u;
    slot->mapped_user_pages = 0u;
    slot->owned_user_frames = 0u;
    zero_bytes(slot->user_l3_pa, sizeof(slot->user_l3_pa));
    zero_bytes(slot->user_frames, sizeof(slot->user_frames));
    if(low_pa)(void)pmm_free_page(low_pa);
    (void)pmm_free_page(ram_pa);
    (void)pmm_free_page(device_pa);
    (void)pmm_free_page(root_pa);
    chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
    return AS_OK;
}

int address_space_query(address_space_handle_t handle, uint64_t virtual_address,
                        vmm_mapping_info_t *out) {
    address_space_slot_t *slot;
    uint64_t irq_flags;
    int result;
    if (out == NULL) return 0;
    irq_flags = chos_spin_lock_irqsave(&address_space_lock);
    result = handle_slot(handle, &slot) && query_root(slot, virtual_address, out);
    chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
    return result;
}

int address_space_copy_from_user(address_space_handle_t handle, void *destination,
                                 uint64_t user_source, size_t length) {
    uint8_t *dst = (uint8_t *)destination;
    size_t copied = 0u;
    if (length == 0u) return 1;
    if (dst == NULL || !address_space_user_range_valid(user_source, length)) return 0;

    while (copied < length) {
        uint64_t va = user_source + (uint64_t)copied;
        vmm_mapping_info_t mapping;
        size_t offset = (size_t)(va & (VMM_PAGE_SIZE - 1u));
        size_t chunk = VMM_PAGE_SIZE - offset;
        size_t i;
        const uint8_t *src;

        if (chunk > length - copied) chunk = length - copied;
        if (!address_space_query(handle, va, &mapping) ||
            !mapping.accessible_at_el0 || mapping.memory_type != VMM_MEMORY_NORMAL) {
            return 0;
        }
        /* User pages are PMM-owned and the kernel identity map remains present in
           every root. Copy by the validated physical alias, not by trusting a user VA
           to be directly dereferenceable in EL1. */
        src = (const uint8_t *)(uintptr_t)mapping.physical_address;
        for (i = 0u; i < chunk; ++i) dst[copied + i] = src[i];
        copied += chunk;
    }
    return 1;
}

int address_space_copy_to_user(address_space_handle_t handle, uint64_t user_destination,
                               const void *source, size_t length) {
    const uint8_t *src = (const uint8_t *)source;
    size_t copied = 0u;
    if (length == 0u) return 1;
    if (src == NULL || !address_space_user_range_valid(user_destination, length)) return 0;
    while (copied < length) {
        uint64_t va = user_destination + (uint64_t)copied;
        vmm_mapping_info_t mapping;
        size_t offset = (size_t)(va & (VMM_PAGE_SIZE - 1u));
        size_t chunk = VMM_PAGE_SIZE - offset;
        size_t i;
        uint8_t *dst;
        if (chunk > length - copied) chunk = length - copied;
        if (!address_space_query(handle, va, &mapping) ||
            !mapping.accessible_at_el0 || !mapping.writable ||
            mapping.memory_type != VMM_MEMORY_NORMAL) return 0;
        dst = (uint8_t *)(uintptr_t)mapping.physical_address;
        for (i = 0u; i < chunk; ++i) dst[i] = src[copied + i];
        vmm_clean_page_table_range(dst, chunk);
        copied += chunk;
    }
    __asm__ volatile("dsb sy" ::: "memory");
    return 1;
}

int address_space_get_info(address_space_handle_t handle, address_space_info_t *out) {
    address_space_slot_t *slot;
    uint64_t irq_flags;
    if (out == NULL) return 0;
    irq_flags = chos_spin_lock_irqsave(&address_space_lock);
    if (!handle_slot(handle, &slot)) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return 0;
    }
    fill_info(handle.slot, slot, out);
    chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
    return 1;
}

size_t address_space_count(void) {
    uint64_t irq_flags = chos_spin_lock_irqsave(&address_space_lock);
    size_t i, count = 0u;
    for (i = 0u; i < CHOS_AS_MAX_SPACES; ++i) if (spaces[i].live) ++count;
    chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
    return count;
}

int address_space_get_info_at(size_t index, address_space_info_t *out) {
    uint64_t irq_flags;
    if (index >= CHOS_AS_MAX_SPACES || out == NULL) return 0;
    irq_flags = chos_spin_lock_irqsave(&address_space_lock);
    if (!spaces[index].live) {
        chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
        return 0;
    }
    fill_info(index, &spaces[index], out);
    chos_spin_unlock_irqrestore(&address_space_lock, irq_flags);
    return 1;
}

int address_space_self_test(void) {
    address_space_handle_t first = {0u, 0u, 0u};
    address_space_handle_t second = {0u, 0u, 0u};
    address_space_info_t info;
    vmm_mapping_info_t map;
    pmm_stats_t before, after;
    uint64_t page_rw = 0u, page_ro = 0u, page_peer = 0u, returned = 0u;
    uint64_t text_page = 0u;
    volatile uint64_t *rw_payload, *ro_payload, *peer_payload;
    int first_created = 0, second_created = 0;
    int first_rw = 0, first_ro = 0, second_page = 0;
    int mapped_rw = 0, mapped_ro = 0, mapped_peer = 0;
    int ok = 0;

    if (!vmm_is_enabled() ||
        vmm_current_root_table_address() != vmm_kernel_root_table_address() ||
        active_address_space_slot != -1) return 0;
    pmm_get_stats(&before);
    if (address_space_create(&first) != AS_OK) goto cleanup;
    first_created = 1;
    if (address_space_create(&second) != AS_OK) goto cleanup;
    second_created = 1;
    if (address_space_alloc_user_page(first, &page_rw) != AS_OK) goto cleanup;
    first_rw = 1;
    if (address_space_alloc_user_page(first, &page_ro) != AS_OK) goto cleanup;
    first_ro = 1;
    if (address_space_alloc_user_page(second, &page_peer) != AS_OK) goto cleanup;
    second_page = 1;

    /* Fresh user frames must not disclose stale RAM contents. */
    rw_payload = (volatile uint64_t *)(uintptr_t)page_rw;
    ro_payload = (volatile uint64_t *)(uintptr_t)page_ro;
    peer_payload = (volatile uint64_t *)(uintptr_t)page_peer;
    if (rw_payload[0] != 0u || rw_payload[(VMM_PAGE_SIZE / sizeof(uint64_t)) - 1u] != 0u ||
        ro_payload[0] != 0u || peer_payload[0] != 0u) goto cleanup;

    if (address_space_map_user_page(first, CHOS_AS_USER_VA_BASE, page_rw, AS_PERM_USER_RW) != AS_OK) goto cleanup;
    mapped_rw = 1;
    if (address_space_map_user_page(first, CHOS_AS_USER_VA_BASE + VMM_PAGE_SIZE, page_ro, AS_PERM_USER_RO) != AS_OK) goto cleanup;
    mapped_ro = 1;
    /* Identical VAs in different roots intentionally map different physical frames. */
    if (address_space_map_user_page(second, CHOS_AS_USER_VA_BASE, page_peer, AS_PERM_USER_RW) != AS_OK) goto cleanup;
    mapped_peer = 1;

    *rw_payload = 0x43484f5355534552ull;
    *peer_payload = 0x5052495641544500ull;
    if (*rw_payload == *peer_payload) goto cleanup;
    if (!address_space_query(first, CHOS_AS_USER_VA_BASE, &map) ||
        map.physical_address != page_rw || map.memory_type != VMM_MEMORY_NORMAL ||
        !map.writable || !map.accessible_at_el0 || map.executable_at_el1 || map.executable_at_el0) goto cleanup;
    if (!address_space_query(first, CHOS_AS_USER_VA_BASE + VMM_PAGE_SIZE, &map) ||
        map.physical_address != page_ro || map.writable || !map.accessible_at_el0 ||
        map.executable_at_el1 || map.executable_at_el0) goto cleanup;
    if (!address_space_query(second, CHOS_AS_USER_VA_BASE, &map) ||
        map.physical_address != page_peer || !map.writable || !map.accessible_at_el0 ||
        map.executable_at_el1 || map.executable_at_el0) goto cleanup;
    if (address_space_query(first, CHOS_AS_USER_VA_BASE + 2u * VMM_PAGE_SIZE, &map)) goto cleanup;

    text_page = ((uint64_t)(uintptr_t)&__text_start) & ~((uint64_t)VMM_PAGE_SIZE - 1u);
    if (!address_space_query(first, text_page, &map) || map.accessible_at_el0 ||
        !map.executable_at_el1 || map.writable || map.executable_at_el0) goto cleanup;
    if (!address_space_query(first, (uint64_t)platform_uart_base(), &map) ||
        map.memory_type != VMM_MEMORY_DEVICE || map.accessible_at_el0 ||
        map.executable_at_el1 || map.executable_at_el0) goto cleanup;
    if (address_space_map_user_page(first, CHOS_AS_USER_VA_BASE, page_ro, AS_PERM_USER_RW) != AS_ERR_MAPPING_CONFLICT) goto cleanup;
    if (address_space_map_user_page(first, CHOS_AS_USER_VA_BASE + 3u * VMM_PAGE_SIZE,
                                    text_page, AS_PERM_USER_RW) != AS_ERR_PAGE_NOT_OWNED) goto cleanup;
    if (!address_space_get_info(first, &info) || info.mapped_user_pages != 2u || info.user_l3_tables != 1u ||
        info.owned_user_frames != 2u || info.root_table_physical == 0u ||
        info.root_table_physical == vmm_kernel_root_table_address()) goto cleanup;
    if (address_space_unmap_user_page(first, CHOS_AS_USER_VA_BASE, &returned) != AS_OK || returned != page_rw) goto cleanup;
    mapped_rw = 0;
    if (address_space_unmap_user_page(first, CHOS_AS_USER_VA_BASE + VMM_PAGE_SIZE, &returned) != AS_OK || returned != page_ro) goto cleanup;
    mapped_ro = 0;
    if (address_space_unmap_user_page(second, CHOS_AS_USER_VA_BASE, &returned) != AS_OK || returned != page_peer) goto cleanup;
    mapped_peer = 0;
    if (address_space_destroy(first) != AS_OK) goto cleanup;
    first_created = 0;
    first_rw = first_ro = 0;
    if (address_space_destroy(second) != AS_OK) goto cleanup;
    second_created = 0;
    second_page = 0;
    if (address_space_query(first, CHOS_AS_USER_VA_BASE, &map) ||
        address_space_query(second, CHOS_AS_USER_VA_BASE, &map)) goto cleanup;
    pmm_get_stats(&after);
    ok = before.free_pages == after.free_pages && before.allocated_pages == after.allocated_pages &&
         address_space_count() == 0u;

cleanup:
    if (first_created) {
        if (mapped_rw && address_space_unmap_user_page(first, CHOS_AS_USER_VA_BASE, NULL) == AS_OK) mapped_rw = 0;
        if (mapped_ro && address_space_unmap_user_page(first, CHOS_AS_USER_VA_BASE + VMM_PAGE_SIZE, NULL) == AS_OK) mapped_ro = 0;
        if (!mapped_rw && !mapped_ro && address_space_destroy(first) == AS_OK) {
            first_created = 0;
            first_rw = first_ro = 0;
        }
        if (first_created) {
            if (first_ro && !mapped_ro && address_space_free_user_page(first, page_ro) == AS_OK) first_ro = 0;
            if (first_rw && !mapped_rw && address_space_free_user_page(first, page_rw) == AS_OK) first_rw = 0;
            if (!mapped_rw && !mapped_ro) (void)address_space_destroy(first);
        }
    }
    if (second_created) {
        if (mapped_peer && address_space_unmap_user_page(second, CHOS_AS_USER_VA_BASE, NULL) == AS_OK) mapped_peer = 0;
        if (!mapped_peer && address_space_destroy(second) == AS_OK) {
            second_created = 0;
            second_page = 0;
        }
        if (second_created) {
            if (second_page && !mapped_peer) (void)address_space_free_user_page(second, page_peer);
            if (!mapped_peer) (void)address_space_destroy(second);
        }
    }
    return ok;
}

int address_space_activation_self_test(void) {
    const uint64_t first_value = 0x43484f5355413031ull;
    const uint64_t second_value = 0x43484f5355413032ull;
    const uint64_t first_updated = 0x4652535457524954ull;
    address_space_handle_t first = {0u, 0u, 0u};
    address_space_handle_t second = {0u, 0u, 0u};
    pmm_stats_t before, after;
    vmm_stats_t vmm_stats;
    vmm_mapping_info_t mapping;
    uint64_t first_page = 0u, second_page = 0u, live_added_page = 0u;
    uint64_t first_root = 0u, second_root = 0u;
    uint64_t kernel_root = vmm_kernel_root_table_address();
    volatile uint64_t *user_alias = (volatile uint64_t *)(uintptr_t)CHOS_AS_USER_VA_BASE;
    volatile uint64_t *first_backing, *second_backing, *live_added_backing;
    volatile uint64_t *live_added_alias = (volatile uint64_t *)(uintptr_t)(CHOS_AS_USER_VA_BASE + VMM_PAGE_SIZE);
    int first_created = 0, second_created = 0;
    int first_mapped = 0, second_mapped = 0, live_added_owned = 0, live_added_mapped = 0;
    int ok = 0;

    if (!vmm_is_enabled() || vmm_current_root_table_address() != kernel_root ||
        active_address_space_slot != -1) return 0;
    vmm_get_stats(&vmm_stats);
    if (vmm_stats.dynamic_mappings != 0u) return 0;
    pmm_get_stats(&before);

    if (address_space_create(&first) != AS_OK) goto cleanup;
    first_created = 1;
    if (address_space_create(&second) != AS_OK) goto cleanup;
    second_created = 1;
    if (address_space_alloc_user_page(first, &first_page) != AS_OK) goto cleanup;
    if (address_space_alloc_user_page(second, &second_page) != AS_OK) goto cleanup;
    if (address_space_map_user_page(first, CHOS_AS_USER_VA_BASE, first_page, AS_PERM_USER_RW) != AS_OK) goto cleanup;
    first_mapped = 1;
    if (address_space_map_user_page(second, CHOS_AS_USER_VA_BASE, second_page, AS_PERM_USER_RW) != AS_OK) goto cleanup;
    second_mapped = 1;

    first_root = (uint64_t)(uintptr_t)spaces[first.slot].root_pa;
    second_root = (uint64_t)(uintptr_t)spaces[second.slot].root_pa;
    first_backing = (volatile uint64_t *)(uintptr_t)first_page;
    second_backing = (volatile uint64_t *)(uintptr_t)second_page;
    first_backing[0] = first_value;
    second_backing[0] = second_value;
    __asm__ volatile("dsb sy" ::: "memory");

    if (address_space_activate(first) != AS_OK) goto cleanup;
    if (!address_space_is_active(first) ||
        vmm_current_root_table_address() != first_root || *user_alias != first_value) goto cleanup;
    *user_alias = first_updated;
    __asm__ volatile("dsb sy" ::: "memory");
    if (first_backing[0] != first_updated || second_backing[0] != second_value) goto cleanup;
    if (address_space_destroy(first) != AS_ERR_ACTIVE) goto cleanup;
    if (!vmm_query_mapping(CHOS_AS_USER_VA_BASE, &mapping) ||
        mapping.physical_address != first_page || !mapping.accessible_at_el0) goto cleanup;

    /* Exercise a live-root PTE update (targeted TLBI, not a whole-root switch). */
    if (address_space_alloc_user_page(first, &live_added_page) != AS_OK) goto cleanup;
    live_added_owned = 1;
    live_added_backing = (volatile uint64_t *)(uintptr_t)live_added_page;
    if (*live_added_backing != 0u) goto cleanup;
    if (address_space_map_user_page(first, CHOS_AS_USER_VA_BASE + VMM_PAGE_SIZE,
                                    live_added_page, AS_PERM_USER_RW) != AS_OK) goto cleanup;
    live_added_mapped = 1;
    *live_added_alias = 0x544c42494e56414cull;
    __asm__ volatile("dsb sy" ::: "memory");
    if (*live_added_backing != 0x544c42494e56414cull) goto cleanup;
    if (!vmm_query_mapping(CHOS_AS_USER_VA_BASE + VMM_PAGE_SIZE, &mapping) ||
        mapping.physical_address != live_added_page || !mapping.writable) goto cleanup;
    if (address_space_unmap_user_page(first, CHOS_AS_USER_VA_BASE + VMM_PAGE_SIZE, NULL) != AS_OK) goto cleanup;
    live_added_mapped = 0;
    if (address_space_query(first, CHOS_AS_USER_VA_BASE + VMM_PAGE_SIZE, &mapping)) goto cleanup;
    if (address_space_free_user_page(first, live_added_page) != AS_OK) goto cleanup;
    live_added_owned = 0;

    if (address_space_activate(second) != AS_OK) goto cleanup;
    if (!address_space_is_active(second) ||
        vmm_current_root_table_address() != second_root || *user_alias != second_value) goto cleanup;
    *user_alias = second_value + 1u;
    __asm__ volatile("dsb sy" ::: "memory");
    if (second_backing[0] != second_value + 1u || first_backing[0] != first_updated) goto cleanup;
    if (!vmm_query_mapping(CHOS_AS_USER_VA_BASE, &mapping) ||
        mapping.physical_address != second_page || !mapping.accessible_at_el0) goto cleanup;

    if (address_space_activate(first) != AS_OK || *user_alias != first_updated) goto cleanup;
    if (address_space_deactivate() != AS_OK) goto cleanup;
    if (vmm_current_root_table_address() != kernel_root || address_space_is_active(first) ||
        address_space_is_active(second) || vmm_query_mapping(CHOS_AS_USER_VA_BASE, &mapping)) goto cleanup;

    if (address_space_unmap_user_page(first, CHOS_AS_USER_VA_BASE, NULL) != AS_OK) goto cleanup;
    first_mapped = 0;
    if (address_space_unmap_user_page(second, CHOS_AS_USER_VA_BASE, NULL) != AS_OK) goto cleanup;
    second_mapped = 0;
    if (address_space_destroy(first) != AS_OK) goto cleanup;
    first_created = 0;
    if (address_space_destroy(second) != AS_OK) goto cleanup;
    second_created = 0;
    pmm_get_stats(&after);
    ok = before.free_pages == after.free_pages && before.allocated_pages == after.allocated_pages &&
         address_space_count() == 0u && vmm_current_root_table_address() == kernel_root;

cleanup:
    if (vmm_current_root_table_address() != kernel_root || active_address_space_slot != -1)
        (void)address_space_deactivate();
    if (first_created) {
        if (live_added_mapped && address_space_unmap_user_page(first, CHOS_AS_USER_VA_BASE + VMM_PAGE_SIZE, NULL) == AS_OK)
            live_added_mapped = 0;
        if (live_added_owned && !live_added_mapped &&
            address_space_free_user_page(first, live_added_page) == AS_OK) live_added_owned = 0;
        if (first_mapped && address_space_unmap_user_page(first, CHOS_AS_USER_VA_BASE, NULL) == AS_OK)
            first_mapped = 0;
        if (!first_mapped && !live_added_mapped && address_space_destroy(first) == AS_OK) first_created = 0;
    }
    if (second_created) {
        if (second_mapped && address_space_unmap_user_page(second, CHOS_AS_USER_VA_BASE, NULL) == AS_OK)
            second_mapped = 0;
        if (!second_mapped && address_space_destroy(second) == AS_OK) second_created = 0;
    }
    if (vmm_current_root_table_address() != kernel_root) ok = 0;
    return ok;
}

const char *address_space_error_string(address_space_error_t error) {
    switch (error) {
        case AS_OK: return "success";
        case AS_ERR_ARGUMENT: return "null or invalid argument";
        case AS_ERR_VMM_NOT_READY: return "kernel VMM/MMU is not active";
        case AS_ERR_CAPACITY: return "maximum address-space count reached";
        case AS_ERR_OUT_OF_MEMORY: return "PMM could not allocate page-table memory";
        case AS_ERR_BAD_HANDLE: return "invalid or stale address-space handle";
        case AS_ERR_VA_RANGE: return "virtual address outside the user mapping window";
        case AS_ERR_ALIGNMENT: return "address or permission is not valid/aligned";
        case AS_ERR_PAGE_NOT_OWNED: return "backing page is not a live PMM allocation";
        case AS_ERR_PAGE_NOT_MAPPED: return "user virtual page is not mapped";
        case AS_ERR_PAGE_MAPPED: return "cannot free a user frame while it remains mapped";
        case AS_ERR_FRAME_CAPACITY: return "address-space user-frame capacity reached";
        case AS_ERR_MAPPING_CONFLICT: return "virtual page is already mapped";
        case AS_ERR_KERNEL_TEMPLATE: return "kernel mapping template conflicts with user window";
        case AS_ERR_HAS_MAPPINGS: return "unmap caller-owned pages before destroying the address space";
        case AS_ERR_BAD_DESCRIPTOR: return "invalid or inconsistent translation descriptor";
        case AS_ERR_ACTIVE: return "cannot destroy the active address space";
        case AS_ERR_REFERENCED: return "address space is pinned by scheduler tasks or reference count is saturated";
        case AS_ERR_BAD_REFERENCE: return "address-space task reference underflow";
        case AS_ERR_SWITCH_FAILED: return "safe TTBR0 switch failed or root state is inconsistent";
        default: return "unknown address-space error";
    }
}

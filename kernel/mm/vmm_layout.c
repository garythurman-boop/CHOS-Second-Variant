#include <stddef.h>
#include <stdint.h>
#include "chos/fdt.h"
#include "chos/vmm.h"

#define DESC_VALID          (1ull << 0)
#define DESC_TABLE_OR_PAGE  (1ull << 1)
#define DESC_ATTRIDX_SHIFT  2u
#define DESC_ATTRIDX_MASK   (7ull << DESC_ATTRIDX_SHIFT)
#define DESC_AP_SHIFT       6u
#define DESC_AP_MASK        (3ull << DESC_AP_SHIFT)
#define DESC_SH_OUTER       (2ull << 8)
#define DESC_SH_INNER       (3ull << 8)
#define DESC_AF             (1ull << 10)
#define DESC_NG             (1ull << 11)
#define DESC_PXN            (1ull << 53)
#define DESC_UXN            (1ull << 54)
#define DESC_ADDR_MASK      0x0000fffffffff000ull
#define DESC_AP_EL1_RO      (2ull << DESC_AP_SHIFT)
#define VMM_PHYS_LIMIT      (1ull << 40)

static int range_end(uint64_t base, uint64_t size, uint64_t *end) {
    if (end == NULL || size == 0u || base > UINT64_MAX - size) return 0;
    *end = base + size;
    return *end > base;
}

static int range_is_in_ram(const fdt_info_t *tree, uint64_t start, uint64_t end) {
    size_t i;
    if (tree == NULL || start >= end) return 0;
    for (i = 0u; i < tree->memory_count; ++i) {
        uint64_t region_end;
        if (range_end(tree->memory[i].base, tree->memory[i].size, &region_end) &&
            start >= tree->memory[i].base && end <= region_end) return 1;
    }
    return 0;
}

uint64_t vmm_make_table_descriptor(uint64_t table_address) {
    if ((table_address & (VMM_PAGE_SIZE - 1u)) != 0u ||
        table_address >= VMM_PHYS_LIMIT) return 0u;
    return (table_address & DESC_ADDR_MASK) | DESC_VALID | DESC_TABLE_OR_PAGE;
}

uint64_t vmm_make_user_page_descriptor(uint64_t physical_address, int writable) {
    uint64_t descriptor;
    if ((physical_address & (VMM_PAGE_SIZE - 1u)) != 0u ||
        physical_address >= VMM_PHYS_LIMIT) return 0u;
    /* User leaves use normal WBWA memory, inner-shareable, AF=1. AP=01 gives
       EL0/EL1 RW, AP=11 gives EL0/EL1 RO. Both PXN+UXN are set: user-mode
       execution remains deliberately unsupported until executable alias policy exists. */
    descriptor = (physical_address & DESC_ADDR_MASK) | DESC_VALID | DESC_TABLE_OR_PAGE |
                 DESC_AF | DESC_NG | DESC_SH_INNER | DESC_PXN | DESC_UXN;
    descriptor |= (writable ? 1ull : 3ull) << DESC_AP_SHIFT;
    return descriptor;
}

int vmm_validate_user_page_descriptor(uint64_t descriptor, uint64_t physical_address,
                                      int writable) {
    uint64_t expected = vmm_make_user_page_descriptor(physical_address, writable);
    return expected != 0u && descriptor == expected;
}

uint64_t vmm_make_user_code_page_descriptor(uint64_t physical_address) {
    uint64_t descriptor;
    if ((physical_address & (VMM_PAGE_SIZE - 1u)) != 0u ||
        physical_address >= VMM_PHYS_LIMIT) return 0u;
    /* AP=11: EL0/EL1 read-only. PXN=1 forbids EL1 execution; UXN=0 allows EL0
       instruction fetch. Non-global and inner-shareable like ordinary user leaves. */
    descriptor = (physical_address & DESC_ADDR_MASK) | DESC_VALID | DESC_TABLE_OR_PAGE |
                 DESC_AF | DESC_NG | DESC_SH_INNER | DESC_PXN |
                 (3ull << DESC_AP_SHIFT);
    return descriptor;
}

int vmm_validate_user_code_page_descriptor(uint64_t descriptor, uint64_t physical_address) {
    uint64_t expected = vmm_make_user_code_page_descriptor(physical_address);
    return expected != 0u && descriptor == expected;
}

uint64_t vmm_make_user_inaccessible_page_descriptor(uint64_t physical_address) {
    uint64_t descriptor = vmm_make_user_page_descriptor(physical_address, 1);
    return descriptor ? descriptor & ~DESC_AP_MASK : 0u;
}

int vmm_validate_user_inaccessible_page_descriptor(uint64_t descriptor, uint64_t physical_address) {
    uint64_t expected = vmm_make_user_inaccessible_page_descriptor(physical_address);
    return expected != 0u && descriptor == expected;
}

uint64_t vmm_make_page_descriptor(uint64_t physical_address,
                                  vmm_memory_type_t memory_type,
                                  vmm_page_permissions_t permissions) {
    uint64_t descriptor;
    if ((physical_address & (VMM_PAGE_SIZE - 1u)) != 0u ||
        physical_address >= VMM_PHYS_LIMIT) return 0u;
    if (memory_type != VMM_MEMORY_DEVICE && memory_type != VMM_MEMORY_NORMAL) return 0u;
    if ((unsigned)permissions > (unsigned)VMM_PERM_KERNEL_DATA) return 0u;
    if ((memory_type == VMM_MEMORY_DEVICE) != (permissions == VMM_PERM_DEVICE)) return 0u;

    descriptor = (physical_address & DESC_ADDR_MASK) | DESC_VALID | DESC_TABLE_OR_PAGE | DESC_AF;
    if (memory_type == VMM_MEMORY_DEVICE) {
        /* MAIR AttrIdx 1 = Device-nGnRE; Outer Shareable; privileged RW; XN. */
        descriptor |= (1ull << DESC_ATTRIDX_SHIFT) | DESC_SH_OUTER | DESC_PXN | DESC_UXN;
    } else {
        /* MAIR AttrIdx 0 = Normal WBWA; Inner Shareable. EL0 has no access. */
        descriptor |= DESC_SH_INNER;
        if (permissions == VMM_PERM_KERNEL_TEXT || permissions == VMM_PERM_KERNEL_RODATA) {
            descriptor |= DESC_AP_EL1_RO;
        }
        if (permissions != VMM_PERM_KERNEL_TEXT) descriptor |= DESC_PXN;
        descriptor |= DESC_UXN;
    }
    return descriptor;
}

int vmm_validate_page_descriptor(uint64_t descriptor, uint64_t physical_address,
                                 vmm_memory_type_t memory_type,
                                 vmm_page_permissions_t permissions) {
    uint64_t expected = vmm_make_page_descriptor(physical_address, memory_type, permissions);
    return expected != 0u && descriptor == expected;
}

vmm_error_t vmm_validate_dynamic_page_mapping(uint64_t virtual_address,
                                               uint64_t physical_address,
                                               vmm_memory_type_t memory_type,
                                               vmm_page_permissions_t permissions) {
    if (virtual_address < VMM_DYNAMIC_VA_BASE ||
        virtual_address >= VMM_DYNAMIC_VA_LIMIT) {
        return VMM_ERR_VIRTUAL_ADDRESS_OUT_OF_RANGE;
    }
    if ((virtual_address & (VMM_PAGE_SIZE - 1u)) != 0u ||
        (physical_address & (VMM_PAGE_SIZE - 1u)) != 0u ||
        vmm_make_page_descriptor(physical_address, memory_type, permissions) == 0u) {
        return VMM_ERR_BAD_MAPPING_ARGUMENT;
    }
    /* Existing identity mappings remain writable or device-RW. Until the kernel
       can remove those aliases safely, dynamic aliases must not widen permissions. */
    if ((memory_type == VMM_MEMORY_NORMAL && permissions != VMM_PERM_KERNEL_DATA) ||
        (memory_type == VMM_MEMORY_DEVICE && permissions != VMM_PERM_DEVICE)) {
        return VMM_ERR_BAD_MAPPING_ARGUMENT;
    }
    return VMM_OK;
}


/* Find a free VA candidate in a dynamic mapping bitmap. This helper is deliberately
 * pure so wraparound/full-window behavior can be tested without EL1 page tables. */
int vmm_find_dynamic_candidate(const uint8_t *bitmap, size_t bitmap_bytes,
                               uint64_t start_hint, uint64_t *candidate) {
    const size_t page_count = (size_t)VMM_DYNAMIC_PAGE_COUNT;
    size_t start_index, index, scanned = 0u;
    if (bitmap == NULL || candidate == NULL || bitmap_bytes < VMM_DYNAMIC_BITMAP_BYTES ||
        start_hint < VMM_DYNAMIC_VA_BASE || start_hint >= VMM_DYNAMIC_VA_LIMIT ||
        ((start_hint - VMM_DYNAMIC_VA_BASE) & (VMM_PAGE_SIZE - 1u)) != 0u) return 0;
    start_index = (size_t)((start_hint - VMM_DYNAMIC_VA_BASE) / VMM_PAGE_SIZE);
    index = start_index;
    while (scanned < page_count) {
        /* Skip a whole occupied byte when aligned. The 768 MiB window has 24 KiB
         * of bitmap; a full-window search therefore stays cheap even with IRQs masked. */
        if ((index & 7u) == 0u && page_count - scanned >= 8u &&
            bitmap[index / 8u] == 0xffu) {
            index += 8u;
            scanned += 8u;
            if (index >= page_count) index = 0u;
            continue;
        }
        if ((bitmap[index / 8u] & (uint8_t)(1u << (index & 7u))) == 0u) {
            *candidate = VMM_DYNAMIC_VA_BASE + (uint64_t)index * VMM_PAGE_SIZE;
            return 1;
        }
        ++index;
        ++scanned;
        if (index >= page_count) index = 0u;
    }
    return 0;
}

vmm_error_t vmm_validate_boot_layout(const fdt_info_t *tree,
                                     uint64_t kernel_start, uint64_t kernel_end,
                                     uint64_t dtb_start, uint64_t dtb_size) {
    uint64_t dtb_end;
    size_t i, j;

    if (tree == NULL) return VMM_ERR_NULL_ARGUMENT;
    if (!tree->valid || tree->memory_count == 0u ||
        tree->memory_count > FDT_MAX_MEMORY_REGIONS ||
        tree->device_count > FDT_MAX_DEVICES) return VMM_ERR_BAD_DEVICE_TREE;
    /* Device records are inventory, not a list of ranges that must all be
       mapped as peripheral MMIO. QEMU's FDT may describe PCI apertures,
       bridge windows, flash or other regions outside the low MMIO aperture.
       The early VMM maps only the fixed platform windows used by current
       drivers (PL011 and GICv2), in vmm.c. */
    if (kernel_start >= kernel_end || kernel_start < VMM_RAM_BASE ||
        kernel_end > VMM_MAP_LIMIT ||
        !range_is_in_ram(tree, kernel_start, kernel_end)) {
        return VMM_ERR_BAD_KERNEL_RANGE;
    }
    if (!range_end(dtb_start, dtb_size, &dtb_end) ||
        dtb_start < VMM_RAM_BASE || dtb_end > VMM_MAP_LIMIT ||
        !range_is_in_ram(tree, dtb_start, dtb_end)) {
        return VMM_ERR_BAD_DTB_RANGE;
    }

    for (i = 0u; i < tree->memory_count; ++i) {
        uint64_t end_i;
        if (!range_end(tree->memory[i].base, tree->memory[i].size, &end_i) ||
            tree->memory[i].base < VMM_RAM_BASE || end_i > VMM_MAP_LIMIT) {
            return VMM_ERR_UNSUPPORTED_RAM_LAYOUT;
        }
        for (j = i + 1u; j < tree->memory_count; ++j) {
            uint64_t end_j;
            if (!range_end(tree->memory[j].base, tree->memory[j].size, &end_j) ||
                (tree->memory[i].base < end_j && tree->memory[j].base < end_i)) {
                return VMM_ERR_UNSUPPORTED_RAM_LAYOUT;
            }
        }
    }

    /* Do not validate arbitrary FDT reg ranges against the VMM's low-MMIO
       aperture: many are host-bridge/resource windows rather than device
       register windows required by the kernel at this stage. The current
       platform-specific mappings are bounded by map_device_range() before
       any translation tables can be enabled. */
    return VMM_OK;
}

/* MMU-off handoff retains its original contract. Translated DMA is accepted
 * only for the explicit CHOS-owned UEFI identity profile; caching remains off. */
int vmm_identity_dma_policy(uint64_t sctlr,int owned_uefi_profile,int enabled_by_kernel){
    if(sctlr&(UINT64_C(1)<<2))return 0;
    return !(sctlr&1u) || (owned_uefi_profile==1 && enabled_by_kernel==1);
}

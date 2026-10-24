#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "chos/fdt.h"
#include "chos/vmm.h"

#define DESC_VALID (1ull << 0)
#define DESC_TABLE_OR_PAGE (1ull << 1)
#define ATTRIDX_MASK (7ull << 2)
#define AP_MASK (3ull << 6)
#define PXN (1ull << 53)
#define UXN (1ull << 54)

static void make_tree(fdt_info_t *tree) {
    memset(tree, 0, sizeof(*tree));
    tree->valid = 1;
    tree->memory_count = 1u;
    tree->memory[0].base = 0x40000000ull;
    tree->memory[0].size = 0x10000000ull;
    tree->device_count = 2u;
    tree->devices[0].has_reg = 1;
    tree->devices[0].reg_base = 0x09000000ull;
    tree->devices[0].reg_size = 0x1000ull;
    tree->devices[1].has_reg = 1;
    tree->devices[1].reg_base = 0x0a000000ull;
    tree->devices[1].reg_size = 0x2000ull;
}

int main(void) {
    fdt_info_t tree;
    uint64_t desc, candidate;
    static uint8_t dynamic_bitmap[VMM_DYNAMIC_BITMAP_BYTES];
    size_t last_index = (size_t)VMM_DYNAMIC_PAGE_COUNT - 1u;

    assert(vmm_make_table_descriptor(0x40201000ull) == (0x40201000ull | 3ull));
    assert(vmm_make_table_descriptor(0x40201001ull) == 0u);
    assert(vmm_make_table_descriptor(0x10000000000ull) == 0u);

    desc = vmm_make_page_descriptor(0x40200000ull, VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_TEXT);
    assert(desc != 0u && (desc & 3u) == 3u);
    assert((desc & AP_MASK) == (2ull << 6));
    assert((desc & PXN) == 0u && (desc & UXN) != 0u);
    assert(((desc & ATTRIDX_MASK) >> 2) == 0u);
    assert(vmm_validate_page_descriptor(desc, 0x40200000ull,
                                        VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_TEXT));
    assert(!vmm_validate_page_descriptor(desc, 0x40200000ull,
                                         VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_DATA));

    desc = vmm_make_page_descriptor(0x40201000ull, VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_RODATA);
    assert(desc != 0u && (desc & AP_MASK) == (2ull << 6));
    assert((desc & PXN) != 0u && (desc & UXN) != 0u);
    desc = vmm_make_page_descriptor(0x40202000ull, VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_DATA);
    assert(desc != 0u && (desc & AP_MASK) == 0u);
    assert((desc & PXN) != 0u && (desc & UXN) != 0u);
    desc = vmm_make_page_descriptor(0x09000000ull, VMM_MEMORY_DEVICE, VMM_PERM_DEVICE);
    assert(desc != 0u && ((desc & ATTRIDX_MASK) >> 2) == 1u);
    assert((desc & PXN) != 0u && (desc & UXN) != 0u && (desc & AP_MASK) == 0u);
    assert(vmm_validate_dynamic_page_mapping(VMM_DYNAMIC_VA_BASE, 0x40210000ull,
                                             VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_DATA) == VMM_OK);
    assert(vmm_validate_dynamic_page_mapping(VMM_DYNAMIC_VA_BASE, 0x40210000ull,
                                             VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_TEXT) == VMM_ERR_BAD_MAPPING_ARGUMENT);
    assert(vmm_validate_dynamic_page_mapping(VMM_DYNAMIC_VA_BASE, 0x40210001ull,
                                             VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_DATA) == VMM_ERR_BAD_MAPPING_ARGUMENT);
    assert(vmm_validate_dynamic_page_mapping(VMM_DYNAMIC_VA_BASE + 1u, 0x40210000ull,
                                             VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_DATA) == VMM_ERR_BAD_MAPPING_ARGUMENT);
    assert(vmm_validate_dynamic_page_mapping(VMM_DYNAMIC_VA_LIMIT - VMM_PAGE_SIZE, 0x40210000ull,
                                             VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_DATA) == VMM_OK);
    assert(vmm_validate_dynamic_page_mapping(VMM_DYNAMIC_VA_LIMIT, 0x40210000ull,
                                             VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_DATA) == VMM_ERR_VIRTUAL_ADDRESS_OUT_OF_RANGE);
    assert(vmm_validate_dynamic_page_mapping(VMM_DYNAMIC_VA_BASE - VMM_PAGE_SIZE, 0x40210000ull,
                                             VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_DATA) == VMM_ERR_VIRTUAL_ADDRESS_OUT_OF_RANGE);
    assert(vmm_validate_dynamic_page_mapping(VMM_DYNAMIC_VA_BASE, 0x10000000000ull,
                                             VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_DATA) == VMM_ERR_BAD_MAPPING_ARGUMENT);
    assert(vmm_validate_dynamic_page_mapping(VMM_DYNAMIC_VA_BASE, 0x09000000ull,
                                             VMM_MEMORY_DEVICE, VMM_PERM_DEVICE) == VMM_OK);
    assert(vmm_validate_dynamic_page_mapping(VMM_DYNAMIC_VA_BASE, 0x09000000ull,
                                             VMM_MEMORY_DEVICE, VMM_PERM_KERNEL_DATA) == VMM_ERR_BAD_MAPPING_ARGUMENT);
    /* Dynamic candidate search is pure and testable without touching page tables. */
    memset(dynamic_bitmap, 0, sizeof(dynamic_bitmap));
    assert(vmm_find_dynamic_candidate(dynamic_bitmap, sizeof(dynamic_bitmap),
                                      VMM_DYNAMIC_VA_BASE, &candidate));
    assert(candidate == VMM_DYNAMIC_VA_BASE);
    dynamic_bitmap[0] |= 0x03u;
    assert(vmm_find_dynamic_candidate(dynamic_bitmap, sizeof(dynamic_bitmap),
                                      VMM_DYNAMIC_VA_BASE, &candidate));
    assert(candidate == VMM_DYNAMIC_VA_BASE + 2u * VMM_PAGE_SIZE);
    memset(dynamic_bitmap, 0xff, sizeof(dynamic_bitmap));
    dynamic_bitmap[0] &= (uint8_t)~(1u << 1);
    assert(vmm_find_dynamic_candidate(dynamic_bitmap, sizeof(dynamic_bitmap),
                                      VMM_DYNAMIC_VA_BASE + 10u * VMM_PAGE_SIZE, &candidate));
    assert(candidate == VMM_DYNAMIC_VA_BASE + VMM_PAGE_SIZE); /* wraparound */
    memset(dynamic_bitmap, 0xff, sizeof(dynamic_bitmap));
    assert(!vmm_find_dynamic_candidate(dynamic_bitmap, sizeof(dynamic_bitmap),
                                       VMM_DYNAMIC_VA_BASE, &candidate)); /* full window */
    assert(!vmm_find_dynamic_candidate(dynamic_bitmap, sizeof(dynamic_bitmap) - 1u,
                                       VMM_DYNAMIC_VA_BASE, &candidate));
    assert(!vmm_find_dynamic_candidate(dynamic_bitmap, sizeof(dynamic_bitmap),
                                       VMM_DYNAMIC_VA_BASE + 1u, &candidate));
    dynamic_bitmap[last_index / 8u] &= (uint8_t)~(1u << (last_index % 8u));
    assert(vmm_find_dynamic_candidate(dynamic_bitmap, sizeof(dynamic_bitmap),
                                      VMM_DYNAMIC_VA_LIMIT - VMM_PAGE_SIZE, &candidate));
    assert(candidate == VMM_DYNAMIC_VA_LIMIT - VMM_PAGE_SIZE);
    assert(vmm_make_page_descriptor(0x40200001ull, VMM_MEMORY_NORMAL,
                                    VMM_PERM_KERNEL_DATA) == 0u);
    assert(vmm_make_page_descriptor(0x40200000ull, VMM_MEMORY_DEVICE,
                                    VMM_PERM_KERNEL_DATA) == 0u);
    assert(vmm_make_page_descriptor(0x40200000ull, VMM_MEMORY_NORMAL,
                                    (vmm_page_permissions_t)99) == 0u);

    make_tree(&tree);
    assert(vmm_validate_boot_layout(&tree, 0x40200000ull, 0x40500000ull,
                                    0x40000000ull, 0x100000ull) == VMM_OK);
    assert(vmm_validate_boot_layout(&tree, 0x3ff00000ull, 0x40300000ull,
                                    0x40000000ull, 0x100000ull) == VMM_ERR_BAD_KERNEL_RANGE);
    assert(vmm_validate_boot_layout(&tree, 0x40200000ull, 0x40300000ull,
                                    0x4ffff000ull, 0x2000ull) == VMM_ERR_BAD_DTB_RANGE);
    tree.memory[0].base = 0u;
    assert(vmm_validate_boot_layout(&tree, 0x40200000ull, 0x40300000ull,
                                    0x40000000ull, 0x100000ull) == VMM_ERR_BAD_KERNEL_RANGE);
    make_tree(&tree);
    /* An unrelated FDT resource aperture does not have to fit the low
       peripheral-MMIO map: VMM only maps the current platform's required
       PL011/GIC windows, not all enumerated reg ranges. */
    tree.devices[0].reg_base = 0x100000000ull;
    tree.devices[0].reg_size = 0x100000ull;
    assert(vmm_validate_boot_layout(&tree, 0x40200000ull, 0x40300000ull,
                                    0x40000000ull, 0x100000ull) == VMM_OK);
    make_tree(&tree);
    tree.valid = 0;
    assert(vmm_validate_boot_layout(&tree, 0x40200000ull, 0x40300000ull,
                                    0x40000000ull, 0x100000ull) == VMM_ERR_BAD_DEVICE_TREE);
    make_tree(&tree);
    tree.devices_truncated = 1;
    assert(vmm_validate_boot_layout(&tree, 0x40200000ull, 0x40300000ull,
                                    0x40000000ull, 0x100000ull) == VMM_OK);
    make_tree(&tree);
    tree.memory_count = 2u;
    tree.memory[1].base = 0x48000000ull;
    tree.memory[1].size = 0x10000000ull;
    assert(vmm_validate_boot_layout(&tree, 0x40200000ull, 0x40300000ull,
                                    0x40000000ull, 0x100000ull) == VMM_ERR_UNSUPPORTED_RAM_LAYOUT);

    puts("vmm: descriptors, W^X, boot layout, dynamic mapping bounds/permissions, bitmap candidate search/wrap/full-window, and platform MMIO policy checks passed");
    return 0;
}

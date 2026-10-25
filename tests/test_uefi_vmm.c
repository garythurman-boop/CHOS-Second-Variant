#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "chos/uefi_vmm.h"

#define PAGE UINT64_C(4096)
#define BLOCK UINT64_C(0x200000)
#define NORMAL_BASE UINT64_C(0x40000000)
#define HIGH_BAR UINT64_C(0x800000c000)
#define TABLE_BASE UINT64_C(0x48000000)

/* A heap-independent, fake physical table base keeps the builder's descriptor
 * arithmetic testable on an ordinary host without treating host VAs as PAs. */
static chos_uefi_vmm_tables_t tables;

static void fresh(void) {
    assert(chos_uefi_vmm_init(&tables, TABLE_BASE));
    assert(tables.physical_base == TABLE_BASE);
    assert(tables.l2_used == 0u && tables.l3_used == 0u);
}

static vmm_mapping_info_t mapping(uint64_t address) {
    vmm_mapping_info_t result;
    memset(&result, 0xa5, sizeof(result));
    assert(chos_uefi_vmm_query(&tables, address, &result));
    assert(result.physical_address == address);
    assert(!result.accessible_at_el0 && !result.executable_at_el0);
    return result;
}

static void unmapped(uint64_t address) {
    vmm_mapping_info_t result;
    assert(!chos_uefi_vmm_query(&tables, address, &result));
}

static void test_init_and_arguments(void) {
    assert(!chos_uefi_vmm_init(NULL, TABLE_BASE));
    assert(!chos_uefi_vmm_init(&tables, 0));
    assert(!chos_uefi_vmm_init(&tables, TABLE_BASE + 1));
    assert(!chos_uefi_vmm_init(&tables, CHOS_UVMM_PHYS_LIMIT));
    assert(!chos_uefi_vmm_init(&tables, CHOS_UVMM_PHYS_LIMIT - PAGE));
    fresh();
    assert((tables.root[0] & 3u) == 3u);
    assert((tables.root[1] & 3u) == 3u);
    assert(chos_uefi_vmm_l1_address(&tables, 0) ==
           TABLE_BASE + offsetof(chos_uefi_vmm_tables_t, l1[0]));
    assert(chos_uefi_vmm_l1_address(&tables, 1) ==
           TABLE_BASE + offsetof(chos_uefi_vmm_tables_t, l1[1]));
    assert(chos_uefi_vmm_l1_address(&tables, 2) == 0);
    assert(chos_uefi_vmm_l1_address(NULL, 0) == 0);
    assert(chos_uefi_vmm_l2_address(&tables, 512) == 0);
    assert(chos_uefi_vmm_l2_address(NULL, 0) == 0);
    assert(!chos_uefi_vmm_map(NULL, NORMAL_BASE, PAGE,
                            VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_DATA));
    assert(!chos_uefi_vmm_map(&tables, NORMAL_BASE, 0,
                            VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_DATA));
    assert(!chos_uefi_vmm_map(&tables, NORMAL_BASE + 1, PAGE,
                            VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_DATA));
    assert(!chos_uefi_vmm_map(&tables, NORMAL_BASE, PAGE + 1,
                            VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_DATA));
    assert(!chos_uefi_vmm_map(&tables, CHOS_UVMM_PHYS_LIMIT, PAGE,
                            VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_DATA));
    assert(!chos_uefi_vmm_map(&tables, CHOS_UVMM_PHYS_LIMIT - PAGE, PAGE * 2,
                            VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_DATA));
    assert(!chos_uefi_vmm_map(&tables, NORMAL_BASE, UINT64_MAX - PAGE + 1,
                            VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_DATA));
    assert(!chos_uefi_vmm_map(&tables, NORMAL_BASE, PAGE,
                            VMM_MEMORY_DEVICE, VMM_PERM_KERNEL_DATA));
    assert(!chos_uefi_vmm_map(&tables, NORMAL_BASE, PAGE,
                            (vmm_memory_type_t)7, VMM_PERM_KERNEL_DATA));
    assert(!chos_uefi_vmm_map(&tables, NORMAL_BASE, PAGE,
                            VMM_MEMORY_NORMAL, (vmm_page_permissions_t)7));
    assert(tables.l2_used == 0u && tables.l3_used == 0u);
    vmm_mapping_info_t out;
    assert(!chos_uefi_vmm_query(NULL, NORMAL_BASE, &out));
    assert(!chos_uefi_vmm_query(&tables, NORMAL_BASE, NULL));
    assert(!chos_uefi_vmm_query(&tables, CHOS_UVMM_PHYS_LIMIT, &out));
    assert(!chos_uefi_vmm_protect(NULL, NORMAL_BASE, VMM_PERM_KERNEL_DATA, 1));
    assert(!chos_uefi_vmm_protect(&tables, NORMAL_BASE + 1, VMM_PERM_KERNEL_TEXT, 1));
    assert(!chos_uefi_vmm_protect(&tables, CHOS_UVMM_PHYS_LIMIT, VMM_PERM_KERNEL_TEXT, 1));
    assert(!chos_uefi_vmm_protect(&tables, NORMAL_BASE, VMM_PERM_DEVICE, 1));
    assert(!chos_uefi_vmm_protect(&tables, NORMAL_BASE, (vmm_page_permissions_t)-1, 1));
    assert(!chos_uefi_vmm_protect(&tables, NORMAL_BASE, VMM_PERM_KERNEL_TEXT, 1));
}

static void test_blocks_high_bar_and_edges(void) {
    fresh();
    assert(chos_uefi_vmm_map(&tables, NORMAL_BASE, BLOCK * 3,
                           VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_DATA));
    assert(tables.l2_used == 1u && tables.l3_used == 0u);
    for (uint64_t offset = 0; offset < BLOCK * 3; offset += PAGE) {
        vmm_mapping_info_t m = mapping(NORMAL_BASE + offset + 11);
        assert(m.memory_type == VMM_MEMORY_NORMAL && m.writable && !m.executable_at_el1);
    }
    unmapped(NORMAL_BASE - 1);
    unmapped(NORMAL_BASE + BLOCK * 3);

    /* This BAR is outside a39-bit VA, as allocated by the current QEMU profile. */
    assert(HIGH_BAR >= (UINT64_C(1) << 39));
    assert(chos_uefi_vmm_map(&tables, HIGH_BAR, PAGE * 3,
                           VMM_MEMORY_DEVICE, VMM_PERM_DEVICE));
    for (uint64_t offset = 0; offset < PAGE * 3; offset += PAGE) {
        vmm_mapping_info_t m = mapping(HIGH_BAR + offset + 3);
        assert(m.memory_type == VMM_MEMORY_DEVICE && m.writable && !m.executable_at_el1);
    }
    unmapped(HIGH_BAR - 1);
    unmapped(HIGH_BAR + PAGE * 3);
    assert(chos_uefi_vmm_map(&tables, CHOS_UVMM_PHYS_LIMIT - PAGE, PAGE,
                           VMM_MEMORY_DEVICE, VMM_PERM_DEVICE));
    mapping(CHOS_UVMM_PHYS_LIMIT - 1);
    assert(chos_uefi_vmm_map(&tables, 0, PAGE, VMM_MEMORY_DEVICE, VMM_PERM_DEVICE));
    mapping(0);

    /* A range crossing both the1GiB L2-parent and512GiB L1-parent boundaries. */
    fresh();
    uint64_t boundary = UINT64_C(1) << 39;
    assert(chos_uefi_vmm_map(&tables, boundary - PAGE, PAGE * 2,
                           VMM_MEMORY_DEVICE, VMM_PERM_DEVICE));
    mapping(boundary - 1);
    mapping(boundary);
    unmapped(boundary - PAGE - 1);
    unmapped(boundary + PAGE);
    assert(tables.l2_used == 2u && tables.l3_used == 2u);
}

static void test_permissions_refinement_and_guards(void) {
    fresh();
    assert(chos_uefi_vmm_map(&tables, NORMAL_BASE, BLOCK * 2,
                           VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_DATA));
    assert(chos_uefi_vmm_protect(&tables, NORMAL_BASE + PAGE,
                               VMM_PERM_KERNEL_TEXT, 1));
    assert(tables.l3_used == 1u);
    vmm_mapping_info_t m = mapping(NORMAL_BASE + PAGE);
    assert(m.memory_type == VMM_MEMORY_NORMAL && !m.writable && m.executable_at_el1);
    m = mapping(NORMAL_BASE);
    assert(m.writable && !m.executable_at_el1);
    m = mapping(NORMAL_BASE + PAGE * 2);
    assert(m.writable && !m.executable_at_el1);
    assert(chos_uefi_vmm_protect(&tables, NORMAL_BASE + PAGE * 2,
                               VMM_PERM_KERNEL_RODATA, 1));
    m = mapping(NORMAL_BASE + PAGE * 2);
    assert(!m.writable && !m.executable_at_el1);
    assert(chos_uefi_vmm_protect(&tables, NORMAL_BASE + PAGE * 3,
                               VMM_PERM_KERNEL_DATA, 0));
    unmapped(NORMAL_BASE + PAGE * 3);
    mapping(NORMAL_BASE + PAGE * 3 - 1);
    mapping(NORMAL_BASE + PAGE * 4);
    /* protect never creates a previously removed guard or an absent page. */
    assert(!chos_uefi_vmm_protect(&tables, NORMAL_BASE + PAGE * 3,
                                VMM_PERM_KERNEL_DATA, 1));
    assert(!chos_uefi_vmm_protect(&tables, NORMAL_BASE + BLOCK * 2,
                                VMM_PERM_KERNEL_DATA, 1));
    assert(chos_uefi_vmm_protect(&tables, NORMAL_BASE + BLOCK,
                               VMM_PERM_KERNEL_TEXT, 1));
    m = mapping(NORMAL_BASE + BLOCK);
    assert(!m.writable && m.executable_at_el1);
    assert(tables.l3_used == 2u);
}

static void test_mixed_page_block_range(void) {
    fresh();
    const uint64_t start = NORMAL_BASE + PAGE;
    const uint64_t length = BLOCK * 3;
    assert(chos_uefi_vmm_map(&tables, start, length,
                           VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_DATA));
    assert(tables.l2_used == 1u && tables.l3_used == 2u);
    unmapped(start - 1);
    unmapped(start + length);
    for (uint64_t offset = 0; offset < length; offset += PAGE) {
        vmm_mapping_info_t m = mapping(start + offset);
        assert(m.memory_type == VMM_MEMORY_NORMAL && m.writable && !m.executable_at_el1);
    }
    assert(chos_uefi_vmm_protect(&tables, NORMAL_BASE + BLOCK + PAGE,
                               VMM_PERM_KERNEL_RODATA, 1));
    vmm_mapping_info_t m = mapping(NORMAL_BASE + BLOCK + PAGE);
    assert(!m.writable && !m.executable_at_el1);
    m = mapping(NORMAL_BASE + BLOCK + PAGE - 1);
    assert(m.writable && !m.executable_at_el1);
    m = mapping(NORMAL_BASE + BLOCK + PAGE * 2);
    assert(m.writable && !m.executable_at_el1);
}

static void test_overlap_policy_and_idempotence(void) {
    fresh();
    assert(chos_uefi_vmm_map(&tables, NORMAL_BASE, BLOCK * 8,
                           VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_DATA));
    uint32_t l2 = tables.l2_used, l3 = tables.l3_used;
    for (unsigned i = 0; i < 40; ++i)
        assert(chos_uefi_vmm_map(&tables, NORMAL_BASE, BLOCK * 8,
                               VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_DATA));
    assert(tables.l2_used == l2 && tables.l3_used == l3);
    assert(chos_uefi_vmm_map(&tables, NORMAL_BASE + 7 * PAGE, PAGE * 3,
                           VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_DATA));
    l3 = tables.l3_used;
    assert(chos_uefi_vmm_map(&tables, NORMAL_BASE + 7 * PAGE, PAGE * 3,
                           VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_DATA));
    assert(tables.l3_used == l3);
    assert(!chos_uefi_vmm_map(&tables, NORMAL_BASE + 8 * PAGE, PAGE,
                            VMM_MEMORY_DEVICE, VMM_PERM_DEVICE));
    assert(!chos_uefi_vmm_map(&tables, NORMAL_BASE + 8 * PAGE, PAGE,
                            VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_TEXT));
    vmm_mapping_info_t m = mapping(NORMAL_BASE + 8 * PAGE);
    assert(m.memory_type == VMM_MEMORY_NORMAL && m.writable && !m.executable_at_el1);
    assert(chos_uefi_vmm_protect(&tables, NORMAL_BASE + 8 * PAGE,
                               VMM_PERM_KERNEL_TEXT, 1));
    assert(!chos_uefi_vmm_map(&tables, NORMAL_BASE + 8 * PAGE, PAGE,
                            VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_DATA));
    m = mapping(NORMAL_BASE + 8 * PAGE);
    assert(!m.writable && m.executable_at_el1);

    fresh();
    assert(chos_uefi_vmm_map(&tables, NORMAL_BASE, BLOCK,
                           VMM_MEMORY_DEVICE, VMM_PERM_DEVICE));
    l3 = tables.l3_used;
    for (unsigned i = 0; i < 32; ++i)
        assert(!chos_uefi_vmm_protect(&tables, NORMAL_BASE + PAGE * i,
                                    VMM_PERM_KERNEL_TEXT, 1));
    assert(tables.l3_used == l3); /* Invalid protection cannot burn L3 capacity. */
    m = mapping(NORMAL_BASE);
    assert(m.memory_type == VMM_MEMORY_DEVICE && m.writable && !m.executable_at_el1);
}

static void test_pool_exhaustion_preserves_existing_mappings(void) {
    fresh();
    for (unsigned i = 0; i < CHOS_UVMM_L2_TABLES; ++i)
        assert(chos_uefi_vmm_map(&tables, (uint64_t)i << 30, BLOCK,
                               VMM_MEMORY_DEVICE, VMM_PERM_DEVICE));
    assert(tables.l2_used == CHOS_UVMM_L2_TABLES);
    assert(!chos_uefi_vmm_map(&tables, (uint64_t)CHOS_UVMM_L2_TABLES << 30, PAGE,
                            VMM_MEMORY_DEVICE, VMM_PERM_DEVICE));
    assert(tables.l2_used == CHOS_UVMM_L2_TABLES);
    mapping(0);
    mapping(((uint64_t)CHOS_UVMM_L2_TABLES - 1) << 30);
    unmapped((uint64_t)CHOS_UVMM_L2_TABLES << 30);

    fresh();
    for (unsigned i = 0; i < CHOS_UVMM_L3_TABLES; ++i)
        assert(chos_uefi_vmm_map(&tables, NORMAL_BASE + (uint64_t)i * BLOCK, PAGE,
                               VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_DATA));
    assert(tables.l3_used == CHOS_UVMM_L3_TABLES);
    assert(!chos_uefi_vmm_map(&tables, NORMAL_BASE + (uint64_t)CHOS_UVMM_L3_TABLES * BLOCK,
                            PAGE, VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_DATA));
    assert(tables.l3_used == CHOS_UVMM_L3_TABLES);
    for (unsigned i = 0; i < CHOS_UVMM_L3_TABLES; ++i) {
        vmm_mapping_info_t m = mapping(NORMAL_BASE + (uint64_t)i * BLOCK);
        assert(m.writable && !m.executable_at_el1);
        unmapped(NORMAL_BASE + (uint64_t)i * BLOCK + PAGE);
    }
    unmapped(NORMAL_BASE + (uint64_t)CHOS_UVMM_L3_TABLES * BLOCK);

    /* Exhaustion while splitting must leave the original block intact. */
    uint64_t block = NORMAL_BASE + ((uint64_t)CHOS_UVMM_L3_TABLES + 1) * BLOCK;
    assert(chos_uefi_vmm_map(&tables, block, BLOCK,
                           VMM_MEMORY_NORMAL, VMM_PERM_KERNEL_DATA));
    assert(!chos_uefi_vmm_protect(&tables, block, VMM_PERM_KERNEL_TEXT, 1));
    vmm_mapping_info_t m = mapping(block);
    assert(m.writable && !m.executable_at_el1);
    m = mapping(block + BLOCK - 1);
    assert(m.writable && !m.executable_at_el1);
}

int main(void) {
    assert(vmm_identity_dma_policy(0,0,0));
    assert(vmm_identity_dma_policy(0,1,0));
    assert(!vmm_identity_dma_policy(1,0,0));
    assert(!vmm_identity_dma_policy(1,1,0));
    assert(!vmm_identity_dma_policy(1,0,1));
    assert(vmm_identity_dma_policy(1,1,1));
    assert(!vmm_identity_dma_policy(4,0,0));
    assert(!vmm_identity_dma_policy(5,1,1));
    assert(!vmm_identity_dma_policy(1,2,1));

    test_init_and_arguments();
    test_blocks_high_bar_and_edges();
    test_permissions_refinement_and_guards();
    test_mixed_page_block_range();
    test_overlap_policy_and_idempotence();
    test_pool_exhaustion_preserves_existing_mappings();
    puts("UEFI VMM: four-level high BAR/block/page mappings, W^X, guards, overlap, bounds and pool exhaustion PASS");
    return 0;
}

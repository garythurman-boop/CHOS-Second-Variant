#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "chos/address_space.h"

static void test_user_va_range(void) {
    assert(address_space_user_va_valid(CHOS_AS_USER_VA_BASE));
    assert(address_space_user_range_valid(CHOS_AS_USER_VA_BASE, 1u));
    assert(address_space_user_range_valid(CHOS_AS_USER_VA_LIMIT - 1u, 1u));
    assert(address_space_user_range_valid(UINT64_MAX, 0u));
    assert(!address_space_user_range_valid(CHOS_AS_USER_VA_BASE - 1u, 1u));
    assert(!address_space_user_range_valid(CHOS_AS_USER_VA_LIMIT - 1u, 2u));
    assert(!address_space_user_range_valid(UINT64_MAX - 1u, 4u));
    assert(address_space_user_va_valid(CHOS_AS_USER_VA_BASE + VMM_PAGE_SIZE));
    assert(address_space_user_va_valid(CHOS_AS_USER_VA_LIMIT - VMM_PAGE_SIZE));
    assert(!address_space_user_va_valid(CHOS_AS_USER_VA_BASE - VMM_PAGE_SIZE));
    assert(!address_space_user_va_valid(CHOS_AS_USER_VA_LIMIT));
    assert(!address_space_user_va_valid(CHOS_AS_USER_VA_BASE + 1u));
    assert(address_space_user_l2_index(CHOS_AS_USER_VA_BASE) == CHOS_AS_USER_L2_FIRST);
    assert(address_space_user_l2_index(CHOS_AS_USER_VA_LIMIT - VMM_PAGE_SIZE) == CHOS_AS_USER_L2_LIMIT - 1u);
    assert(address_space_user_l2_index(CHOS_AS_USER_VA_BASE + 1u) == UINT32_MAX);
    assert(address_space_user_l3_index(CHOS_AS_USER_VA_BASE) == 0u);
    assert(address_space_user_l3_index(CHOS_AS_USER_VA_BASE + 17u * VMM_PAGE_SIZE) == 17u);
}

static void test_user_descriptor_permissions(void) {
    const uint64_t physical = 0x41234000ull;
    uint64_t rw = vmm_make_user_page_descriptor(physical, 1);
    uint64_t ro = vmm_make_user_page_descriptor(physical, 0);
    uint64_t rx = vmm_make_user_code_page_descriptor(physical);
    assert(rw != 0u && ro != 0u && rw != ro);
    assert(rx != 0u && rx != rw && rx != ro);
    assert(address_space_user_descriptor_matches(rw, physical, AS_PERM_USER_RW));
    assert(address_space_user_descriptor_matches(ro, physical, AS_PERM_USER_RO));
    assert(address_space_user_descriptor_matches(rx, physical, AS_PERM_USER_RX));
    assert(!address_space_user_descriptor_matches(rw, physical, AS_PERM_USER_RO));
    assert(!address_space_user_descriptor_matches(ro, physical, AS_PERM_USER_RW));
    assert(!address_space_user_descriptor_matches(rw, physical, AS_PERM_USER_RX));
    assert(!address_space_user_descriptor_matches(rx, physical, AS_PERM_USER_RW));
    assert(!address_space_user_descriptor_matches(rw, physical + 1u, AS_PERM_USER_RW));
    assert(vmm_make_user_page_descriptor(physical + 1u, 1) == 0u);
    assert(vmm_make_user_page_descriptor(1ull << 40, 1) == 0u);
    assert(((rw >> 6) & 3u) == 1u); /* EL0 RW. */
    assert(((ro >> 6) & 3u) == 3u); /* EL0 RO. */
    assert((rw & (1ull << 53)) != 0u); /* Privileged execute-never. */
    assert((rw & (1ull << 54)) != 0u); /* User data is execute-never. */
    assert((rx & (1ull << 53)) != 0u); /* User code cannot execute at EL1. */
    assert((rx & (1ull << 54)) == 0u); /* User code is executable at EL0. */
    assert(((rx >> 6) & 3u) == 3u); /* User code is read-only. */
    assert((rw & (1ull << 11)) != 0u); /* nG: per-address-space entries are not global. */
    assert(((rw >> 2) & 7u) == 0u); /* Normal memory attr index. */
    assert((rw & (1ull << 10)) != 0u); /* Access flag. */
}

static void test_template_sanitization(void) {
    uint64_t device[VMM_L2_ENTRIES];
    uint64_t ram[VMM_L2_ENTRIES];
    uint32_t dynamic_first = (uint32_t)((VMM_DYNAMIC_VA_BASE >> 21) & 0x1ffu);
    size_t i;
    for (i = 0u; i < VMM_L2_ENTRIES; ++i) {
        device[i] = 0u;
        ram[i] = 0u;
    }
    device[10] = 0x12345003ull; /* A shared kernel/device mapping remains. */
    ram[4] = 0x23456003ull;     /* Identity-mapped kernel RAM remains. */
    ram[dynamic_first] = 0x34567003ull;
    ram[VMM_L2_ENTRIES - 1u] = 0x45678003ull;
    assert(address_space_prepare_l2_templates(device, ram, VMM_L2_ENTRIES));
    assert(device[10] == 0x12345003ull);
    assert(ram[4] == 0x23456003ull);
    assert(ram[dynamic_first] == 0x34567003ull);
    assert(ram[VMM_L2_ENTRIES - 1u] == 0x45678003ull);
    for (i = CHOS_AS_USER_L2_FIRST; i < CHOS_AS_USER_L2_LIMIT; ++i) assert(device[i] == 0u);

    device[CHOS_AS_USER_L2_FIRST] = 0x77777003ull;
    assert(!address_space_prepare_l2_templates(device, ram, VMM_L2_ENTRIES));
    assert(!address_space_prepare_l2_templates(device, ram, VMM_L2_ENTRIES - 1u));
    assert(!address_space_prepare_l2_templates(NULL, ram, VMM_L2_ENTRIES));
}

int main(void) {
    test_user_va_range();
    test_user_descriptor_permissions();
    test_template_sanitization();
    puts("test_address_space: PASS");
    return 0;
}

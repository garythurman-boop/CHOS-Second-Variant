#include <stddef.h>
#include <stdint.h>
#include "chos/address_space.h"

int address_space_user_va_valid(uint64_t virtual_address) {
    return virtual_address >= CHOS_AS_USER_VA_BASE &&
           virtual_address < CHOS_AS_USER_VA_LIMIT &&
           (virtual_address & (VMM_PAGE_SIZE - 1u)) == 0u;
}

int address_space_user_range_valid(uint64_t virtual_address, size_t length) {
    uint64_t end;
    if (length == 0u) return 1;
    if (virtual_address < CHOS_AS_USER_VA_BASE ||
        virtual_address >= CHOS_AS_USER_VA_LIMIT ||
        virtual_address > UINT64_MAX - (uint64_t)length) return 0;
    end = virtual_address + (uint64_t)length;
    return end > virtual_address && end <= CHOS_AS_USER_VA_LIMIT;
}

uint32_t address_space_user_l2_index(uint64_t virtual_address) {
    if (!address_space_user_va_valid(virtual_address)) return UINT32_MAX;
    return (uint32_t)((virtual_address >> 21) & 0x1ffu);
}

uint32_t address_space_user_l3_index(uint64_t virtual_address) {
    if (!address_space_user_va_valid(virtual_address)) return UINT32_MAX;
    return (uint32_t)((virtual_address >> 12) & 0x1ffu);
}

int address_space_prepare_l2_templates(uint64_t *device_l2, uint64_t *ram_l2,
                                       size_t entry_count) {
    size_t i;
    const uint32_t dynamic_first = (uint32_t)((VMM_DYNAMIC_VA_BASE >> 21) & 0x1ffu);
    if (device_l2 == NULL || ram_l2 == NULL || entry_count < VMM_L2_ENTRIES) return 0;

    /* User VA region must be empty in the kernel/device template; fail closed if a
       future platform mapping occupies any of these slots. */
    for (i = CHOS_AS_USER_L2_FIRST; i < CHOS_AS_USER_L2_LIMIT; ++i) {
        if (device_l2[i] != 0u) return 0;
    }
    /* Global dynamic aliases are kernel implementation details. They are not inherited
       by process roots, whose private user mapping area is separate. */
    for (i = dynamic_first; i < VMM_L2_ENTRIES; ++i) ram_l2[i] = 0u;
    for (i = CHOS_AS_USER_L2_FIRST; i < CHOS_AS_USER_L2_LIMIT; ++i) device_l2[i] = 0u;
    return 1;
}

int address_space_user_descriptor_matches(uint64_t descriptor,
                                          uint64_t physical_address,
                                          address_space_user_permissions_t permissions) {
    int writable;
    if (permissions == AS_PERM_USER_RX) {
        return vmm_validate_user_code_page_descriptor(descriptor, physical_address);
    }
    if (permissions != AS_PERM_USER_RO && permissions != AS_PERM_USER_RW) return 0;
    writable = permissions == AS_PERM_USER_RW;
    return vmm_validate_user_page_descriptor(descriptor, physical_address, writable);
}

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chos/pmm.h"

static pmm_init_stage_t observed_stages[16];
static size_t observed_stage_count;

/* Strong test override for the weak platform hook in pmm.c. */
void pmm_init_progress(pmm_init_stage_t stage) {
    /* Re-enter stats in the callback: UEFI bootstrap mode must remain lock-free
     * while state is only partially initialized and IRQs/tasks are not active. */
    if (stage >= PMM_INIT_STAGE_VALIDATING && stage <= PMM_INIT_STAGE_CORE_READY) {
        pmm_stats_t stats;
        pmm_get_stats(&stats);
    }
    if (observed_stage_count < sizeof(observed_stages) / sizeof(observed_stages[0]))
        observed_stages[observed_stage_count++] = stage;
}

static void assert_stage_sequence(void) {
    static const pmm_init_stage_t expected[] = {
        PMM_INIT_STAGE_UEFI_START,
        PMM_INIT_STAGE_UEFI_MAP_NORMALIZED,
        PMM_INIT_STAGE_UEFI_CORE_ENTRY,
        PMM_INIT_STAGE_VALIDATING,
        PMM_INIT_STAGE_REGIONS_READY,
        PMM_INIT_STAGE_BITMAPS_READY,
        PMM_INIT_STAGE_RESERVING,
        PMM_INIT_STAGE_RESERVATIONS_DONE,
        PMM_INIT_STAGE_COUNTERS_OK,
        PMM_INIT_STAGE_CORE_READY
    };
    size_t i;
    assert(observed_stage_count == sizeof(expected) / sizeof(expected[0]));
    for (i = 0; i < observed_stage_count; ++i) assert(observed_stages[i] == expected[i]);
}

int main(void) {
    const size_t allocation_size = 16u * 1024u * 1024u;
    uint8_t *memory = (uint8_t *)aligned_alloc(4096u, allocation_size);
    chos_boot_info_t info;
    chos_efi_memory_descriptor_t map[4];
    pmm_stats_t stats;
    uint64_t page = 0u;
    uint64_t base;
    assert(memory != NULL);
    memset(memory, 0, allocation_size);
    memset(&info, 0, sizeof(info));
    memset(map, 0, sizeof(map));
    base = (uint64_t)(uintptr_t)memory;
    map[0].type = 7u; map[0].physical_start = base; map[0].number_of_pages = 256u;
    map[1].type = 7u; map[1].physical_start = base + 256u * 4096u; map[1].number_of_pages = 256u;
    map[2].type = 7u; map[2].physical_start = base + 8u * 1024u * 1024u; map[2].number_of_pages = 128u;
    map[3].type = 2u; map[3].physical_start = base + 10u * 1024u * 1024u; map[3].number_of_pages = 1u;

    info.magic = CHOS_BOOT_INFO_MAGIC;
    info.version = CHOS_BOOT_INFO_VERSION;
    info.struct_size = sizeof(info);
    info.flags = CHOS_BOOT_FLAG_UEFI | CHOS_BOOT_FLAG_FRAMEBUFFER;
    info.framebuffer_base = base + 1024u * 1024u;
    info.framebuffer_size = 320u * 200u * 4u;
    info.framebuffer_width = 320u;
    info.framebuffer_height = 200u;
    info.framebuffer_stride = 320u;
    info.framebuffer_pixel_format = CHOS_GOP_PIXEL_BGRX8;
    info.memory_map_address = (uint64_t)(uintptr_t)map;
    info.memory_map_size = sizeof(map);
    info.memory_descriptor_size = sizeof(map[0]);
    info.memory_descriptor_version = 1u;
    info.memory_descriptor_count = 4u;
    info.kernel_physical_base = base + 0x10000u;
    info.kernel_image_size = 0x20000u;
    info.kernel_entry = info.kernel_physical_base + 0x100u;

    assert(chos_boot_info_validate(&info));
    observed_stage_count = 0u;
    assert(pmm_init_from_uefi(&info) == PMM_OK);
    assert(pmm_is_bootstrap_mode());
    assert_stage_sequence();
    pmm_get_stats(&stats);
    assert(stats.initialized);
    assert(stats.region_count == 2u); /* Two adjacent descriptors coalesce; the gap remains. */
    assert(stats.total_pages == 640u);
    assert(stats.reserved_pages >= 95u); /* Kernel image plus rounded framebuffer pages are reserved. */
    assert(stats.free_pages + stats.reserved_pages == stats.total_pages);
    assert(pmm_alloc_pages(3u, &page));
    assert(page < info.kernel_physical_base || page >= info.kernel_physical_base + info.kernel_image_size);
    assert(page < info.framebuffer_base || page >= info.framebuffer_base + info.framebuffer_size);
    assert(pmm_free_pages(page, 3u));

    /* An invalid descriptor stride must be rejected before it is walked. */
    info.memory_descriptor_size = 8u;
    assert(pmm_init_from_uefi(&info) == PMM_ERR_NULL_ARGUMENT);

    /* Fusion exposes far more conventional RAM than the small unit fixture.
     * This large synthetic map verifies initialization is bounded and reserves
     * a high-address kernel span without scanning every free page individually. */
    memset(&info, 0, sizeof(info));
    memset(map, 0, sizeof(map));
    map[0].type = 7u; map[0].physical_start = 0x80000000u; map[0].number_of_pages = 262144u; /* 1 GiB */
    map[1].type = 7u; map[1].physical_start = 0xF0000000u; map[1].number_of_pages = 65536u;  /* 256 MiB */
    map[2].type = 2u; map[2].physical_start = 0u; map[2].number_of_pages = 1u;
    map[3].type = 2u; map[3].physical_start = 0u; map[3].number_of_pages = 1u;
    info.magic = CHOS_BOOT_INFO_MAGIC; info.version = CHOS_BOOT_INFO_VERSION;
    info.struct_size = sizeof(info); info.flags = CHOS_BOOT_FLAG_UEFI;
    info.memory_map_address = (uint64_t)(uintptr_t)map; info.memory_map_size = sizeof(map);
    info.memory_descriptor_size = sizeof(map[0]); info.memory_descriptor_version = 1u;
    info.memory_descriptor_count = 4u;
    info.kernel_physical_base = 0xF8A2C000u; info.kernel_image_size = 45u * 1024u * 1024u;
    info.kernel_entry = info.kernel_physical_base + 0x2000u;
    assert(chos_boot_info_validate(&info));
    observed_stage_count = 0u;
    assert(pmm_init_from_uefi(&info) == PMM_OK);
    assert(pmm_is_bootstrap_mode());
    assert_stage_sequence();
    pmm_get_stats(&stats);
    assert(stats.total_pages == 327680u);
    assert(stats.reserved_pages == (45u * 1024u * 1024u) / CHOS_PAGE_SIZE);
    assert(stats.free_pages + stats.reserved_pages == stats.total_pages);
    /* Publish runtime locking only once initialization is complete, then verify
     * the ordinary locked paths still allocate/free and return a stable snapshot. */
    pmm_enable_runtime_locking();
    assert(!pmm_is_bootstrap_mode());
    assert(pmm_alloc_page(&page));
    assert(page < info.kernel_physical_base || page >= info.kernel_physical_base + info.kernel_image_size);
    assert(pmm_free_page(page));
    pmm_get_stats(&stats);
    assert(stats.free_pages + stats.reserved_pages == stats.total_pages);

    free(memory);
    puts("UEFI PMM adapter: stage trace, coalescing, compact reservations, large RAM map, allocation/free and bad ABI rejection passed");
    return 0;
}

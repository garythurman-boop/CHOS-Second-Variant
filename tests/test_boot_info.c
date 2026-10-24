#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "chos/boot_info.h"

static void init_info(chos_boot_info_t *info, uint8_t *map, size_t map_size) {
    memset(info, 0, sizeof(*info));
    info->magic = CHOS_BOOT_INFO_MAGIC;
    info->version = CHOS_BOOT_INFO_VERSION;
    info->struct_size = sizeof(*info);
    info->flags = CHOS_BOOT_FLAG_UEFI | CHOS_BOOT_FLAG_ACPI | CHOS_BOOT_FLAG_FRAMEBUFFER;
    info->memory_map_address = (uint64_t)(uintptr_t)map;
    info->memory_map_size = map_size;
    info->memory_descriptor_size = sizeof(chos_efi_memory_descriptor_t);
    info->memory_descriptor_version = 1u;
    info->memory_descriptor_count = (uint32_t)(map_size / sizeof(chos_efi_memory_descriptor_t));
    info->kernel_physical_base = 0x80000000u;
    info->kernel_image_size = 0x04000000u;
    info->kernel_entry = 0x80002000u;
    info->acpi_rsdp_address = 0x100000u;
    info->framebuffer_base = 0x90000000u;
    info->framebuffer_size = 800u * 600u * 4u;
    info->framebuffer_width = 800u;
    info->framebuffer_height = 600u;
    info->framebuffer_stride = 800u;
    info->framebuffer_pixel_format = CHOS_GOP_PIXEL_BGRX8;
}

int main(void) {
    chos_boot_info_t info;
    chos_boot_memory_summary_t summary;
    chos_efi_memory_descriptor_t map[3];
    uint64_t extended_map_storage[3u * 6u]; /* 48-byte descriptors: newer firmware may append fields. */
    memset(map, 0, sizeof(map));
    memset(extended_map_storage, 0, sizeof(extended_map_storage));
    map[0].type = 7u;
    map[0].physical_start = 0x10000000u;
    map[0].number_of_pages = 256u;
    map[1].type = 7u;
    map[1].physical_start = 0x40000000u;
    map[1].number_of_pages = 1024u;
    map[2].type = 2u; /* Loader data is not counted as conventional RAM. */
    map[2].physical_start = 0x80000000u;
    map[2].number_of_pages = 4096u;
    init_info(&info, (uint8_t *)map, sizeof(map));
    assert(chos_boot_info_validate(&info));
    assert(chos_boot_info_memory_summary(&info, &summary));
    assert(summary.conventional_range_count == 2u);
    assert(summary.conventional_bytes == (uint64_t)(256u + 1024u) * 4096u);
    assert(summary.largest_conventional_range == (uint64_t)1024u * 4096u);
    assert(summary.highest_conventional_end == 0x40400000u);

    info.version++;
    assert(!chos_boot_info_validate(&info));
    info.version = CHOS_BOOT_INFO_VERSION;
    info.memory_descriptor_size = 8u;
    assert(!chos_boot_info_validate(&info));
    info.memory_descriptor_size = sizeof(chos_efi_memory_descriptor_t);
    info.memory_map_size++;
    assert(!chos_boot_info_validate(&info));
    info.memory_map_size = sizeof(map);
    info.memory_descriptor_count = 3u;

    /* Descriptor stride may be larger than the stable 40-byte prefix. */
    {
        chos_efi_memory_descriptor_t *d0 = (chos_efi_memory_descriptor_t *)(void *)((uint8_t *)extended_map_storage + 0u * 48u);
        chos_efi_memory_descriptor_t *d1 = (chos_efi_memory_descriptor_t *)(void *)((uint8_t *)extended_map_storage + 1u * 48u);
        chos_efi_memory_descriptor_t *d2 = (chos_efi_memory_descriptor_t *)(void *)((uint8_t *)extended_map_storage + 2u * 48u);
        d0->type = 7u; d0->physical_start = 0x10000000u; d0->number_of_pages = 256u;
        d1->type = 7u; d1->physical_start = 0x40000000u; d1->number_of_pages = 1024u;
        d2->type = 2u; d2->physical_start = 0x80000000u; d2->number_of_pages = 4096u;
    }
    init_info(&info, (uint8_t *)extended_map_storage, sizeof(extended_map_storage));
    info.memory_descriptor_size = 48u;
    info.memory_descriptor_count = 3u;
    assert(chos_boot_info_validate(&info));
    assert(chos_boot_info_memory_summary(&info, &summary));
    assert(summary.conventional_range_count == 2u);
    assert(summary.conventional_bytes == (uint64_t)(256u + 1024u) * 4096u);
    assert(summary.highest_conventional_end == 0x40400000u);

    info.framebuffer_stride = 100u; /* Cannot be narrower than visible mode. */
    assert(!chos_boot_info_validate(&info));

    /* A bus-sized MMIO aperture may be split across adjacent firmware
     * descriptors. Accept the union, but never bridge a gap or conventional RAM. */
    memset(map, 0, sizeof(map));
    map[0].type = 11u; map[0].physical_start = 0x40000000u; map[0].number_of_pages = 128u;
    map[1].type = 11u; map[1].physical_start = 0x40080000u; map[1].number_of_pages = 128u;
    map[2].type = 7u;  map[2].physical_start = 0x40100000u; map[2].number_of_pages = 256u;
    init_info(&info, (uint8_t *)map, sizeof(map));
    assert(chos_boot_info_validate(&info));
    assert(chos_boot_info_contains_mmio(&info, 0x40000000u, 0x100000u));
    assert(!chos_boot_info_contains_mmio(&info, 0x40000000u, 0x101000u));
    assert(!chos_boot_info_contains_mmio(&info, 0x40100000u, 0x1000u));
    /* Authoritative ACPI MMIO may be omitted, but never overlap RAM. */
    assert(chos_boot_info_mmio_safe(&info,0x08000000u,0x10000u));
    assert(!chos_boot_info_mmio_safe(&info,0x40100000u,0x1000u));
    assert(!chos_boot_info_mmio_safe(&info,UINT64_MAX-16,32));
    info.pci_window_count=CHOS_BOOT_MAX_PCI_WINDOWS+1;
    assert(!chos_boot_info_validate(&info));
    puts("UEFI boot-info ABI: descriptor stride, framebuffer bounds, map summaries and contiguous MMIO coverage passed");
    return 0;
}

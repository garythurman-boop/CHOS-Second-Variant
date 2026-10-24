#include <stddef.h>
#include <stdint.h>
#include "chos/boot_info.h"

#define EFI_CONVENTIONAL_MEMORY 7u
#define EFI_PAGE_SIZE UINT64_C(4096)

int chos_boot_info_validate(const chos_boot_info_t *info) {
    uint64_t required;
    if (info == NULL || info->magic != CHOS_BOOT_INFO_MAGIC ||
        info->version != CHOS_BOOT_INFO_VERSION ||
        info->struct_size < sizeof(*info) ||
        (info->flags & CHOS_BOOT_FLAG_UEFI) == 0u) return 0;
    if (info->memory_map_address == 0u || info->memory_map_size == 0u ||
        info->memory_descriptor_size < sizeof(chos_efi_memory_descriptor_t) ||
        info->memory_descriptor_size > 4096u ||
        info->memory_map_size % info->memory_descriptor_size != 0u ||
        info->memory_map_size / info->memory_descriptor_size > UINT32_MAX ||
        info->memory_descriptor_count != info->memory_map_size / info->memory_descriptor_size)
        return 0;
    if (info->memory_map_address > UINT64_MAX - info->memory_map_size ||
        info->pci_window_count > CHOS_BOOT_MAX_PCI_WINDOWS) return 0;
    for(uint32_t i=0;i<info->pci_window_count;++i) {
        const chos_pci_window_t *w=&info->pci_windows[i];
        if(!w->size || w->bus_base>UINT64_MAX-w->size || w->cpu_base>UINT64_MAX-w->size) return 0;
    }
    if (((info->flags & CHOS_BOOT_FLAG_FDT) != 0u) != (info->fdt_address != 0u)) return 0;
    if (((info->flags & CHOS_BOOT_FLAG_ACPI) != 0u) != (info->acpi_rsdp_address != 0u)) return 0;
    if (((info->flags & CHOS_BOOT_FLAG_UEFI_TEXT_INPUT) != 0u) !=
        (info->uefi_text_input_address != 0u)) return 0;
    if (((info->flags & CHOS_BOOT_FLAG_UEFI_POINTER_INPUT) != 0u) !=
        (info->uefi_simple_pointer_address != 0u)) return 0;
    if (((info->flags & CHOS_BOOT_FLAG_UEFI_ABSOLUTE_POINTER) != 0u) !=
        (info->uefi_absolute_pointer_address != 0u)) return 0;
    if ((info->flags & (CHOS_BOOT_FLAG_UEFI_TEXT_INPUT | CHOS_BOOT_FLAG_UEFI_POINTER_INPUT |
                        CHOS_BOOT_FLAG_UEFI_ABSOLUTE_POINTER)) != 0u &&
        (info->flags & CHOS_BOOT_FLAG_UEFI_SERVICES_ACTIVE) == 0u) return 0;
    if ((info->flags & CHOS_BOOT_FLAG_UEFI_SERVICES_ACTIVE) != 0u &&
        (info->uefi_text_input_address == 0u && info->uefi_simple_pointer_address == 0u &&
         info->uefi_absolute_pointer_address == 0u)) return 0;
    if ((info->flags & CHOS_BOOT_FLAG_FRAMEBUFFER) != 0u) {
        if (info->framebuffer_base == 0u || info->framebuffer_size < 4u ||
            info->framebuffer_width < 320u || info->framebuffer_width > 16384u ||
            info->framebuffer_height < 200u || info->framebuffer_height > 16384u ||
            info->framebuffer_stride < info->framebuffer_width ||
            info->framebuffer_stride > 32768u ||
            (info->framebuffer_pixel_format != CHOS_GOP_PIXEL_RGBX8 &&
             info->framebuffer_pixel_format != CHOS_GOP_PIXEL_BGRX8 &&
             info->framebuffer_pixel_format != CHOS_GOP_PIXEL_BITMASK) ||
            (uint64_t)info->framebuffer_stride > UINT64_MAX / 4u / info->framebuffer_height)
            return 0;
        required = (uint64_t)info->framebuffer_stride * info->framebuffer_height * 4u;
        if (required > info->framebuffer_size || info->framebuffer_base > UINT64_MAX - required)
            return 0;
        if (info->framebuffer_pixel_format == CHOS_GOP_PIXEL_BITMASK &&
            (info->framebuffer_red_mask == 0u || info->framebuffer_green_mask == 0u ||
             info->framebuffer_blue_mask == 0u ||
             (info->framebuffer_red_mask & info->framebuffer_green_mask) != 0u ||
             (info->framebuffer_red_mask & info->framebuffer_blue_mask) != 0u ||
             (info->framebuffer_green_mask & info->framebuffer_blue_mask) != 0u)) return 0;
    }
    if (info->kernel_image_size == 0u ||
        info->kernel_physical_base > UINT64_MAX - info->kernel_image_size ||
        info->kernel_entry < info->kernel_physical_base ||
        info->kernel_entry >= info->kernel_physical_base + info->kernel_image_size)
        return 0;
    return 1;
}


int chos_boot_info_contains_memory(const chos_boot_info_t *info, uint64_t address, uint64_t length) {
    const uint8_t *map;
    uint64_t offset, wanted_end;
    if (!chos_boot_info_validate(info) || address == 0u || length == 0u ||
        address > UINT64_MAX - length) return 0;
    wanted_end = address + length;
    map = (const uint8_t *)(uintptr_t)info->memory_map_address;
    for (offset = 0u; offset < info->memory_map_size; offset += info->memory_descriptor_size) {
        const chos_efi_memory_descriptor_t *desc =
            (const chos_efi_memory_descriptor_t *)(const void *)(map + offset);
        uint64_t bytes, end;
        if (desc->number_of_pages == 0u || desc->number_of_pages > UINT64_MAX / EFI_PAGE_SIZE) continue;
        bytes = desc->number_of_pages * EFI_PAGE_SIZE;
        if (desc->physical_start > UINT64_MAX - bytes) continue;
        end = desc->physical_start + bytes;
        if (address >= desc->physical_start && wanted_end <= end) return 1;
    }
    return 0;
}


int chos_boot_info_contains_mmio(const chos_boot_info_t *info, uint64_t address, uint64_t length) {
    const uint8_t *map;
    uint64_t wanted_end, cursor;
    uint32_t pass;
    if (!chos_boot_info_validate(info) || address == 0u || length == 0u ||
        address > UINT64_MAX - length) return 0;
    wanted_end = address + length;
    cursor = address;
    map = (const uint8_t *)(uintptr_t)info->memory_map_address;

    /* UEFI may split one ECAM aperture across adjacent descriptors. Accept a
     * contiguous union of ReservedMemory/MMIO descriptors, but never bridge a
     * gap or accept conventional/loader RAM as permission to touch registers. */
    for (pass = 0u; pass < info->memory_descriptor_count && cursor < wanted_end; ++pass) {
        uint64_t furthest = cursor;
        uint64_t offset;
        for (offset = 0u; offset < info->memory_map_size; offset += info->memory_descriptor_size) {
            const chos_efi_memory_descriptor_t *desc =
                (const chos_efi_memory_descriptor_t *)(const void *)(map + offset);
            uint64_t bytes, end;
            if ((desc->type != 0u && desc->type != 11u) || desc->number_of_pages == 0u ||
                desc->number_of_pages > UINT64_MAX / EFI_PAGE_SIZE) continue;
            bytes = desc->number_of_pages * EFI_PAGE_SIZE;
            if (desc->physical_start > UINT64_MAX - bytes) continue;
            end = desc->physical_start + bytes;
            if (desc->physical_start <= cursor && end > furthest) furthest = end;
        }
        if (furthest == cursor) return 0;
        cursor = furthest < wanted_end ? furthest : wanted_end;
    }
    return cursor == wanted_end;
}

int chos_boot_info_memory_summary(const chos_boot_info_t *info,
                                 chos_boot_memory_summary_t *summary) {
    const uint8_t *map;
    uint64_t offset;
    if (summary == NULL || !chos_boot_info_validate(info)) return 0;
    summary->conventional_bytes = 0u;
    summary->largest_conventional_range = 0u;
    summary->conventional_range_count = 0u;
    summary->highest_conventional_end = 0u;
    map = (const uint8_t *)(uintptr_t)info->memory_map_address;
    for (offset = 0u; offset < info->memory_map_size; offset += info->memory_descriptor_size) {
        const chos_efi_memory_descriptor_t *desc =
            (const chos_efi_memory_descriptor_t *)(const void *)(map + offset);
        uint64_t bytes, end;
        if (desc->type != EFI_CONVENTIONAL_MEMORY || desc->number_of_pages == 0u ||
            desc->number_of_pages > UINT64_MAX / EFI_PAGE_SIZE) continue;
        bytes = desc->number_of_pages * EFI_PAGE_SIZE;
        if (desc->physical_start > UINT64_MAX - bytes) continue;
        end = desc->physical_start + bytes;
        if (summary->conventional_bytes > UINT64_MAX - bytes) return 0;
        summary->conventional_bytes += bytes;
        if (bytes > summary->largest_conventional_range) summary->largest_conventional_range = bytes;
        if (end > summary->highest_conventional_end) summary->highest_conventional_end = end;
        ++summary->conventional_range_count;
    }
    return 1;
}

int chos_boot_info_mmio_safe(const chos_boot_info_t *info,uint64_t address,uint64_t length) {
    if(!chos_boot_info_validate(info) || !address || !length || address>UINT64_MAX-length) return 0;
    const uint8_t *map=(const uint8_t *)(uintptr_t)info->memory_map_address;
    for(uint64_t off=0;off<info->memory_map_size;off+=info->memory_descriptor_size) {
        const chos_efi_memory_descriptor_t *d=(const void *)(map+off);
        if(!d->number_of_pages) continue;
        if(d->number_of_pages>UINT64_MAX/4096 || d->physical_start>UINT64_MAX-d->number_of_pages*4096) return 0;
        uint64_t end=d->physical_start+d->number_of_pages*4096;
        if(d->physical_start<address+length && end>address && d->type!=0 && d->type!=11) return 0;
    }
    return 1;
}

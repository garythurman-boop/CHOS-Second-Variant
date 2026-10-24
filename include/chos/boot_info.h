#ifndef CHOS_BOOT_INFO_H
#define CHOS_BOOT_INFO_H

#include <stdint.h>

/* Stable EFI-to-kernel handoff ABI. The kernel owns the machine after
 * ExitBootServices. Physical addresses in this structure remain valid; UEFI
 * protocol function pointers must not be called from the kernel. */
#define CHOS_BOOT_INFO_MAGIC UINT64_C(0x43484f53424f4f54) /* CHOSBOOT */
#define CHOS_BOOT_INFO_VERSION 3u
#define CHOS_UEFI_ENTRY_MAGIC UINT64_C(0x43484f5355454631) /* CHOSUEF1 */

#define CHOS_BOOT_FLAG_UEFI        (UINT64_C(1) << 0)
#define CHOS_BOOT_FLAG_FDT         (UINT64_C(1) << 1)
#define CHOS_BOOT_FLAG_ACPI        (UINT64_C(1) << 2)
#define CHOS_BOOT_FLAG_FRAMEBUFFER  (UINT64_C(1) << 3)
#define CHOS_BOOT_FLAG_UEFI_TEXT_INPUT (UINT64_C(1) << 4)
#define CHOS_BOOT_FLAG_UEFI_POINTER_INPUT (UINT64_C(1) << 5)
#define CHOS_BOOT_FLAG_UEFI_SERVICES_ACTIVE (UINT64_C(1) << 6)
#define CHOS_BOOT_FLAG_UEFI_ABSOLUTE_POINTER (UINT64_C(1) << 7)

/* UEFI EFI_GRAPHICS_PIXEL_FORMAT values. */
#define CHOS_GOP_PIXEL_RGBX8 0u
#define CHOS_GOP_PIXEL_BGRX8 1u
#define CHOS_GOP_PIXEL_BITMASK 2u
#define CHOS_GOP_PIXEL_BLT_ONLY 3u

/* Layout matches the stable prefix of EFI_MEMORY_DESCRIPTOR.  Firmware may
 * append fields, so walk descriptors using memory_descriptor_size, never sizeof. */
typedef struct {
    uint32_t type;
    uint32_t pad;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attributes;
} chos_efi_memory_descriptor_t;

typedef struct {
    uint64_t magic;
    uint32_t version;
    uint32_t struct_size;
    uint64_t flags;
    uint64_t memory_map_address;
    uint64_t memory_map_size;
    uint64_t memory_descriptor_size;
    uint32_t memory_descriptor_version;
    uint32_t memory_descriptor_count;
    uint64_t kernel_physical_base;
    uint64_t kernel_image_size;
    uint64_t kernel_entry;
    uint64_t fdt_address;
    uint64_t acpi_rsdp_address;
    uint64_t framebuffer_base;
    uint64_t framebuffer_size;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_stride;
    uint32_t framebuffer_pixel_format;
    uint32_t framebuffer_red_mask;
    uint32_t framebuffer_green_mask;
    uint32_t framebuffer_blue_mask;
    uint32_t framebuffer_reserved_mask;
    /* Deprecated compatibility fields retained to preserve ABI layout.
     * The loader must leave these zero; native OS drivers provide input. */
    uint64_t uefi_text_input_address;
    uint64_t uefi_simple_pointer_address;
    uint64_t uefi_absolute_pointer_address;
} chos_boot_info_t;

typedef struct {
    uint64_t conventional_bytes;
    uint64_t largest_conventional_range;
    uint64_t conventional_range_count;
    uint64_t highest_conventional_end;
} chos_boot_memory_summary_t;

int chos_boot_info_validate(const chos_boot_info_t *info);
/* True when an address range is wholly contained in one advertised EFI descriptor. */
int chos_boot_info_contains_memory(const chos_boot_info_t *info, uint64_t address, uint64_t length);
/* True only for firmware descriptors typed ReservedMemory or MemoryMappedIO. */
int chos_boot_info_contains_mmio(const chos_boot_info_t *info, uint64_t address, uint64_t length);
int chos_boot_info_memory_summary(const chos_boot_info_t *info,
                                 chos_boot_memory_summary_t *summary);

#endif

#ifndef CHOS_PMM_H
#define CHOS_PMM_H

#include <stddef.h>
#include <stdint.h>
#include "chos/fdt.h"
#include "chos/boot_info.h"

#define CHOS_PAGE_SIZE 4096u
#define PMM_MAX_TRACKED_PAGES 4194304u /* 16 GiB at 4 KiB per page. */

typedef enum {
    PMM_OK = 0,
    PMM_ERR_NULL_ARGUMENT,
    PMM_ERR_BAD_DEVICE_TREE,
    PMM_ERR_BAD_RESERVED_RANGE,
    PMM_ERR_BAD_RAM_RANGE,
    PMM_ERR_OVERLAPPING_RAM,
    PMM_ERR_TOO_MUCH_RAM,
    PMM_ERR_NO_RAM,
    PMM_ERR_UNSUPPORTED_RESERVATION
} pmm_error_t;

typedef enum {
    PMM_INIT_STAGE_VALIDATING = 1,
    PMM_INIT_STAGE_REGIONS_READY,
    PMM_INIT_STAGE_BITMAPS_READY,
    PMM_INIT_STAGE_RESERVING,
    PMM_INIT_STAGE_RESERVATIONS_DONE,
    PMM_INIT_STAGE_UEFI_START = 6,
    PMM_INIT_STAGE_UEFI_MAP_NORMALIZED,
    PMM_INIT_STAGE_UEFI_CORE_ENTRY,
    PMM_INIT_STAGE_COUNTERS_OK,
    PMM_INIT_STAGE_CORE_READY
} pmm_init_stage_t;

/* Optional early-boot trace hook. The core supplies a weak no-op; platforms may
 * override it to emit progress without coupling the memory manager to a console. */
void pmm_init_progress(pmm_init_stage_t stage);

typedef struct {
    int initialized;
    pmm_error_t error;
    size_t region_count;
    uint64_t total_pages;
    uint64_t free_pages;
    uint64_t allocated_pages;
    uint64_t reserved_pages;
} pmm_stats_t;

/*
 * Initialize from normalized FDT RAM ranges. The allocator reserves the full
 * [kernel_start, kernel_end), DTB, and all static FDT reserved ranges. Dynamic
 * /reserved-memory requests without a physical base are rejected until placement exists.
 * MMU is assumed off / physical addresses identity accessible at this stage.
 */
pmm_error_t pmm_init(const fdt_info_t *tree,
                     uint64_t kernel_start, uint64_t kernel_end,
                     uint64_t dtb_start, uint64_t dtb_size);

/* Initialize from the preserved UEFI map. Only EfiConventionalMemory is made
 * free; loader, ACPI, MMIO, framebuffer, kernel and boot-map regions remain reserved. */
pmm_error_t pmm_init_from_uefi(const chos_boot_info_t *info);

/* IRQ-safe metadata locking; do not call from IRQ context. Heap may call PMM, not vice versa. */
/* Allocate count physically contiguous 4 KiB pages. Returns 1 on success, 0 on failure. */
int pmm_alloc_pages(size_t count, uint64_t *physical_address);

/* Free a contiguous range previously returned by pmm_alloc_pages. */
int pmm_free_pages(uint64_t physical_address, size_t count);

/* Single-page convenience wrappers. */
int pmm_alloc_page(uint64_t *physical_address);
int pmm_free_page(uint64_t physical_address);
/* True only when an aligned page belongs to a live PMM allocation. */
int pmm_is_allocated_page(uint64_t physical_address);

void pmm_get_stats(pmm_stats_t *out);
/* UEFI bootstrap uses lock-free PMM operations while running single-threaded
 * with IRQs masked. Call pmm_enable_runtime_locking() before enabling IRQs or
 * starting concurrent tasks. QEMU/FDT boot keeps runtime locking enabled. */
int pmm_is_bootstrap_mode(void);
void pmm_enable_runtime_locking(void);
const char *pmm_error_string(pmm_error_t error);

#endif

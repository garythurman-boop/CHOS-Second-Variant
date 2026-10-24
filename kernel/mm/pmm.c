#include <stddef.h>
#include <stdint.h>
#include "chos/fdt.h"
#include "chos/pmm.h"
#include "chos/spinlock.h"

#define PMM_REGION_LIMIT FDT_MAX_MEMORY_REGIONS
#define PMM_STATE_BITS 2u
#define PMM_STATES_PER_BYTE 4u
#define PMM_BITMAP_BYTES ((PMM_MAX_TRACKED_PAGES + PMM_STATES_PER_BYTE - 1u) / PMM_STATES_PER_BYTE)
#define PMM_RUNMAP_BYTES ((PMM_MAX_TRACKED_PAGES + 7u) / 8u)

/* The bitmap stores 2-bit states: 0=reserved, 1=free, 2=allocated, 3=invalid. */
enum {
    PAGE_RESERVED = 0u,
    PAGE_FREE = 1u,
    PAGE_ALLOCATED = 2u,
    PAGE_INVALID = 3u
};

typedef struct {
    uint64_t base;
    uint64_t end;
    uint64_t pages;
    uint64_t first_slot;
} pmm_region_t;

static uint8_t page_states[PMM_BITMAP_BYTES];
/* One-bit maps mark exact allocation starts and ends for safe multi-page frees. */
static uint8_t allocation_heads[PMM_RUNMAP_BYTES];
static uint8_t allocation_tails[PMM_RUNMAP_BYTES];
static pmm_region_t regions[PMM_REGION_LIMIT];
static size_t region_count;
static uint64_t total_pages;
static uint64_t free_pages;
static uint64_t allocated_pages;
static uint64_t reserved_pages;
static uint64_t next_hint;
static int initialized;
static pmm_error_t last_error = PMM_ERR_BAD_DEVICE_TREE;
/* Lock order: heap may acquire PMM; PMM must never call into the heap. */
static chos_spinlock_t pmm_lock = CHOS_SPINLOCK_INITIALIZER;
/* UEFI's first initialization phase runs with IRQs masked and without runtime
 * clients. Avoid taking a kernel spinlock before interrupt/vector state and the
 * heap are fully initialized. This mode is enabled only by the UEFI adapter and
 * is ended explicitly immediately before runtime IRQ delivery is enabled. */
static volatile uint32_t pmm_bootstrap_mode;

int pmm_is_bootstrap_mode(void) {
    return __atomic_load_n(&pmm_bootstrap_mode, __ATOMIC_ACQUIRE) != 0u;
}

void pmm_enable_runtime_locking(void) {
    /* Caller contract: single-core bootstrap, local IRQs still masked, and no
     * runtime PMM users exist yet. Establish a known lock state before publish. */
    __atomic_store_n(&pmm_lock.held, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&pmm_bootstrap_mode, 0u, __ATOMIC_RELEASE);
}

__attribute__((weak)) void pmm_init_progress(pmm_init_stage_t stage) {
    (void)stage;
}

static void zero_bytes(void *ptr, size_t length) {
    uint8_t *p = (uint8_t *)ptr;
    size_t i;
    for (i = 0; i < length; ++i) p[i] = 0u;
}

static void fill_bytes(void *ptr, uint8_t value, size_t length) {
    uint8_t *p = (uint8_t *)ptr;
    size_t i;
    for (i = 0; i < length; ++i) p[i] = value;
}

static int add_overflow_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (b > UINT64_MAX - a) return 1;
    *out = a + b;
    return 0;
}

static int align_up_page(uint64_t value, uint64_t *out) {
    const uint64_t mask = (uint64_t)CHOS_PAGE_SIZE - 1u;
    if (value > UINT64_MAX - mask) return 0;
    *out = (value + mask) & ~mask;
    return 1;
}

static uint8_t state_get(uint64_t slot) {
    size_t byte_index = (size_t)(slot / PMM_STATES_PER_BYTE);
    unsigned shift = (unsigned)(slot % PMM_STATES_PER_BYTE) * PMM_STATE_BITS;
    return (uint8_t)((page_states[byte_index] >> shift) & 0x3u);
}

static void state_set(uint64_t slot, uint8_t state) {
    size_t byte_index = (size_t)(slot / PMM_STATES_PER_BYTE);
    unsigned shift = (unsigned)(slot % PMM_STATES_PER_BYTE) * PMM_STATE_BITS;
    uint8_t mask = (uint8_t)(0x3u << shift);
    page_states[byte_index] = (uint8_t)((page_states[byte_index] & (uint8_t)~mask) |
                                        (uint8_t)((state & 0x3u) << shift));
}

static int runmap_get(const uint8_t *map, uint64_t slot) {
    return (map[slot / 8u] & (uint8_t)(1u << (slot % 8u))) != 0u;
}

static void runmap_set(uint8_t *map, uint64_t slot, int value) {
    uint8_t mask = (uint8_t)(1u << (slot % 8u));
    if (value) map[slot / 8u] |= mask;
    else map[slot / 8u] &= (uint8_t)~mask;
}

/* Mark only pages intersecting a reserved physical range. RAM begins free in the
 * packed bitmap, so we avoid walking every page of multi-gigabyte firmware RAM
 * merely to decide it is free. Duplicate/overlapping reservations are counted once. */
static pmm_error_t reserve_range_unlocked(uint64_t start, uint64_t length) {
    uint64_t raw_end, page_start, page_end;
    size_t i;
    if (length == 0u) return PMM_OK;
    if (add_overflow_u64(start, length, &raw_end) || !align_up_page(raw_end, &page_end))
        return PMM_ERR_BAD_RESERVED_RANGE;
    page_start = start & ~((uint64_t)CHOS_PAGE_SIZE - 1u);
    for (i = 0u; i < region_count; ++i) {
        uint64_t lo = page_start > regions[i].base ? page_start : regions[i].base;
        uint64_t hi = page_end < regions[i].end ? page_end : regions[i].end;
        uint64_t first, limit, slot;
        if (lo >= hi) continue;
        first = regions[i].first_slot + ((lo - regions[i].base) / CHOS_PAGE_SIZE);
        limit = regions[i].first_slot + ((hi - regions[i].base) / CHOS_PAGE_SIZE);
        for (slot = first; slot < limit; ++slot) {
            if (state_get(slot) == PAGE_FREE) {
                state_set(slot, PAGE_RESERVED);
                if (free_pages == 0u) return PMM_ERR_BAD_RESERVED_RANGE;
                --free_pages;
                ++reserved_pages;
            }
        }
    }
    return PMM_OK;
}

static void sort_regions(void) {
    size_t i;
    for (i = 1u; i < region_count; ++i) {
        pmm_region_t current = regions[i];
        size_t j = i;
        while (j > 0u && regions[j - 1u].base > current.base) {
            regions[j] = regions[j - 1u];
            --j;
        }
        regions[j] = current;
    }
}

static int slot_to_address(uint64_t slot, uint64_t *address) {
    size_t i;
    if (address == NULL || slot >= total_pages) return 0;
    for (i = 0; i < region_count; ++i) {
        uint64_t region_slot_end = regions[i].first_slot + regions[i].pages;
        if (slot >= regions[i].first_slot && slot < region_slot_end) {
            uint64_t offset = (slot - regions[i].first_slot) * CHOS_PAGE_SIZE;
            *address = regions[i].base + offset;
            return 1;
        }
    }
    return 0;
}

static int address_to_slot(uint64_t address, uint64_t *slot) {
    size_t i;
    if (slot == NULL || (address & ((uint64_t)CHOS_PAGE_SIZE - 1u)) != 0u) return 0;
    for (i = 0; i < region_count; ++i) {
        if (address >= regions[i].base && address < regions[i].end) {
            *slot = regions[i].first_slot + ((address - regions[i].base) / CHOS_PAGE_SIZE);
            return 1;
        }
    }
    return 0;
}

static pmm_error_t pmm_init_unlocked(const fdt_info_t *tree,
                     uint64_t kernel_start, uint64_t kernel_end,
                     uint64_t dtb_start, uint64_t dtb_size) {
    uint64_t dtb_end = 0u;
    uint64_t slot_cursor = 0u;
    size_t i;

    initialized = 0;
    last_error = PMM_ERR_BAD_DEVICE_TREE;
    region_count = 0u;
    total_pages = 0u;
    free_pages = 0u;
    allocated_pages = 0u;
    reserved_pages = 0u;
    next_hint = 0u;
    zero_bytes(regions, sizeof(regions));
    pmm_init_progress(PMM_INIT_STAGE_VALIDATING);

    if (tree == NULL) {
        last_error = PMM_ERR_NULL_ARGUMENT;
        return last_error;
    }
    if (!tree->valid || tree->memory_count == 0u ||
        tree->memory_count > PMM_REGION_LIMIT ||
        tree->reserved_count > FDT_MAX_RESERVED_REGIONS) {
        last_error = PMM_ERR_BAD_DEVICE_TREE;
        return last_error;
    }
    if (tree->reserved_dynamic_count != 0u) {
        last_error = PMM_ERR_UNSUPPORTED_RESERVATION;
        return last_error;
    }
    if (kernel_start >= kernel_end || dtb_size == 0u ||
        add_overflow_u64(dtb_start, dtb_size, &dtb_end)) {
        last_error = PMM_ERR_BAD_RESERVED_RANGE;
        return last_error;
    }
    for (i = 0u; i < tree->reserved_count; ++i) {
        uint64_t reservation_end;
        if (tree->reserved[i].size == 0u ||
            add_overflow_u64(tree->reserved[i].base, tree->reserved[i].size,
                             &reservation_end) ||
            reservation_end <= tree->reserved[i].base) {
            last_error = PMM_ERR_BAD_RESERVED_RANGE;
            return last_error;
        }
    }

    for (i = 0u; i < tree->memory_count; ++i) {
        uint64_t raw_end, aligned_start, aligned_end;
        if (tree->memory[i].size == 0u ||
            add_overflow_u64(tree->memory[i].base, tree->memory[i].size, &raw_end) ||
            !align_up_page(tree->memory[i].base, &aligned_start)) {
            last_error = PMM_ERR_BAD_RAM_RANGE;
            return last_error;
        }
        aligned_end = raw_end & ~((uint64_t)CHOS_PAGE_SIZE - 1u);
        if (aligned_end <= aligned_start) continue;
        if (region_count >= PMM_REGION_LIMIT) {
            last_error = PMM_ERR_BAD_RAM_RANGE;
            return last_error;
        }
        regions[region_count].base = aligned_start;
        regions[region_count].end = aligned_end;
        regions[region_count].pages = (aligned_end - aligned_start) / CHOS_PAGE_SIZE;
        ++region_count;
    }

    if (region_count == 0u) {
        last_error = PMM_ERR_NO_RAM;
        return last_error;
    }

    sort_regions();
    for (i = 0u; i < region_count; ++i) {
        if (i > 0u && regions[i].base < regions[i - 1u].end) {
            last_error = PMM_ERR_OVERLAPPING_RAM;
            return last_error;
        }
        if (regions[i].pages > PMM_MAX_TRACKED_PAGES - slot_cursor) {
            last_error = PMM_ERR_TOO_MUCH_RAM;
            return last_error;
        }
        regions[i].first_slot = slot_cursor;
        slot_cursor += regions[i].pages;
    }
    total_pages = slot_cursor;
    pmm_init_progress(PMM_INIT_STAGE_REGIONS_READY);

    /* Reset only the active portion of the maps. Clearing the full 16 GiB
     * capacity here needlessly touches ~2 MiB on every boot while IRQs are
     * masked, even when firmware reports only 2 GiB of RAM. Slots at or above
     * total_pages are inaccessible, so stale bytes beyond this active prefix
     * cannot be observed by alloc/free operations. */
    {
        size_t state_bytes = (size_t)((total_pages + PMM_STATES_PER_BYTE - 1u) / PMM_STATES_PER_BYTE);
        size_t runmap_bytes = (size_t)((total_pages + 7u) / 8u);
        fill_bytes(page_states, 0x55u, state_bytes);
        zero_bytes(allocation_heads, runmap_bytes);
        zero_bytes(allocation_tails, runmap_bytes);
    }
    pmm_init_progress(PMM_INIT_STAGE_BITMAPS_READY);
    free_pages = total_pages;
    reserved_pages = 0u;
    {
        pmm_error_t reserve_result;
        pmm_init_progress(PMM_INIT_STAGE_RESERVING);
        reserve_result = reserve_range_unlocked(kernel_start, kernel_end - kernel_start);
        if (reserve_result == PMM_OK) reserve_result = reserve_range_unlocked(dtb_start, dtb_size);
        for (i = 0u; reserve_result == PMM_OK && i < tree->reserved_count; ++i)
            reserve_result = reserve_range_unlocked(tree->reserved[i].base, tree->reserved[i].size);
        if (reserve_result != PMM_OK) {
            last_error = reserve_result;
            region_count = 0u;
            total_pages = free_pages = allocated_pages = reserved_pages = 0u;
            return last_error;
        }
    }

    pmm_init_progress(PMM_INIT_STAGE_RESERVATIONS_DONE);

    /* Counter sanity protects against malformed maps/reservations. */
    if (total_pages == 0u || total_pages > PMM_MAX_TRACKED_PAGES ||
        free_pages + reserved_pages != total_pages) {
        last_error = PMM_ERR_BAD_RAM_RANGE;
        region_count = 0u;
        total_pages = free_pages = allocated_pages = reserved_pages = 0u;
        return last_error;
    }

    pmm_init_progress(PMM_INIT_STAGE_COUNTERS_OK);
    initialized = 1;
    last_error = PMM_OK;
    pmm_init_progress(PMM_INIT_STAGE_CORE_READY);
    return PMM_OK;
}


pmm_error_t pmm_init(const fdt_info_t *tree,
                     uint64_t kernel_start, uint64_t kernel_end,
                     uint64_t dtb_start, uint64_t dtb_size) {
    pmm_error_t result;
    /* This is a boot-only operation: no runtime PMM users or IRQ handlers are
     * enabled yet. Explicitly establish the lock state before and after the
     * bootstrap transaction. This also makes early lock state deterministic on
     * firmware paths and ensures pmm_get_stats() cannot inherit a stale held bit.
     * Never hold the lock while the progress hook emits framebuffer diagnostics. */
    __atomic_store_n(&pmm_lock.held, 0u, __ATOMIC_RELAXED);
    result = pmm_init_unlocked(tree, kernel_start, kernel_end, dtb_start, dtb_size);
    __atomic_store_n(&pmm_lock.held, 0u, __ATOMIC_RELEASE);
    return result;
}

static int pmm_alloc_pages_unlocked(size_t count, uint64_t *physical_address) {
    uint64_t pass;
    uint64_t need;

    if (physical_address == NULL || !initialized || count == 0u ||
        free_pages < (uint64_t)count || total_pages == 0u) {
        return 0;
    }
    need = (uint64_t)count;
    if (need > total_pages) return 0;

    /* Search candidate runs. A run must be both free in the bitmap and
       physically contiguous; slot adjacency alone is not sufficient when
       the FDT reports disjoint RAM regions. */
    for (pass = 0u; pass < total_pages; ++pass) {
        uint64_t slot = (next_hint + pass) % total_pages;
        uint64_t base;
        uint64_t offset;
        int run_ok = 1;

        if (state_get(slot) != PAGE_FREE || need > total_pages - slot) continue;
        if (!slot_to_address(slot, &base)) continue;

        for (offset = 1u; offset < need; ++offset) {
            uint64_t candidate_address;
            if (state_get(slot + offset) != PAGE_FREE ||
                !slot_to_address(slot + offset, &candidate_address) ||
                candidate_address != base + offset * CHOS_PAGE_SIZE) {
                run_ok = 0;
                break;
            }
        }
        if (!run_ok) continue;

        for (offset = 0u; offset < need; ++offset) {
            state_set(slot + offset, PAGE_ALLOCATED);
        }
        runmap_set(allocation_heads, slot, 1);
        runmap_set(allocation_tails, slot + need - 1u, 1);
        free_pages -= need;
        allocated_pages += need;
        next_hint = (slot + need) % total_pages;
        *physical_address = base;
        return 1;
    }
    return 0;
}


/* UEFI adapter: normalize conventional descriptors into the same checked region
 * model used by the FDT path. The EFI map is a physical byte stream whose stride
 * may exceed the stable descriptor prefix; never cast and increment by sizeof. */
pmm_error_t pmm_init_from_uefi(const chos_boot_info_t *info) {
    /* This object is large because the shared FDT normalizer also contains a
     * bounded device inventory. Keep it out of the 64 KiB early boot stack. */
    static fdt_info_t normalized;
    const uint8_t *map;
    uint64_t offset, kernel_end;
    size_t i;

    if (!chos_boot_info_validate(info)) return PMM_ERR_NULL_ARGUMENT;
    /* No scheduler/task concurrency exists in this EFI bring-up path. Keep PMM
     * operations lock-free before the first trace callback until the kernel
     * explicitly publishes runtime mode. */
    __atomic_store_n(&pmm_lock.held, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&pmm_bootstrap_mode, 1u, __ATOMIC_RELEASE);
    pmm_init_progress(PMM_INIT_STAGE_UEFI_START);
    zero_bytes(&normalized, sizeof(normalized));
    normalized.valid = 1;
    normalized.error = FDT_OK;
    normalized.reserved_dynamic_count = 0u;
    map = (const uint8_t *)(uintptr_t)info->memory_map_address;

    for (offset = 0u; offset < info->memory_map_size; offset += info->memory_descriptor_size) {
        const chos_efi_memory_descriptor_t *desc =
            (const chos_efi_memory_descriptor_t *)(const void *)(map + offset);
        uint64_t bytes, end;
        fdt_memory_region_t current;
        if (desc->type != 7u || desc->number_of_pages == 0u ||
            desc->number_of_pages > UINT64_MAX / CHOS_PAGE_SIZE) continue;
        bytes = desc->number_of_pages * CHOS_PAGE_SIZE;
        if (desc->physical_start > UINT64_MAX - bytes) return PMM_ERR_BAD_RAM_RANGE;
        end = desc->physical_start + bytes;
        current.base = desc->physical_start;
        current.size = bytes;

        /* Coalesce adjacent conventional ranges; UEFI descriptors can be split
         * for bookkeeping even when the represented RAM is physically adjacent. */
        for (i = 0u; i < normalized.memory_count; ++i) {
            uint64_t existing_end;
            if (normalized.memory[i].size > UINT64_MAX - normalized.memory[i].base)
                return PMM_ERR_BAD_RAM_RANGE;
            existing_end = normalized.memory[i].base + normalized.memory[i].size;
            if (existing_end == current.base) {
                if (current.size > UINT64_MAX - normalized.memory[i].size) return PMM_ERR_BAD_RAM_RANGE;
                normalized.memory[i].size += current.size;
                current.size = 0u;
                break;
            }
            if (end == normalized.memory[i].base) {
                if (normalized.memory[i].size > UINT64_MAX - current.size) return PMM_ERR_BAD_RAM_RANGE;
                normalized.memory[i].base = current.base;
                normalized.memory[i].size += current.size;
                current.size = 0u;
                break;
            }
            if (current.base < existing_end && normalized.memory[i].base < end)
                return PMM_ERR_OVERLAPPING_RAM;
        }
        if (current.size == 0u) continue;
        if (normalized.memory_count >= FDT_MAX_MEMORY_REGIONS) return PMM_ERR_TOO_MUCH_RAM;
        normalized.memory[normalized.memory_count++] = current;
    }

    if (normalized.memory_count == 0u) return PMM_ERR_NO_RAM;
    pmm_init_progress(PMM_INIT_STAGE_UEFI_MAP_NORMALIZED);
    /* Sort regions: firmware memory-map ordering is not an ABI guarantee. */
    for (i = 1u; i < normalized.memory_count; ++i) {
        fdt_memory_region_t v = normalized.memory[i];
        size_t j = i;
        while (j > 0u && normalized.memory[j - 1u].base > v.base) {
            normalized.memory[j] = normalized.memory[j - 1u];
            --j;
        }
        normalized.memory[j] = v;
    }

    /* The EFI boot-info record and firmware table roots are not normally
     * conventional RAM, but explicitly reserve them if a nonconforming map
     * classifies any overlapping page as conventional. */
#define ADD_UEFI_RESERVATION(addr_, size_) do { \
        uint64_t a_ = (uint64_t)(addr_), z_ = (uint64_t)(size_); \
        if (a_ != 0u && z_ != 0u && a_ <= UINT64_MAX - z_) { \
            if (normalized.reserved_count < FDT_MAX_RESERVED_REGIONS) { \
                normalized.reserved[normalized.reserved_count].base = a_; \
                normalized.reserved[normalized.reserved_count].size = z_; \
                normalized.reserved[normalized.reserved_count].source = 1u; \
                ++normalized.reserved_count; \
            } else return PMM_ERR_BAD_RESERVED_RANGE; \
        } \
    } while (0)
    ADD_UEFI_RESERVATION((uintptr_t)info, sizeof(*info));
    ADD_UEFI_RESERVATION(info->acpi_rsdp_address, 36u);
    /* A conforming UEFI map describes the framebuffer as MMIO, not conventional
     * RAM, but reserve it explicitly in case firmware reports overlapping
     * conventional ranges. The PMM must never hand framebuffer pages to heap/users. */
    if ((info->flags & CHOS_BOOT_FLAG_FRAMEBUFFER) != 0u)
        ADD_UEFI_RESERVATION(info->framebuffer_base, info->framebuffer_size);
    if ((info->flags & CHOS_BOOT_FLAG_FDT) != 0u) {
        uint64_t fdt_size = 40u;
        if (!chos_boot_info_contains_memory(info, info->fdt_address, 40u))
            return PMM_ERR_BAD_RESERVED_RANGE;
        {
            const volatile uint8_t *fdt = (const volatile uint8_t *)(uintptr_t)info->fdt_address;
            uint64_t declared = ((uint64_t)fdt[4] << 24) | ((uint64_t)fdt[5] << 16) |
                               ((uint64_t)fdt[6] << 8) | fdt[7];
            if (declared >= 40u && declared <= 1024u * 1024u &&
                chos_boot_info_contains_memory(info, info->fdt_address, declared)) fdt_size = declared;
        }
        ADD_UEFI_RESERVATION(info->fdt_address, fdt_size);
    }
#undef ADD_UEFI_RESERVATION

    if (info->kernel_image_size > UINT64_MAX - info->kernel_physical_base)
        return PMM_ERR_BAD_RESERVED_RANGE;
    kernel_end = info->kernel_physical_base + info->kernel_image_size;
    pmm_init_progress(PMM_INIT_STAGE_UEFI_CORE_ENTRY);
    return pmm_init(&normalized, info->kernel_physical_base, kernel_end,
                    info->memory_map_address, info->memory_map_size);
}

int pmm_alloc_pages(size_t count, uint64_t *physical_address) {
    uint64_t irq_flags;
    int result;
    if (pmm_is_bootstrap_mode()) return pmm_alloc_pages_unlocked(count, physical_address);
    irq_flags = chos_spin_lock_irqsave(&pmm_lock);
    result = pmm_alloc_pages_unlocked(count, physical_address);
    chos_spin_unlock_irqrestore(&pmm_lock, irq_flags);
    return result;
}

static int pmm_free_pages_unlocked(uint64_t physical_address, size_t count) {
    uint64_t first_slot;
    uint64_t need;
    uint64_t offset;

    if (!initialized || count == 0u ||
        !address_to_slot(physical_address, &first_slot)) return 0;
    need = (uint64_t)count;
    if (need > total_pages - first_slot) return 0;
    if (!runmap_get(allocation_heads, first_slot) ||
        !runmap_get(allocation_tails, first_slot + need - 1u)) return 0;

    /* Validate the entire range before mutating any state. */
    for (offset = 0u; offset < need; ++offset) {
        uint64_t candidate_address;
        if (state_get(first_slot + offset) != PAGE_ALLOCATED ||
            !slot_to_address(first_slot + offset, &candidate_address) ||
            candidate_address != physical_address + offset * CHOS_PAGE_SIZE) {
            return 0;
        }
        /* Reject merging adjacent, separately returned allocations into a
           single free operation; only the exact original run may be freed. */
        if (offset > 0u &&
            (runmap_get(allocation_heads, first_slot + offset) ||
             runmap_get(allocation_tails, first_slot + offset - 1u))) {
            return 0;
        }
    }
    for (offset = 0u; offset < need; ++offset) {
        state_set(first_slot + offset, PAGE_FREE);
    }
    runmap_set(allocation_heads, first_slot, 0);
    runmap_set(allocation_tails, first_slot + need - 1u, 0);
    free_pages += need;
    allocated_pages -= need;
    if (first_slot < next_hint) next_hint = first_slot;
    return 1;
}


int pmm_free_pages(uint64_t physical_address, size_t count) {
    uint64_t irq_flags;
    int result;
    if (pmm_is_bootstrap_mode()) return pmm_free_pages_unlocked(physical_address, count);
    irq_flags = chos_spin_lock_irqsave(&pmm_lock);
    result = pmm_free_pages_unlocked(physical_address, count);
    chos_spin_unlock_irqrestore(&pmm_lock, irq_flags);
    return result;
}

int pmm_alloc_page(uint64_t *physical_address) {
    return pmm_alloc_pages(1u, physical_address);
}

int pmm_free_page(uint64_t physical_address) {
    return pmm_free_pages(physical_address, 1u);
}

int pmm_is_allocated_page(uint64_t physical_address) {
    uint64_t irq_flags;
    uint64_t slot = 0u;
    int result;
    if (pmm_is_bootstrap_mode()) {
        return initialized && address_to_slot(physical_address, &slot) &&
               state_get(slot) == PAGE_ALLOCATED;
    }
    irq_flags = chos_spin_lock_irqsave(&pmm_lock);
    result = initialized && address_to_slot(physical_address, &slot) &&
             state_get(slot) == PAGE_ALLOCATED;
    chos_spin_unlock_irqrestore(&pmm_lock, irq_flags);
    return result;
}

static void pmm_get_stats_unlocked(pmm_stats_t *out) {
    out->initialized = initialized;
    out->error = last_error;
    out->region_count = region_count;
    out->total_pages = total_pages;
    out->free_pages = free_pages;
    out->allocated_pages = allocated_pages;
    out->reserved_pages = reserved_pages;
}

void pmm_get_stats(pmm_stats_t *out) {
    uint64_t irq_flags;
    if (out == NULL) return;
    /* During EFI bootstrap there is one execution context and IRQs are masked;
     * taking a runtime spinlock is both unnecessary and unsafe before the
     * platform has published runtime locking. Once enabled, use the normal lock. */
    if (pmm_is_bootstrap_mode()) {
        pmm_get_stats_unlocked(out);
        return;
    }
    irq_flags = chos_spin_lock_irqsave(&pmm_lock);
    pmm_get_stats_unlocked(out);
    chos_spin_unlock_irqrestore(&pmm_lock, irq_flags);
}

const char *pmm_error_string(pmm_error_t error) {
    switch (error) {
        case PMM_OK: return "OK";
        case PMM_ERR_NULL_ARGUMENT: return "null argument";
        case PMM_ERR_BAD_DEVICE_TREE: return "device tree is invalid or contains no RAM";
        case PMM_ERR_BAD_RESERVED_RANGE: return "invalid kernel or device-tree reservation";
        case PMM_ERR_BAD_RAM_RANGE: return "invalid RAM range";
        case PMM_ERR_OVERLAPPING_RAM: return "overlapping RAM regions";
        case PMM_ERR_TOO_MUCH_RAM: return "RAM exceeds the 16 GiB tracking limit";
        case PMM_ERR_NO_RAM: return "no page-aligned RAM available";
        case PMM_ERR_UNSUPPORTED_RESERVATION: return "dynamic /reserved-memory request cannot yet be placed safely";
        default: return "unknown page allocator error";
    }
}

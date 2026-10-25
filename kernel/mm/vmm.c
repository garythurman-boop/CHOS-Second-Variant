#include <stddef.h>
#include <stdint.h>
#include "chos/fdt.h"
#include "chos/platform.h"
#include "chos/vmm.h"
#include "chos/pmm.h"
#include "chos/task_stack_layout.h"
#include "chos/uefi_vmm.h"

#define MAIR_ATTR_NORMAL_WBWA 0xffull
#define MAIR_ATTR_DEVICE_NGNRE 0x04ull
#define MAIR_EL1_VALUE (MAIR_ATTR_NORMAL_WBWA | (MAIR_ATTR_DEVICE_NGNRE << 8))
#define TCR_T0SZ_39BIT          25ull
#define TCR_IRGN0_WBWA          (1ull << 8)
#define TCR_ORGN0_WBWA          (1ull << 10)
#define TCR_SH0_INNER           (3ull << 12)
#define TCR_EPD1_DISABLE_TTBR1  (1ull << 23)
#define TCR_IPS_40BIT           (2ull << 32)
#define TCR_EL1_VALUE (TCR_T0SZ_39BIT | TCR_IRGN0_WBWA | TCR_ORGN0_WBWA | \
                       TCR_SH0_INNER | TCR_EPD1_DISABLE_TTBR1 | TCR_IPS_40BIT)
#define SCTLR_M                 (1ull << 0)
#define SCTLR_C                 (1ull << 2)
#define SCTLR_I                 (1ull << 12)
#define SCTLR_WXN               (1ull << 19)
#define SCTLR_ENABLE_MASK       (SCTLR_M | SCTLR_C | SCTLR_I | SCTLR_WXN)
#define DESC_VALID              (1ull << 0)
#define DESC_TABLE_OR_PAGE      (1ull << 1)
#define DESC_ADDR_MASK          0x0000fffffffff000ull
#define ATTRIDX_SHIFT           2u
#define ATTRIDX_MASK            (7ull << ATTRIDX_SHIFT)
#define AP_SHIFT                6u
#define AP_MASK                 (3ull << AP_SHIFT)
#define DESC_PXN                (1ull << 53)
#define DESC_UXN                (1ull << 54)
#define L1_ENTRIES              512u
#define L2_ENTRIES              512u
#define L3_ENTRIES              512u
#define INVALID_L3_SLOT         0xffffu
#define GIC_WINDOW_SIZE          0x10000ull
#define UART_WINDOW_SIZE         0x1000ull

static uint64_t l1_table[L1_ENTRIES] __attribute__((aligned(VMM_PAGE_SIZE)));
static uint64_t l2_device[L2_ENTRIES] __attribute__((aligned(VMM_PAGE_SIZE)));
static uint64_t l2_ram[L2_ENTRIES] __attribute__((aligned(VMM_PAGE_SIZE)));
static uint64_t l3_pool[VMM_MAX_L3_TABLES][L3_ENTRIES] __attribute__((aligned(VMM_PAGE_SIZE)));
static uint16_t l3_slots[2][L2_ENTRIES];
#define DYNAMIC_BITMAP_BYTES VMM_DYNAMIC_BITMAP_BYTES
static uint8_t dynamic_map_bitmap[DYNAMIC_BITMAP_BYTES]; /* owned dynamic aliases only */
static uint8_t dynamic_candidate_bitmap[DYNAMIC_BITMAP_BYTES]; /* identity RAM + aliases */
static uint64_t highest_ram_end;
static uint64_t dynamic_search_hint = VMM_DYNAMIC_VA_BASE;
static uint32_t dynamic_mappings;
static uint32_t l3_tables_used;
static uint32_t mapped_device_pages;
static uint32_t mapped_ram_pages;
static int prepared;
static int uefi_profile;
extern int vmm_uefi_build(const chos_boot_info_t *,const chos_acpi_info_t *,const chos_pci_device_t *,size_t,uint64_t);
extern uint64_t vmm_uefi_root(void);
extern void vmm_uefi_map_stats(uint32_t *,uint32_t *,uint32_t *);
extern int vmm_uefi_copy_l2(unsigned,uint64_t *);
extern int vmm_uefi_copy_l1(uint64_t *);
static int enabled_by_chos;
static vmm_error_t last_error = VMM_ERR_NOT_PREPARED;
static volatile uint64_t vmm_probe_word;

extern uint8_t __text_start, __text_end;
extern uint8_t __rodata_start, __rodata_end;
extern uint8_t __data_start, __data_end;
extern uint8_t __bss_start, __bss_end;
extern uint8_t __stack_guard_start;
extern uint8_t __task_stacks_start, __task_stacks_end;

static void zero_bytes(void *memory, size_t length) {
    uint8_t *bytes = (uint8_t *)memory;
    size_t i;
    for (i = 0u; i < length; ++i) bytes[i] = 0u;
}

static uint64_t read_sctlr_el1(void) {
    uint64_t value;
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(value));
    return value;
}

static uint64_t read_ttbr0_el1(void) {
    uint64_t value;
    __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(value));
    return value;
}

static uint64_t save_and_mask_irqs(void) {
    uint64_t daif;
    __asm__ volatile("mrs %0, daif" : "=r"(daif));
    __asm__ volatile("msr daifset, #2\n\tisb" ::: "memory");
    return daif;
}

static void restore_daif(uint64_t daif) {
    __asm__ volatile("msr daif, %0\n\tisb" :: "r"(daif) : "memory");
}

/* Serialize diagnostic state against timer-preempted tasks on this single CPU. */
static void record_last_error(vmm_error_t error) {
    uint64_t daif = save_and_mask_irqs();
    last_error = error;
    restore_daif(daif);
}

static uint64_t next_dynamic_hint(uint64_t address) {
    address += VMM_PAGE_SIZE;
    return address >= VMM_DYNAMIC_VA_LIMIT ? VMM_DYNAMIC_VA_BASE : address;
}

static size_t dynamic_bitmap_index(uint64_t virtual_address) {
    return (size_t)((virtual_address - VMM_DYNAMIC_VA_BASE) / VMM_PAGE_SIZE);
}

static int dynamic_bitmap_get(uint64_t virtual_address) {
    size_t index;
    if (virtual_address < VMM_DYNAMIC_VA_BASE || virtual_address >= VMM_DYNAMIC_VA_LIMIT) return 0;
    index = dynamic_bitmap_index(virtual_address);
    return (dynamic_map_bitmap[index / 8u] & (uint8_t)(1u << (index % 8u))) != 0u;
}

static void dynamic_bitmap_set(uint64_t virtual_address, int value) {
    size_t index = dynamic_bitmap_index(virtual_address);
    uint8_t mask = (uint8_t)(1u << (index % 8u));
    if (value) dynamic_map_bitmap[index / 8u] |= mask;
    else dynamic_map_bitmap[index / 8u] &= (uint8_t)~mask;
    if (value) dynamic_candidate_bitmap[index / 8u] |= mask;
    else dynamic_candidate_bitmap[index / 8u] &= (uint8_t)~mask;
}

static void clean_range_to_poc(const void *memory, size_t length) {
    uint64_t ctr;
    uintptr_t line_size, start, end, address;
    if (memory == NULL || length == 0u) return;
    __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
    line_size = (uintptr_t)4u << ((ctr >> 16) & 0x0fu);
    if (line_size < 4u || (line_size & (line_size - 1u)) != 0u) line_size = 64u;
    start = (uintptr_t)memory & ~(line_size - 1u);
    end = (uintptr_t)memory + length;
    for (address = start; address < end; address += line_size) {
        __asm__ volatile("dc cvac, %0" :: "r"(address) : "memory");
    }
    __asm__ volatile("dsb sy" ::: "memory");
}

static int interval_end(uint64_t base, uint64_t size, uint64_t *end) {
    if (size == 0u || end == NULL || base > UINT64_MAX - size) return 0;
    *end = base + size;
    return *end > base;
}

static int l1_l2_indexes(uint64_t address, uint32_t *l1_index, uint32_t *l2_index) {
    if (address >= VMM_MAP_LIMIT || l1_index == NULL || l2_index == NULL) return 0;
    *l1_index = (uint32_t)(address >> 30);
    *l2_index = (uint32_t)((address >> 21) & 0x1ffu);
    return *l1_index < 2u;
}

static uint64_t *l2_table_for(uint32_t l1_index) {
    if (l1_index == 0u) return l2_device;
    if (l1_index == 1u) return l2_ram;
    return NULL;
}

static uint64_t *ensure_l3_table(uint32_t l1_index, uint32_t l2_index) {
    uint64_t *l2_table;
    uint16_t slot;
    if (l1_index >= 2u || l2_index >= L2_ENTRIES) return NULL;
    l2_table = l2_table_for(l1_index);
    if (l2_table == NULL) return NULL;
    slot = l3_slots[l1_index][l2_index];
    if (slot != INVALID_L3_SLOT) return l3_pool[slot];
    if (l3_tables_used >= VMM_MAX_L3_TABLES) {
        record_last_error(VMM_ERR_TABLE_POOL_EXHAUSTED);
        return NULL;
    }
    slot = (uint16_t)l3_tables_used++;
    zero_bytes(l3_pool[slot], sizeof(l3_pool[slot]));
    /* Publish an empty, cleaned table before exposing its descriptor to the walker. */
    clean_range_to_poc(l3_pool[slot], sizeof(l3_pool[slot]));
    l2_table[l2_index] = vmm_make_table_descriptor((uint64_t)(uintptr_t)l3_pool[slot]);
    if (l2_table[l2_index] == 0u) {
        record_last_error(VMM_ERR_TABLE_VALIDATION);
        return NULL;
    }
    clean_range_to_poc(&l2_table[l2_index], sizeof(l2_table[l2_index]));
    l3_slots[l1_index][l2_index] = slot;
    return l3_pool[slot];
}

static int map_page(uint64_t physical_address, vmm_memory_type_t type,
                    vmm_page_permissions_t permissions) {
    uint32_t l1_index, l2_index, l3_index;
    uint64_t *l3_table;
    uint64_t descriptor;
    if ((physical_address & (VMM_PAGE_SIZE - 1u)) != 0u ||
        !l1_l2_indexes(physical_address, &l1_index, &l2_index)) return 0;
    l3_index = (uint32_t)((physical_address >> 12) & 0x1ffu);
    l3_table = ensure_l3_table(l1_index, l2_index);
    if (l3_table == NULL) return 0;
    descriptor = vmm_make_page_descriptor(physical_address, type, permissions);
    if (descriptor == 0u) {
        record_last_error(VMM_ERR_TABLE_VALIDATION);
        return 0;
    }
    if (l3_table[l3_index] != 0u) {
        if (l3_table[l3_index] != descriptor) {
            record_last_error(VMM_ERR_MAP_CONFLICT);
            return 0;
        }
        return 1; /* Duplicate FDT/platform descriptions must agree exactly. */
    }
    l3_table[l3_index] = descriptor;
    if (type == VMM_MEMORY_DEVICE) ++mapped_device_pages;
    else ++mapped_ram_pages;
    return 1;
}

static int map_device_range(uint64_t base, uint64_t size) {
    uint64_t end, page, rounded_end;
    if (!interval_end(base, size, &end) || base >= VMM_DEVICE_LIMIT || end > VMM_DEVICE_LIMIT ||
        end > UINT64_MAX - (VMM_PAGE_SIZE - 1u)) return 0;
    page = base & ~((uint64_t)VMM_PAGE_SIZE - 1u);
    rounded_end = (end + VMM_PAGE_SIZE - 1u) & ~((uint64_t)VMM_PAGE_SIZE - 1u);
    while (page < rounded_end) {
        if (!map_page(page, VMM_MEMORY_DEVICE, VMM_PERM_DEVICE)) return 0;
        page += VMM_PAGE_SIZE;
    }
    return 1;
}

static vmm_page_permissions_t ram_permissions(uint64_t address) {
    uint64_t text_start = (uint64_t)(uintptr_t)&__text_start;
    uint64_t text_end = (uint64_t)(uintptr_t)&__text_end;
    uint64_t ro_start = (uint64_t)(uintptr_t)&__rodata_start;
    uint64_t ro_end = (uint64_t)(uintptr_t)&__rodata_end;
    uint64_t data_start = (uint64_t)(uintptr_t)&__data_start;
    uint64_t data_end = (uint64_t)(uintptr_t)&__data_end;
    uint64_t bss_start = (uint64_t)(uintptr_t)&__bss_start;
    uint64_t bss_end = (uint64_t)(uintptr_t)&__bss_end;
    if (address >= text_start && address < text_end) return VMM_PERM_KERNEL_TEXT;
    if (address >= ro_start && address < ro_end) return VMM_PERM_KERNEL_RODATA;
    if ((address >= data_start && address < data_end) ||
        (address >= bss_start && address < bss_end)) return VMM_PERM_KERNEL_DATA;
    return VMM_PERM_KERNEL_DATA;
}

static int is_task_stack_guard_page(uint64_t page) {
    uint64_t start = (uint64_t)(uintptr_t)&__task_stacks_start;
    uint64_t end = (uint64_t)(uintptr_t)&__task_stacks_end;
    if (page < start || page >= end) return 0;
    return ((page - start) % CHOS_TASK_STACK_SLOT_SIZE) == 0u;
}

static int task_stack_layout_is_valid(void) {
    uint64_t start = (uint64_t)(uintptr_t)&__task_stacks_start;
    uint64_t end = (uint64_t)(uintptr_t)&__task_stacks_end;
    return (start & (VMM_PAGE_SIZE - 1u)) == 0u &&
           (end & (VMM_PAGE_SIZE - 1u)) == 0u &&
           end >= start && end - start == CHOS_TASK_STACK_REGION_SIZE;
}

static int map_ram_regions(const fdt_info_t *tree) {
    size_t i;
    uint64_t guard_page = (uint64_t)(uintptr_t)&__stack_guard_start;
    if (!task_stack_layout_is_valid()) {
        record_last_error(VMM_ERR_TABLE_VALIDATION);
        return 0;
    }
    for (i = 0u; i < tree->memory_count; ++i) {
        uint64_t raw_end, page, aligned_end;
        if (!interval_end(tree->memory[i].base, tree->memory[i].size, &raw_end)) return 0;
        page = (tree->memory[i].base + VMM_PAGE_SIZE - 1u) & ~((uint64_t)VMM_PAGE_SIZE - 1u);
        aligned_end = raw_end & ~((uint64_t)VMM_PAGE_SIZE - 1u);
        if(aligned_end>highest_ram_end) highest_ram_end=aligned_end;
        /* The candidate window can overlap identity RAM with >256 MiB. Mark
         * it occupied without granting ownership to unmap_page(). */
        uint64_t occupied=page<VMM_DYNAMIC_VA_BASE?VMM_DYNAMIC_VA_BASE:page;
        for(;occupied<aligned_end && occupied<VMM_DYNAMIC_VA_LIMIT;occupied+=VMM_PAGE_SIZE) {
            uint64_t index=(occupied-VMM_DYNAMIC_VA_BASE)/VMM_PAGE_SIZE;
            dynamic_candidate_bitmap[index/8]|=(uint8_t)(1u<<(index%8));
        }
        if (page >= aligned_end) continue;
        for (; page < aligned_end; page += VMM_PAGE_SIZE) {
            if (page == guard_page || is_task_stack_guard_page(page)) continue; /* Guard pages stay unmapped. */
            if (!map_page(page, VMM_MEMORY_NORMAL, ram_permissions(page))) return 0;
        }
    }
    return 1;
}

static int build_fine_grained_map(const fdt_info_t *tree) {
    size_t i;
    zero_bytes(l1_table, sizeof(l1_table));
    zero_bytes(l2_device, sizeof(l2_device));
    zero_bytes(l2_ram, sizeof(l2_ram));
    zero_bytes(l3_pool, sizeof(l3_pool));
    l3_tables_used = 0u;
    mapped_device_pages = 0u;
    mapped_ram_pages = 0u;
    dynamic_mappings = 0u;
    dynamic_search_hint = VMM_DYNAMIC_VA_BASE;
    zero_bytes(dynamic_map_bitmap, sizeof(dynamic_map_bitmap));
    zero_bytes(dynamic_candidate_bitmap,sizeof(dynamic_candidate_bitmap));
    highest_ram_end=0;
    for (i = 0u; i < 2u * L2_ENTRIES; ++i) ((uint16_t *)l3_slots)[i] = INVALID_L3_SLOT;

    l1_table[0] = vmm_make_table_descriptor((uint64_t)(uintptr_t)l2_device);
    l1_table[1] = vmm_make_table_descriptor((uint64_t)(uintptr_t)l2_ram);
    if (l1_table[0] == 0u || l1_table[1] == 0u) {
        record_last_error(VMM_ERR_TABLE_VALIDATION);
        return 0;
    }

    /* Ensure essential platform windows remain mapped even if FDT's device
       list is capped or aliases these windows under a different node name. */
    if (!map_device_range((uint64_t)platform_gic_distributor_base(), GIC_WINDOW_SIZE) ||
        !map_device_range((uint64_t)platform_gic_cpu_interface_base(), GIC_WINDOW_SIZE) ||
        !map_device_range((uint64_t)platform_uart_base(), UART_WINDOW_SIZE) ||
        !map_device_range((uint64_t)platform_rtc_base(), UART_WINDOW_SIZE) ||
        !map_device_range((uint64_t)platform_virtio_mmio_base(), 0x4000u)) return 0;

    /* Map only MMIO windows needed by the current QEMU platform drivers.
       Do not map every FDT `reg` range as Device memory: bridge apertures,
       PCI ECAM/resource windows, flash and buses can have different mapping
       semantics and can exceed this low-MMIO aperture. Additional devices
       should be mapped when their driver/platform mapping policy is added. */
    if (!map_ram_regions(tree)) return 0;
    return 1;
}

static uint64_t *walk_l3_entry_root(uint64_t root_address, uint64_t virtual_address) {
    uint32_t l1_index, l2_index, l3_index;
    uint64_t l1_desc, l2_desc;
    uint64_t *root, *l2_table, *l3_table;
    if ((root_address & (VMM_PAGE_SIZE - 1u)) != 0u || root_address == 0u ||
        root_address >= VMM_MAP_LIMIT ||
        !l1_l2_indexes(virtual_address, &l1_index, &l2_index)) return NULL;
    root = (uint64_t *)(uintptr_t)root_address;
    l3_index = (uint32_t)((virtual_address >> 12) & 0x1ffu);
    l1_desc = root[l1_index];
    if ((l1_desc & 3u) != 3u) return NULL;
    l2_table = (uint64_t *)(uintptr_t)(l1_desc & DESC_ADDR_MASK);
    l2_desc = l2_table[l2_index];
    if ((l2_desc & 3u) != 3u) return NULL;
    l3_table = (uint64_t *)(uintptr_t)(l2_desc & DESC_ADDR_MASK);
    return &l3_table[l3_index];
}

/* Kernel-owned dynamic mappings are edited only in the canonical kernel root. */
static uint64_t *walk_l3_entry(uint64_t virtual_address) {
    return walk_l3_entry_root((uint64_t)(uintptr_t)l1_table, virtual_address);
}

vmm_error_t vmm_prepare(const fdt_info_t *tree,
                        uint64_t kernel_start, uint64_t kernel_end,
                        uint64_t dtb_start, uint64_t dtb_size) {
    vmm_error_t result;
    prepared = 0;
    uefi_profile = 0;
    enabled_by_chos = 0;
    record_last_error(VMM_ERR_NOT_PREPARED);
    if (((uintptr_t)l1_table & (VMM_PAGE_SIZE - 1u)) != 0u ||
        ((uintptr_t)l2_device & (VMM_PAGE_SIZE - 1u)) != 0u ||
        ((uintptr_t)l2_ram & (VMM_PAGE_SIZE - 1u)) != 0u ||
        ((uintptr_t)l3_pool & (VMM_PAGE_SIZE - 1u)) != 0u) {
        record_last_error(VMM_ERR_TABLE_VALIDATION);
        return VMM_ERR_TABLE_VALIDATION;
    }
    result = vmm_validate_boot_layout(tree, kernel_start, kernel_end, dtb_start, dtb_size);
    if (result != VMM_OK) {
        record_last_error(result);
        return result;
    }
    if (!build_fine_grained_map(tree)) {
        if (last_error == VMM_ERR_NOT_PREPARED) record_last_error(VMM_ERR_TABLE_VALIDATION);
        return last_error;
    }
    /* Clean every table actually used before the page walker can read memory. */
    clean_range_to_poc(l1_table, sizeof(l1_table));
    clean_range_to_poc(l2_device, sizeof(l2_device));
    clean_range_to_poc(l2_ram, sizeof(l2_ram));
    clean_range_to_poc(l3_pool, (size_t)l3_tables_used * VMM_PAGE_SIZE);
    prepared = 1;
    record_last_error(VMM_OK);
    return VMM_OK;
}

vmm_error_t vmm_prepare_uefi(const chos_boot_info_t *b,const chos_acpi_info_t *a,
                              const chos_pci_device_t *d,size_t n,uint64_t virtio_mmio) {
    prepared=enabled_by_chos=uefi_profile=0;
    if(read_sctlr_el1()&(SCTLR_M|SCTLR_C))return VMM_ERR_MMU_ALREADY_ENABLED;
    uint64_t mmfr0;__asm__ volatile("mrs %0,id_aa64mmfr0_el1":"=r"(mmfr0));
    if((mmfr0&15u)<2u || ((mmfr0>>28)&15u)==15u)return VMM_ERR_UNSUPPORTED_RAM_LAYOUT;
    if(!vmm_uefi_build(b,a,d,n,virtio_mmio))return VMM_ERR_TABLE_VALIDATION;
    vmm_uefi_map_stats(&mapped_ram_pages,&mapped_device_pages,&l3_tables_used);
    dynamic_mappings=0;uefi_profile=prepared=1;record_last_error(VMM_OK);return VMM_OK;
}
int vmm_uefi_profile(void){return uefi_profile;}
int vmm_identity_dma_ready(void){
    return vmm_identity_dma_policy(read_sctlr_el1(),uefi_profile,vmm_is_enabled());
}
int vmm_copy_kernel_root_template(uint64_t *out,size_t count){
    if(!out||count<512u||!vmm_is_enabled()||dynamic_mappings)return 0;
    const uint64_t *source=(const uint64_t *)(uintptr_t)vmm_kernel_root_table_address();
    for(unsigned i=0;i<512u;++i)out[i]=source[i];return 1;
}
int vmm_copy_kernel_low_l1_template(uint64_t *out,size_t count){
    return uefi_profile && out && count>=512u && vmm_uefi_copy_l1(out);
}
int vmm_enable(void) {
    uint64_t sctlr;
    uintptr_t root = (uintptr_t)vmm_kernel_root_table_address();
    if (!prepared) {
        record_last_error(VMM_ERR_NOT_PREPARED);
        return 0;
    }
    sctlr = read_sctlr_el1();
    if ((sctlr & SCTLR_M) != 0u) {
        record_last_error(VMM_ERR_MMU_ALREADY_ENABLED);
        return 0;
    }
    __asm__ volatile("msr mair_el1, %0" :: "r"((uint64_t)MAIR_EL1_VALUE) : "memory");
    uint64_t tcr=uefi_profile ? (UINT64_C(16)|TCR_EPD1_DISABLE_TTBR1|TCR_IPS_40BIT|TCR_SH0_INNER) : TCR_EL1_VALUE;
    __asm__ volatile("msr tcr_el1, %0" :: "r"(tcr) : "memory");
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"((uint64_t)root) : "memory");
    __asm__ volatile(
        "dsb sy\n\t"
        "tlbi vmalle1\n\t"
        "dsb sy\n\t"
        "isb\n\t"
        "ic iallu\n\t"
        "dsb sy\n\t"
        "isb"
        ::: "memory");
    sctlr = read_sctlr_el1();
    sctlr |= uefi_profile ? (SCTLR_M|SCTLR_I|SCTLR_WXN) : SCTLR_ENABLE_MASK; /* WXN enforces no execute from writable pages. */
    __asm__ volatile("msr sctlr_el1, %0\n\tisb" :: "r"(sctlr) : "memory");
    if ((read_sctlr_el1() & SCTLR_M) == 0u) {
        record_last_error(VMM_ERR_TABLE_VALIDATION);
        return 0;
    }
    enabled_by_chos = 1;
    record_last_error(VMM_OK);
    return 1;
}

int vmm_is_enabled(void) {
    return enabled_by_chos && ((read_sctlr_el1() & SCTLR_M) != 0u);
}

int vmm_copy_kernel_l2_template(uint32_t l1_index, uint64_t *entries, size_t entry_count) {
    uint64_t daif;
    const uint64_t *source;
    size_t i;
    if (entries == NULL || entry_count < L2_ENTRIES || l1_index >= 2u) return 0;
    daif = save_and_mask_irqs();
    if (!prepared || !vmm_is_enabled()) {
        restore_daif(daif);
        return 0;
    }
    if(uefi_profile){int ok=vmm_uefi_copy_l2(l1_index,entries);restore_daif(daif);return ok;}
    if(dynamic_mappings){restore_daif(daif);return 0;}
    source = (l1_index == 0u) ? l2_device : l2_ram;
    for (i = 0u; i < L2_ENTRIES; ++i) entries[i] = source[i];
    restore_daif(daif);
    return 1;
}

uint64_t vmm_kernel_root_table_address(void) {
    return uefi_profile?vmm_uefi_root():(uint64_t)(uintptr_t)l1_table;
}

uint64_t vmm_current_root_table_address(void) {
    if (!vmm_is_enabled()) return vmm_kernel_root_table_address();
    return read_ttbr0_el1() & DESC_ADDR_MASK;
}

int vmm_switch_root_table(uint64_t root_table_address) {
    vmm_mapping_info_t backing;
    if(!vmm_is_enabled()||!root_table_address||(root_table_address&4095u)||
       root_table_address>=CHOS_UVMM_PHYS_LIMIT)return 0;
    uint64_t daif=save_and_mask_irqs();
    if(!vmm_query_root_mapping(vmm_kernel_root_table_address(),root_table_address,&backing)||
       backing.physical_address!=root_table_address || backing.memory_type!=VMM_MEMORY_NORMAL ||
       !backing.writable || backing.executable_at_el1 || backing.executable_at_el0 || backing.accessible_at_el0){
        restore_daif(daif);return 0;
    }
    const uint64_t *new_root=(const uint64_t *)(uintptr_t)root_table_address;
    if((new_root[0]&3u)!=3u || (new_root[1]&3u)!=3u){restore_daif(daif);return 0;}
    __asm__ volatile("dsb ishst\n\tmsr ttbr0_el1, %0\n\tisb\n\ttlbi vmalle1is\n\tdsb ish\n\tisb"
                     ::"r"(root_table_address):"memory");
    int ok=(read_ttbr0_el1()&DESC_ADDR_MASK)==root_table_address;restore_daif(daif);return ok;
}

void vmm_clean_page_table_range(const void *memory, size_t length) {
    clean_range_to_poc(memory, length);
}

/* Walk only trusted CHOS roots. EL0 never supplies a root-table address. */
int vmm_query_root_mapping(uint64_t root,uint64_t va,vmm_mapping_info_t *out){
    if(!out || !root || (root&4095u) || root>=CHOS_UVMM_PHYS_LIMIT ||
       va>=(uefi_profile?CHOS_UVMM_PHYS_LIMIT:VMM_MAP_LIMIT))return 0;
    const uint64_t *t=(const uint64_t *)(uintptr_t)root;
    if(uefi_profile){uint64_t d=t[(va>>39)&511u];if((d&3u)!=3u)return 0;t=(const uint64_t *)(uintptr_t)(d&DESC_ADDR_MASK);}
    uint64_t d=t[(va>>30)&511u];if((d&3u)!=3u)return 0;
    t=(const uint64_t *)(uintptr_t)(d&DESC_ADDR_MASK);d=t[(va>>21)&511u];uint64_t pa;
    if((d&3u)==1u)pa=(d&UINT64_C(0x0000ffffffe00000))|(va&UINT64_C(0x1fffff));
    else if((d&3u)==3u){t=(const uint64_t *)(uintptr_t)(d&DESC_ADDR_MASK);d=t[(va>>12)&511u];
        if((d&3u)!=3u)return 0;pa=(d&DESC_ADDR_MASK)|(va&4095u);
    }else return 0;
    uint64_t ap=(d>>6)&3u;out->physical_address=pa;
    out->memory_type=((d>>2)&7u)==1?VMM_MEMORY_DEVICE:VMM_MEMORY_NORMAL;
    out->writable=ap==0||ap==1;out->accessible_at_el0=ap==1||ap==3;
    out->executable_at_el1=!(d&DESC_PXN);out->executable_at_el0=!(d&DESC_UXN);return 1;
}
int vmm_query_mapping(uint64_t va,vmm_mapping_info_t *out){
    uint64_t daif=save_and_mask_irqs();
    int ok=vmm_query_root_mapping(vmm_current_root_table_address(),va,out);restore_daif(daif);return ok;
}

/* Invalidate a single 4 KiB VA for EL1 across the inner-shareable domain. */
void vmm_tlb_invalidate_page(uint64_t virtual_address) {
    uint64_t operand;
    if ((virtual_address & (VMM_PAGE_SIZE - 1u)) != 0u) return;
    operand = virtual_address >> 12;
    __asm__ volatile("dsb ishst\n\ttlbi vae1is, %0\n\tdsb ish\n\tisb"
                     :: "r"(operand) : "memory");
}

int vmm_find_free_dynamic_va(uint64_t *virtual_address) {
    uint64_t daif, candidate;
    if (uefi_profile || virtual_address == NULL || !vmm_is_enabled()) return 0;
    daif = save_and_mask_irqs();
    if (vmm_current_root_table_address() != vmm_kernel_root_table_address()) {
        record_last_error(VMM_ERR_KERNEL_ROOT_NOT_ACTIVE);
        restore_daif(daif);
        return 0;
    }
    if (!vmm_find_dynamic_candidate(dynamic_candidate_bitmap, sizeof(dynamic_candidate_bitmap),
                                    dynamic_search_hint, &candidate)) {
        restore_daif(daif);
        return 0;
    }
    /* This API is advisory, not a reservation. The caller must handle a map conflict
       if it is preempted after return. vmm_map_page_auto() provides atomic find+map. */
    dynamic_search_hint = next_dynamic_hint(candidate);
    *virtual_address = candidate;
    restore_daif(daif);
    return 1;
}

vmm_error_t vmm_map_page(uint64_t virtual_address, uint64_t physical_address,
                         vmm_memory_type_t memory_type,
                         vmm_page_permissions_t permissions) {
    vmm_error_t error;
    vmm_mapping_info_t backing, occupied;
    if(uefi_profile)return VMM_ERR_UNSUPPORTED_RAM_LAYOUT;
    uint32_t l1_index, l2_index, l3_index;
    uint64_t *l3_table, *entry, descriptor, daif;

    if (!vmm_is_enabled()) {
        record_last_error(VMM_ERR_MMU_NOT_ENABLED);
        return VMM_ERR_MMU_NOT_ENABLED;
    }
    /* Keep root selection stable from validation through PTE publication. Root switches
       occur only with local IRQs masked, so this is the single-core serialization point. */
    daif = save_and_mask_irqs();
    if (vmm_current_root_table_address() != vmm_kernel_root_table_address()) {
        record_last_error(VMM_ERR_KERNEL_ROOT_NOT_ACTIVE);
        restore_daif(daif);
        return VMM_ERR_KERNEL_ROOT_NOT_ACTIVE;
    }
    error = vmm_validate_dynamic_page_mapping(virtual_address, physical_address,
                                              memory_type, permissions);
    if (error != VMM_OK) {
        record_last_error(error);
        restore_daif(daif);
        return error;
    }
    /* Only alias a page that already has a valid identity mapping. This initial
       API deliberately cannot map arbitrary untracked physical addresses. */
    if (!vmm_query_mapping(physical_address, &backing) ||
        backing.physical_address != physical_address || backing.memory_type != memory_type ||
        !backing.writable || backing.executable_at_el1) {
        record_last_error(VMM_ERR_PHYSICAL_ADDRESS_NOT_MAPPED);
        restore_daif(daif);
        return VMM_ERR_PHYSICAL_ADDRESS_NOT_MAPPED;
    }
    if (dynamic_bitmap_get(virtual_address) || vmm_query_mapping(virtual_address, &occupied)) {
        record_last_error(VMM_ERR_MAP_CONFLICT);
        restore_daif(daif);
        return VMM_ERR_MAP_CONFLICT;
    }
    if (!l1_l2_indexes(virtual_address, &l1_index, &l2_index)) {
        record_last_error(VMM_ERR_VIRTUAL_ADDRESS_OUT_OF_RANGE);
        restore_daif(daif);
        return VMM_ERR_VIRTUAL_ADDRESS_OUT_OF_RANGE;
    }
    l3_index = (uint32_t)((virtual_address >> 12) & 0x1ffu);
    l3_table = ensure_l3_table(l1_index, l2_index);
    if (l3_table == NULL) {
        error = last_error == VMM_OK ? VMM_ERR_TABLE_POOL_EXHAUSTED : last_error;
        record_last_error(error);
        restore_daif(daif);
        return error;
    }
    entry = &l3_table[l3_index];
    if ((*entry & DESC_VALID) != 0u) {
        record_last_error(VMM_ERR_MAP_CONFLICT);
        restore_daif(daif);
        return VMM_ERR_MAP_CONFLICT;
    }
    descriptor = vmm_make_page_descriptor(physical_address, memory_type, permissions);
    if (descriptor == 0u) {
        record_last_error(VMM_ERR_BAD_MAPPING_ARGUMENT);
        restore_daif(daif);
        return VMM_ERR_BAD_MAPPING_ARGUMENT;
    }
    *entry = descriptor;
    clean_range_to_poc(entry, sizeof(*entry));
    vmm_tlb_invalidate_page(virtual_address);
    dynamic_bitmap_set(virtual_address, 1);
    dynamic_search_hint = next_dynamic_hint(virtual_address);
    ++dynamic_mappings;
    record_last_error(VMM_OK);
    restore_daif(daif);
    return VMM_OK;
}

vmm_error_t vmm_map_page_auto(uint64_t physical_address,
                              vmm_memory_type_t memory_type,
                              vmm_page_permissions_t permissions,
                              uint64_t *virtual_address) {
    uint64_t daif, candidate;
    vmm_error_t error;
    if (virtual_address == NULL) {
        record_last_error(VMM_ERR_NULL_ARGUMENT);
        return VMM_ERR_NULL_ARGUMENT;
    }
    daif = save_and_mask_irqs();
    if (!vmm_is_enabled()) {
        record_last_error(VMM_ERR_MMU_NOT_ENABLED);
        restore_daif(daif);
        return VMM_ERR_MMU_NOT_ENABLED;
    }
    if (vmm_current_root_table_address() != vmm_kernel_root_table_address()) {
        record_last_error(VMM_ERR_KERNEL_ROOT_NOT_ACTIVE);
        restore_daif(daif);
        return VMM_ERR_KERNEL_ROOT_NOT_ACTIVE;
    }
    if (!vmm_find_dynamic_candidate(dynamic_candidate_bitmap, sizeof(dynamic_candidate_bitmap),
                                    dynamic_search_hint, &candidate)) {
        record_last_error(VMM_ERR_NO_FREE_DYNAMIC_VA);
        restore_daif(daif);
        return VMM_ERR_NO_FREE_DYNAMIC_VA;
    }
    /* vmm_map_page() preserves the already-masked DAIF state on return, so no task
       can claim this candidate between the bitmap scan and the PTE install. */
    error = vmm_map_page(candidate, physical_address, memory_type, permissions);
    if (error == VMM_OK) {
        dynamic_search_hint = next_dynamic_hint(candidate);
        *virtual_address = candidate;
    }
    restore_daif(daif);
    return error;
}

vmm_error_t vmm_unmap_page(uint64_t virtual_address) {
    uint32_t l1_index, l2_index;
    uint64_t *entry, daif;
    if (!vmm_is_enabled()) {
        record_last_error(VMM_ERR_MMU_NOT_ENABLED);
        return VMM_ERR_MMU_NOT_ENABLED;
    }
    if (virtual_address < VMM_DYNAMIC_VA_BASE || virtual_address >= VMM_DYNAMIC_VA_LIMIT) {
        record_last_error(VMM_ERR_VIRTUAL_ADDRESS_OUT_OF_RANGE);
        return VMM_ERR_VIRTUAL_ADDRESS_OUT_OF_RANGE;
    }
    if ((virtual_address & (VMM_PAGE_SIZE - 1u)) != 0u) {
        record_last_error(VMM_ERR_BAD_MAPPING_ARGUMENT);
        return VMM_ERR_BAD_MAPPING_ARGUMENT;
    }

    daif = save_and_mask_irqs();
    if (vmm_current_root_table_address() != vmm_kernel_root_table_address()) {
        record_last_error(VMM_ERR_KERNEL_ROOT_NOT_ACTIVE);
        restore_daif(daif);
        return VMM_ERR_KERNEL_ROOT_NOT_ACTIVE;
    }
    if (!dynamic_bitmap_get(virtual_address)) {
        restore_daif(daif);
        record_last_error(VMM_ERR_VIRTUAL_ADDRESS_NOT_DYNAMIC);
        return VMM_ERR_VIRTUAL_ADDRESS_NOT_DYNAMIC;
    }
    if (!l1_l2_indexes(virtual_address, &l1_index, &l2_index)) {
        restore_daif(daif);
        record_last_error(VMM_ERR_VIRTUAL_ADDRESS_OUT_OF_RANGE);
        return VMM_ERR_VIRTUAL_ADDRESS_OUT_OF_RANGE;
    }
    entry = walk_l3_entry(virtual_address);
    if (entry == NULL || ((*entry & 3u) != 3u)) {
        restore_daif(daif);
        record_last_error(VMM_ERR_TABLE_VALIDATION);
        return VMM_ERR_TABLE_VALIDATION;
    }
    *entry = 0u;
    clean_range_to_poc(entry, sizeof(*entry));
    vmm_tlb_invalidate_page(virtual_address);
    dynamic_bitmap_set(virtual_address, 0);
    if (virtual_address < dynamic_search_hint) dynamic_search_hint = virtual_address;
    if (dynamic_mappings > 0u) --dynamic_mappings;
    record_last_error(VMM_OK);
    restore_daif(daif);
    return VMM_OK;
}

static int vmm_test_dynamic_alias(void) {
    uint64_t physical_address = 0u, virtual_address = 0u, original_value;
    const uint64_t test_value = 0x43484f53414c4941ull;
    volatile uint64_t *identity;
    volatile uint64_t *alias;
    vmm_mapping_info_t map;
    int mapped = 0;
    int ok = 0;

    if (!pmm_alloc_page(&physical_address)) return 0;
    identity = (volatile uint64_t *)(uintptr_t)physical_address;
    original_value = *identity;
    if (vmm_map_page_auto(physical_address, VMM_MEMORY_NORMAL,
                          VMM_PERM_KERNEL_DATA, &virtual_address) != VMM_OK) goto cleanup;
    mapped = 1;
    alias = (volatile uint64_t *)(uintptr_t)virtual_address;
    if (!vmm_query_mapping(virtual_address, &map) ||
        map.physical_address != physical_address || !map.writable ||
        map.executable_at_el1 || map.accessible_at_el0) goto cleanup;
    *alias = test_value;
    if (*identity != test_value) goto cleanup;
    *identity = original_value;
    if (*alias != original_value) goto cleanup;
    if (vmm_unmap_page(virtual_address) != VMM_OK) goto cleanup;
    mapped = 0;
    if (vmm_query_mapping(virtual_address, &map)) goto cleanup;
    if (vmm_unmap_page(virtual_address) != VMM_ERR_VIRTUAL_ADDRESS_NOT_DYNAMIC) goto cleanup;
    ok = 1;

cleanup:
    if (mapped) (void)vmm_unmap_page(virtual_address);
    *identity = original_value;
    if (!pmm_free_page(physical_address)) ok = 0;
    if (ok) record_last_error(VMM_OK);
    return ok;
}

int vmm_self_test(void) {
    vmm_mapping_info_t map;
    uint64_t pa, old_value;
    const uint64_t test_value = 0x43484f53564d4d31ull;
    uint64_t text_address = (uint64_t)(uintptr_t)&__text_start;
    uint64_t ro_address = (uint64_t)(uintptr_t)&__rodata_start;
    uint64_t data_address = (uint64_t)(uintptr_t)&vmm_probe_word;
    uint64_t guard_address = (uint64_t)(uintptr_t)&__stack_guard_start;
    size_t stack_index;
    if (!prepared || !vmm_is_enabled() ||
        (read_ttbr0_el1() & DESC_ADDR_MASK) != (uint64_t)(uintptr_t)l1_table) return 0;
    if (!vmm_query_mapping((uint64_t)platform_uart_base(), &map) ||
        map.memory_type != VMM_MEMORY_DEVICE || !map.writable || map.executable_at_el1 ||
        !vmm_query_mapping((uint64_t)platform_rtc_base(), &map) ||
        map.memory_type != VMM_MEMORY_DEVICE || !map.writable || map.executable_at_el1 ||
        !vmm_query_mapping((uint64_t)platform_gic_distributor_base(), &map) ||
        map.memory_type != VMM_MEMORY_DEVICE || map.writable == 0 || map.executable_at_el1 ||
        !vmm_query_mapping((uint64_t)platform_gic_cpu_interface_base(), &map) ||
        map.memory_type != VMM_MEMORY_DEVICE || map.writable == 0 || map.executable_at_el1) return 0;
    if (!vmm_query_mapping(text_address, &map) || map.memory_type != VMM_MEMORY_NORMAL ||
        map.writable || !map.executable_at_el1 || map.accessible_at_el0) return 0;
    if (!vmm_query_mapping(ro_address, &map) || map.writable || map.executable_at_el1) return 0;
    if (!vmm_query_mapping(data_address, &map) || !map.writable || map.executable_at_el1) return 0;
    if (vmm_query_mapping(guard_address, &map)) return 0;
    if (!task_stack_layout_is_valid()) return 0;
    for (stack_index = 0u; stack_index < CHOS_SCHED_MAX_TASKS; ++stack_index) {
        uint64_t task_guard = (uint64_t)(uintptr_t)&__task_stacks_start +
                              ((uint64_t)stack_index * CHOS_TASK_STACK_SLOT_SIZE);
        if (vmm_query_mapping(task_guard, &map)) return 0;
        if (!vmm_query_mapping(task_guard + CHOS_TASK_STACK_GUARD_SIZE, &map) ||
            map.memory_type != VMM_MEMORY_NORMAL || !map.writable ||
            map.executable_at_el1 || map.accessible_at_el0) return 0;
    }
    if (vmm_query_mapping(0x0b000000ull, &map)) return 0; /* unmapped MMIO hole */
    if (vmm_query_mapping(highest_ram_end, &map)) return 0; /* actual advertised RAM end */
    if (!vmm_virt_to_phys((const void *)(uintptr_t)&vmm_probe_word, &pa) ||
        pa != (uint64_t)(uintptr_t)&vmm_probe_word ||
        vmm_phys_to_virt(pa) != (void *)(uintptr_t)&vmm_probe_word) return 0;
    old_value = vmm_probe_word;
    vmm_probe_word = test_value;
    if (vmm_probe_word != test_value) {
        vmm_probe_word = old_value;
        return 0;
    }
    vmm_probe_word = old_value;
    return vmm_test_dynamic_alias();
}

void *vmm_phys_to_virt(uint64_t physical_address) {
    vmm_mapping_info_t info;
    if (!vmm_is_enabled() || !vmm_query_mapping(physical_address, &info) ||
        info.physical_address != physical_address) return NULL;
    return (void *)(uintptr_t)physical_address;
}

int vmm_virt_to_phys(const void *virtual_address, uint64_t *physical_address) {
    vmm_mapping_info_t info;
    uint64_t address = (uint64_t)(uintptr_t)virtual_address;
    if (!vmm_is_enabled() || physical_address == NULL ||
        !vmm_query_mapping(address, &info)) return 0;
    *physical_address = info.physical_address;
    return 1;
}

void vmm_get_stats(vmm_stats_t *out) {
    uint64_t daif;
    if (out == NULL) return;
    daif = save_and_mask_irqs();
    out->prepared = prepared;
    out->enabled = vmm_is_enabled();
    out->root_table_address = vmm_kernel_root_table_address();
    out->device_map_bytes = (uint64_t)mapped_device_pages * VMM_PAGE_SIZE;
    out->normal_map_bytes = (uint64_t)mapped_ram_pages * VMM_PAGE_SIZE;
    out->guard_page_address = (uint64_t)(uintptr_t)&__stack_guard_start;
    out->task_stack_guard_pages = prepared ? CHOS_SCHED_MAX_TASKS : 0u;
    out->granule_bytes = VMM_PAGE_SIZE;
    out->va_bits = uefi_profile?48u:39u;
    out->l1_table_count = 2u;
    out->l3_tables_used = l3_tables_used;
    out->mapped_device_pages = mapped_device_pages;
    out->mapped_ram_pages = mapped_ram_pages;
    out->dynamic_mappings = dynamic_mappings;
    restore_daif(daif);
}

vmm_error_t vmm_last_error(void) {
    uint64_t daif = save_and_mask_irqs();
    vmm_error_t error = last_error;
    restore_daif(daif);
    return error;
}

const char *vmm_error_string(vmm_error_t error) {
    switch (error) {
        case VMM_OK: return "success";
        case VMM_ERR_NOT_PREPARED: return "translation tables not prepared";
        case VMM_ERR_NULL_ARGUMENT: return "null argument";
        case VMM_ERR_BAD_DEVICE_TREE: return "invalid or unsupported device tree";
        case VMM_ERR_UNSUPPORTED_RAM_LAYOUT: return "RAM range invalid, overlapping, or outside QEMU virt map";
        case VMM_ERR_UNSUPPORTED_DEVICE_LAYOUT: return "device register range outside supported QEMU virt aperture";
        case VMM_ERR_BAD_KERNEL_RANGE: return "kernel image outside discovered RAM or supported map";
        case VMM_ERR_BAD_DTB_RANGE: return "device tree blob outside discovered RAM or supported map";
        case VMM_ERR_TABLE_VALIDATION: return "translation table alignment or descriptor validation failed";
        case VMM_ERR_MMU_ALREADY_ENABLED: return "MMU already enabled with unknown translation tables";
        case VMM_ERR_DEVICE_LIST_TRUNCATED: return "FDT device list truncated; cannot guarantee required mapping coverage";
        case VMM_ERR_TABLE_POOL_EXHAUSTED: return "static L3 page-table pool exhausted";
        case VMM_ERR_MAP_CONFLICT: return "virtual address is already mapped or attributes conflict";
        case VMM_ERR_MMU_NOT_ENABLED: return "MMU is not enabled by CHOS";
        case VMM_ERR_BAD_MAPPING_ARGUMENT: return "invalid alignment, memory type, or dynamic mapping permissions";
        case VMM_ERR_VIRTUAL_ADDRESS_OUT_OF_RANGE: return "virtual address is outside the dynamic mapping window";
        case VMM_ERR_VIRTUAL_ADDRESS_NOT_DYNAMIC: return "page was not created by the dynamic mapping API";
        case VMM_ERR_PHYSICAL_ADDRESS_NOT_MAPPED: return "physical page has no compatible writable identity mapping";
        case VMM_ERR_NO_FREE_DYNAMIC_VA: return "no free virtual address remains in the dynamic mapping window";
        case VMM_ERR_KERNEL_ROOT_NOT_ACTIVE: return "kernel root must be active for global dynamic mappings";
        default: return "unknown virtual memory error";
    }
}

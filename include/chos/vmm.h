#ifndef CHOS_VMM_H
#define CHOS_VMM_H

#include <stddef.h>
#include <stdint.h>
#include "chos/fdt.h"
#include "chos/pci.h"

#define VMM_PAGE_SIZE 4096u
#define VMM_L1_ENTRIES 512u
#define VMM_L2_ENTRIES 512u
#define VMM_L3_ENTRIES 512u
#define VMM_DEVICE_LIMIT 0x40000000ull /* QEMU virt low MMIO aperture */
#define VMM_RAM_BASE     0x40000000ull
#define VMM_MAP_LIMIT    0x80000000ull /* Supported identity-map ceiling */
#define VMM_DYNAMIC_VA_BASE  0x50000000ull /* First candidate after 256 MiB QEMU RAM */
#define VMM_DYNAMIC_VA_LIMIT VMM_MAP_LIMIT
#define VMM_DYNAMIC_PAGE_COUNT ((VMM_DYNAMIC_VA_LIMIT - VMM_DYNAMIC_VA_BASE) / VMM_PAGE_SIZE)
#define VMM_DYNAMIC_BITMAP_BYTES ((VMM_DYNAMIC_PAGE_COUNT + 7u) / 8u)
#define VMM_BLOCK_SIZE   0x40000000ull
/* Covers 1 GiB of RAM (512 L3 tables) plus up to 192 device 2 MiB chunks. */
#define VMM_MAX_L3_TABLES 704u

typedef enum {
    VMM_MEMORY_DEVICE = 0,
    VMM_MEMORY_NORMAL = 1
} vmm_memory_type_t;

typedef enum {
    VMM_PERM_DEVICE = 0,       /* EL1 RW, execute-never */
    VMM_PERM_KERNEL_TEXT,      /* EL1 RO, executable */
    VMM_PERM_KERNEL_RODATA,    /* EL1 RO, execute-never */
    VMM_PERM_KERNEL_DATA       /* EL1 RW, execute-never */
} vmm_page_permissions_t;

typedef enum {
    VMM_OK = 0,
    VMM_ERR_NOT_PREPARED,
    VMM_ERR_NULL_ARGUMENT,
    VMM_ERR_BAD_DEVICE_TREE,
    VMM_ERR_UNSUPPORTED_RAM_LAYOUT,
    VMM_ERR_UNSUPPORTED_DEVICE_LAYOUT,
    VMM_ERR_BAD_KERNEL_RANGE,
    VMM_ERR_BAD_DTB_RANGE,
    VMM_ERR_TABLE_VALIDATION,
    VMM_ERR_MMU_ALREADY_ENABLED,
    VMM_ERR_DEVICE_LIST_TRUNCATED,
    VMM_ERR_TABLE_POOL_EXHAUSTED,
    VMM_ERR_MAP_CONFLICT,
    VMM_ERR_MMU_NOT_ENABLED,
    VMM_ERR_BAD_MAPPING_ARGUMENT,
    VMM_ERR_VIRTUAL_ADDRESS_OUT_OF_RANGE,
    VMM_ERR_VIRTUAL_ADDRESS_NOT_DYNAMIC,
    VMM_ERR_PHYSICAL_ADDRESS_NOT_MAPPED,
    VMM_ERR_NO_FREE_DYNAMIC_VA,
    VMM_ERR_KERNEL_ROOT_NOT_ACTIVE
} vmm_error_t;

typedef struct {
    uint64_t physical_address;
    vmm_memory_type_t memory_type;
    int writable;
    int executable_at_el1;
    int accessible_at_el0;
    int executable_at_el0;
} vmm_mapping_info_t;

typedef struct {
    int prepared;
    int enabled;
    uint64_t root_table_address;
    uint64_t device_map_bytes;
    uint64_t normal_map_bytes;
    uint64_t guard_page_address;
    uint32_t granule_bytes;
    uint32_t va_bits;
    uint32_t l1_table_count;
    uint32_t l3_tables_used;
    uint32_t mapped_device_pages;
    uint32_t mapped_ram_pages;
    uint32_t dynamic_mappings;
    uint32_t task_stack_guard_pages;
} vmm_stats_t;

/* Pure descriptor constructors/validators are also exercised by host tests. */
uint64_t vmm_make_table_descriptor(uint64_t table_address);
uint64_t vmm_make_user_page_descriptor(uint64_t physical_address, int writable);
int vmm_validate_user_page_descriptor(uint64_t descriptor, uint64_t physical_address, int writable);
/* EL0 read-only executable leaf; privileged execution remains prohibited. */
uint64_t vmm_make_user_code_page_descriptor(uint64_t physical_address);
int vmm_validate_user_code_page_descriptor(uint64_t descriptor, uint64_t physical_address);
uint64_t vmm_make_page_descriptor(uint64_t physical_address,
                                  vmm_memory_type_t memory_type,
                                  vmm_page_permissions_t permissions);
int vmm_validate_page_descriptor(uint64_t descriptor, uint64_t physical_address,
                                 vmm_memory_type_t memory_type,
                                 vmm_page_permissions_t permissions);

vmm_error_t vmm_validate_boot_layout(const fdt_info_t *tree,
                                     uint64_t kernel_start, uint64_t kernel_end,
                                     uint64_t dtb_start, uint64_t dtb_size);
vmm_error_t vmm_prepare(const fdt_info_t *tree,
                        uint64_t kernel_start, uint64_t kernel_end,
                        uint64_t dtb_start, uint64_t dtb_size);
vmm_error_t vmm_prepare_uefi(const chos_boot_info_t *,const chos_acpi_info_t *,
                              const chos_pci_device_t *,size_t,uint64_t virtio_mmio);
/* UEFI uses four-level identity mappings with data caching disabled for DMA. */
int vmm_uefi_profile(void);
int vmm_identity_dma_ready(void);
int vmm_identity_dma_policy(uint64_t sctlr,int owned_uefi_profile,int enabled_by_kernel);
int vmm_copy_kernel_root_template(uint64_t *entries,size_t count);
int vmm_copy_kernel_low_l1_template(uint64_t *entries,size_t count);
int vmm_query_root_mapping(uint64_t root,uint64_t va,vmm_mapping_info_t *);
int vmm_enable(void);
int vmm_is_enabled(void);
/* Copy the active kernel root's L2 template (0=device/low VA, 1=RAM) into a page-aligned buffer. */
int vmm_copy_kernel_l2_template(uint32_t l1_index, uint64_t *entries, size_t entry_count);
uint64_t vmm_kernel_root_table_address(void);
/* Current TTBR0_EL1 root and low-level safe root switching. Use the address-space
 * activate/deactivate APIs to keep active-handle tracking synchronized. This prototype
 * uses ASID 0 and invalidates all EL1 stage-1 TLB entries after every root change. */
uint64_t vmm_current_root_table_address(void);
int vmm_switch_root_table(uint64_t root_table_address);
void vmm_clean_page_table_range(const void *memory, size_t length);
int vmm_self_test(void);
int vmm_query_mapping(uint64_t virtual_address, vmm_mapping_info_t *out);
/* Dynamic aliases are limited to the currently unmapped [0x50000000, 0x80000000) VA window.
 * Normal aliases must remain RW+XN to avoid creating a writable/executable physical alias.
 * VMM operations are serialized against timer preemption on the current single CPU. These
 * APIs are NOT a complete SMP locking implementation. */
vmm_error_t vmm_validate_dynamic_page_mapping(uint64_t virtual_address,
                                               uint64_t physical_address,
                                               vmm_memory_type_t memory_type,
                                               vmm_page_permissions_t permissions);
vmm_error_t vmm_map_page(uint64_t virtual_address, uint64_t physical_address,
                         vmm_memory_type_t memory_type,
                         vmm_page_permissions_t permissions);
vmm_error_t vmm_unmap_page(uint64_t virtual_address);
/* Atomic find+map convenience path; unlike find_free_dynamic_va(), this reserves the chosen
 * VA and installs the mapping in one IRQ-masked operation. Physical pages remain caller-owned. */
vmm_error_t vmm_map_page_auto(uint64_t physical_address,
                              vmm_memory_type_t memory_type,
                              vmm_page_permissions_t permissions,
                              uint64_t *virtual_address);
/* Advisory only: the returned VA is not reserved until a map API succeeds. */
int vmm_find_free_dynamic_va(uint64_t *virtual_address);
/* Pure bitmap policy helper, host-tested separately from hardware page tables. */
int vmm_find_dynamic_candidate(const uint8_t *bitmap, size_t bitmap_bytes,
                               uint64_t start_hint, uint64_t *candidate);
void vmm_tlb_invalidate_page(uint64_t virtual_address);
void vmm_get_stats(vmm_stats_t *out);
vmm_error_t vmm_last_error(void);
void *vmm_phys_to_virt(uint64_t physical_address);
int vmm_virt_to_phys(const void *virtual_address, uint64_t *physical_address);
const char *vmm_error_string(vmm_error_t error);

#endif

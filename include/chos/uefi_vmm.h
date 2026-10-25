#ifndef CHOS_UEFI_VMM_H
#define CHOS_UEFI_VMM_H
#include "chos/vmm.h"
/* Four levels cover the current 40-bit physical profile, including PCI BARs
 * above 512 GiB. Fixed pools bound early boot work and memory consumption. */
#define CHOS_UVMM_L2_TABLES 64u
#define CHOS_UVMM_L3_TABLES 192u
#define CHOS_UVMM_PHYS_LIMIT (UINT64_C(1) << 40)
typedef struct {
    uint64_t root[512];
    uint64_t l1[2][512];
    uint64_t l2[CHOS_UVMM_L2_TABLES][512];
    uint64_t l3[CHOS_UVMM_L3_TABLES][512];
    uint64_t physical_base;
    uint32_t l2_used,l3_used;
} chos_uefi_vmm_tables_t;
int chos_uefi_vmm_init(chos_uefi_vmm_tables_t *,uint64_t physical_base);
/* Map identity ranges; an existing mapping must agree in type/permissions.
 * Endpoints are 4 KiB aligned. Generic ranges use 2 MiB blocks when possible. */
int chos_uefi_vmm_map(chos_uefi_vmm_tables_t *,uint64_t base,uint64_t size,
                     vmm_memory_type_t,vmm_page_permissions_t);
/* Refine an already mapped Normal RAM page for kernel W^X or remove a guard.
 * Never changes memory type or creates a new page. */
int chos_uefi_vmm_protect(chos_uefi_vmm_tables_t *,uint64_t page,
                         vmm_page_permissions_t,int present);
int chos_uefi_vmm_query(const chos_uefi_vmm_tables_t *,uint64_t va,vmm_mapping_info_t *);
uint64_t chos_uefi_vmm_l1_address(const chos_uefi_vmm_tables_t *,unsigned index);
uint64_t chos_uefi_vmm_l2_address(const chos_uefi_vmm_tables_t *,unsigned index);
#endif

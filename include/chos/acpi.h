#ifndef CHOS_ACPI_H
#define CHOS_ACPI_H

#include <stddef.h>
#include <stdint.h>
#include "chos/boot_info.h"

#define CHOS_ACPI_MAX_PCI_SEGMENTS 8u

typedef enum {
    CHOS_ACPI_OK = 0,
    CHOS_ACPI_ERR_ARGUMENT,
    CHOS_ACPI_ERR_NO_RSDP,
    CHOS_ACPI_ERR_BAD_RSDP,
    CHOS_ACPI_ERR_BAD_ROOT,
    CHOS_ACPI_ERR_BAD_MADT,
    CHOS_ACPI_ERR_BAD_GTDT,
    CHOS_ACPI_ERR_BAD_MCFG
} chos_acpi_error_t;

typedef struct {
    uint64_t ecam_base;
    uint16_t segment_group;
    uint8_t start_bus;
    uint8_t end_bus;
} chos_acpi_pci_segment_t;

typedef struct {
    uint64_t pl011_base; /* checksum-validated optional SPCR SystemMemory GAS */
    uint32_t dma_translation_unverified; /* SMMU/unknown IORT nodes: no native IOMMU ownership yet */
    uint32_t valid;
    uint8_t rsdp_revision;
    uint8_t root_is_xsdt;
    uint16_t reserved;
    uint64_t rsdp_address;
    uint64_t root_table_address;
    uint64_t madt_address;
    uint64_t gtdt_address;
    uint64_t mcfg_address;
    uint64_t gic_distributor_base;
    uint64_t gic_redistributor_base;
    uint32_t gic_redistributor_length;
    uint32_t gic_version;
    uint32_t gic_cpu_interface_count;
    uint32_t physical_timer_irq;
    uint32_t physical_timer_flags;
    uint32_t virtual_timer_irq;
    uint32_t virtual_timer_flags;
    uint32_t pci_segment_count;
    chos_acpi_pci_segment_t pci_segments[CHOS_ACPI_MAX_PCI_SEGMENTS];
} chos_acpi_info_t;

/* Parse the ACPI root, MADT, GTDT, and MCFG. Every table must be checksum-valid
 * and fully contained in a descriptor from the preserved EFI map. */
chos_acpi_error_t chos_acpi_parse(const chos_boot_info_t *boot, chos_acpi_info_t *out);
const char *chos_acpi_error_string(chos_acpi_error_t error);

#endif

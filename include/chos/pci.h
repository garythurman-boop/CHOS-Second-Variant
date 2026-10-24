#ifndef CHOS_PCI_H
#define CHOS_PCI_H

#include <stddef.h>
#include <stdint.h>
#include "chos/acpi.h"
#include "chos/boot_info.h"

#define CHOS_PCI_MAX_DEVICES 64u
#define CHOS_PCI_MAX_BUSES 256u

typedef struct {
    uint16_t segment;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint8_t header_type;
    uint8_t programming_interface;
    uint8_t subclass;
    uint8_t class_code;
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t command;
    uint16_t status;
    /* Read-only snapshot of BAR dwords; a 64-bit BAR consumes two entries. */
    uint32_t bars[6];
} chos_pci_device_t;

typedef struct {
    uint32_t segments_considered;
    uint32_t buses_scanned;
    uint32_t buses_skipped_unmapped;
    uint32_t devices_found;
    uint32_t devices_stored;
    uint32_t output_truncated;
} chos_pci_scan_summary_t;

/* Enumerate conventional PCI functions through checksum-validated ACPI MCFG
 * ECAM windows. If the EFI memory map describes an overlapping region, it must
 * be Reserved or MMIO; a missing descriptor is allowed because some UEFI maps
 * omit ECAM apertures. This is inventory only: BARs are read but not changed,
 * bus mastering and memory decoding are not enabled, and no driver is attached. */
int chos_pci_enumerate(const chos_boot_info_t *boot,
                       const chos_acpi_info_t *acpi,
                       chos_pci_device_t *devices,
                       size_t capacity,
                       chos_pci_scan_summary_t *summary);

/* Access one naturally aligned PCI configuration DWORD through a validated MCFG
 * ECAM aperture. Writes are only for a discovered function and are used by
 * native drivers to enable the command bits they require. */
int chos_pci_read_config32(const chos_boot_info_t *boot, const chos_acpi_info_t *acpi,
                           uint16_t segment, uint8_t bus, uint8_t device,
                           uint8_t function, uint16_t offset, uint32_t *value_out);
int chos_pci_write_config32(const chos_boot_info_t *boot, const chos_acpi_info_t *acpi,
                            uint16_t segment, uint8_t bus, uint8_t device,
                            uint8_t function, uint16_t offset, uint32_t value);
/* Decode an already assigned memory BAR. Returns zero for I/O, unassigned,
 * reserved-type, or malformed BARs. A 64-bit BAR consumes its next dword. */
int chos_pci_decode_memory_bar(const chos_pci_device_t *device, unsigned bar_index,
                               uint64_t *address_out, int *is_64_bit_out);
int chos_pci_enable_memory_busmaster(const chos_boot_info_t *boot,
                                    const chos_acpi_info_t *acpi,
                                    const chos_pci_device_t *device);

/* Driver-owned BAR sizing with decoding disabled, then translation through the
 * EFI root bridge resource snapshot. Reject RAM overlaps and ambiguous windows. */
int chos_pci_map_bar(const chos_boot_info_t *boot,const chos_acpi_info_t *acpi,
                     const chos_pci_device_t *device,unsigned index,
                     uint64_t *cpu_address,uint64_t *size);
#endif

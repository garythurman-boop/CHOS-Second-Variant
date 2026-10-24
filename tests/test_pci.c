#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "chos/pci.h"

#define ECAM_BYTES (1024u * 1024u)
#define PCI_DEVICE_STRIDE 0x8000u
#define PCI_FUNCTION_STRIDE 0x1000u

static uint8_t ecam[ECAM_BYTES] __attribute__((aligned(ECAM_BYTES)));
static chos_efi_memory_descriptor_t map[2];

static void write32(uint32_t offset, uint32_t value) {
    *(uint32_t *)(void *)(ecam + offset) = value;
}

static void init_boot(chos_boot_info_t *boot) {
    map[0] = (chos_efi_memory_descriptor_t){
        .type = 11u, .physical_start = (uint64_t)(uintptr_t)ecam,
        .number_of_pages = ECAM_BYTES / 4096u
    };
    map[1] = (chos_efi_memory_descriptor_t){
        .type = 7u, .physical_start = UINT64_C(0x100000000),
        .number_of_pages = 256u
    };
    *boot = (chos_boot_info_t){
        .magic = CHOS_BOOT_INFO_MAGIC, .version = CHOS_BOOT_INFO_VERSION,
        .struct_size = sizeof(*boot), .flags = CHOS_BOOT_FLAG_UEFI,
        .memory_map_address = (uint64_t)(uintptr_t)map,
        .memory_map_size = sizeof(map),
        .memory_descriptor_size = sizeof(map[0]),
        .memory_descriptor_count = 2u,
        .kernel_physical_base = UINT64_C(0x200000000),
        .kernel_image_size = 4096u,
        .kernel_entry = UINT64_C(0x200000000)
    };
}

int main(void) {
    chos_boot_info_t boot;
    chos_acpi_info_t acpi = {0};
    chos_pci_device_t devices[4];
    chos_pci_scan_summary_t summary;
    uint32_t f0 = 3u * PCI_DEVICE_STRIDE;
    uint32_t f1 = f0 + PCI_FUNCTION_STRIDE;
    uint32_t fxhci = 5u * PCI_DEVICE_STRIDE;

    init_boot(&boot);
    for (size_t i = 0u; i < sizeof(ecam); ++i) ecam[i] = 0xffu;
    /* VMware-like Ethernet test function 0, marked as a multifunction device. */
    write32(f0 + 0x00u, UINT32_C(0x07b015ad));
    write32(f0 + 0x08u, UINT32_C(0x02000001));
    write32(f0 + 0x0cu, UINT32_C(0x00800000));
    /* NVMe-class function 1 on the same device number. */
    write32(f1 + 0x00u, UINT32_C(0x00101234));
    write32(f1 + 0x08u, UINT32_C(0x01080200));
    write32(f1 + 0x0cu, 0u);
    /* xHCI-class controller at device 5; BAR values are inventory only. */
    write32(fxhci + 0x00u, UINT32_C(0x077015ad));
    write32(fxhci + 0x04u, UINT32_C(0x00100006));
    write32(fxhci + 0x08u, UINT32_C(0x0c033000));
    write32(fxhci + 0x0cu, 0u);
    write32(fxhci + 0x10u, UINT32_C(0xfec00004));
    write32(fxhci + 0x14u, UINT32_C(0x00000001));

    acpi.valid = 1u;
    acpi.pci_segment_count = 1u;
    acpi.pci_segments[0] = (chos_acpi_pci_segment_t){
        .ecam_base = (uint64_t)(uintptr_t)ecam,
        .segment_group = 0u, .start_bus = 0u, .end_bus = 0u
    };
    assert(chos_boot_info_contains_mmio(&boot, (uint64_t)(uintptr_t)ecam, ECAM_BYTES));
    assert(!chos_boot_info_contains_mmio(&boot, UINT64_C(0x100000000), 4096u));
    assert(chos_pci_enumerate(&boot, &acpi, devices, 4u, &summary));
    assert(summary.segments_considered == 1u);
    assert(summary.buses_scanned == 1u && summary.buses_skipped_unmapped == 0u);
    assert(summary.devices_found == 3u && summary.devices_stored == 3u);
    assert(summary.output_truncated == 0u);
    assert(devices[0].bus == 0u && devices[0].device == 3u && devices[0].function == 0u);
    assert(devices[0].vendor_id == 0x15adu && devices[0].device_id == 0x07b0u);
    assert(devices[0].class_code == 0x02u && devices[0].subclass == 0x00u);
    assert(devices[1].device == 3u && devices[1].function == 1u);
    assert(devices[1].vendor_id == 0x1234u && devices[1].device_id == 0x0010u);
    assert(devices[1].class_code == 0x01u && devices[1].subclass == 0x08u);
    assert(devices[2].device == 5u && devices[2].function == 0u);
    assert(devices[2].vendor_id == 0x15adu && devices[2].device_id == 0x0770u);
    assert(devices[2].class_code == 0x0cu && devices[2].subclass == 0x03u);
    assert(devices[2].programming_interface == 0x30u);
    assert(devices[2].command == 0x0006u && devices[2].status == 0x0010u);
    assert(devices[2].bars[0] == UINT32_C(0xfec00004));
    assert(devices[2].bars[1] == UINT32_C(0x00000001));
    {
        uint32_t config;
        uint64_t bar_address;
        int bar64 = 0;
        assert(chos_pci_read_config32(&boot, &acpi, 0u, 0u, 5u, 0u, 0u, &config));
        assert(config == UINT32_C(0x077015ad));
        assert(chos_pci_decode_memory_bar(&devices[2], 0u, &bar_address, &bar64));
        assert(bar64 && bar_address == UINT64_C(0x1fec00000));
        /* The command register write must enable memory decoding and bus mastering. */
        assert(chos_pci_write_config32(&boot, &acpi, 0u, 0u, 3u, 1u, 0x04u, 0u));
        assert(chos_pci_enable_memory_busmaster(&boot, &acpi, &devices[1]));
        assert(chos_pci_read_config32(&boot, &acpi, 0u, 0u, 3u, 1u, 0x04u, &config));
        assert((config & 0x0006u) == 0x0006u);
        assert(!chos_pci_write_config32(&boot, &acpi, 0u, 0u, 5u, 0u, 0x03u, 0u));
        assert(!chos_pci_decode_memory_bar(&devices[2], 6u, &bar_address, &bar64));
    }

    assert(chos_pci_enumerate(&boot, &acpi, devices, 1u, &summary));
    assert(summary.devices_found == 3u && summary.devices_stored == 1u && summary.output_truncated);

    /* A conventional-RAM descriptor is not permission to dereference ECAM. */
    map[0].type = 7u;
    assert(chos_pci_enumerate(&boot, &acpi, devices, 4u, &summary));
    assert(summary.buses_scanned == 0u && summary.buses_skipped_unmapped == 1u);
    assert(summary.devices_found == 0u);

    /* Some UEFI maps omit the MCFG-declared ECAM range. It is safe to use that
     * firmware-declared window only when no EFI descriptor overlaps it as RAM
     * or an unknown kind of resource. */
    map[0] = (chos_efi_memory_descriptor_t){
        .type = 7u, .physical_start = UINT64_C(0x100000000), .number_of_pages = 256u
    };
    map[1] = (chos_efi_memory_descriptor_t){
        .type = 7u, .physical_start = UINT64_C(0x300000000), .number_of_pages = 256u
    };
    assert(chos_pci_enumerate(&boot, &acpi, devices, 4u, &summary));
    assert(summary.buses_scanned == 1u && summary.buses_skipped_unmapped == 0u);
    assert(summary.devices_found == 3u && summary.devices_stored == 3u);

    /* Malformed descriptor ranges must fail closed, not be ignored as absent. */
    map[0] = (chos_efi_memory_descriptor_t){
        .type = 7u, .physical_start = UINT64_MAX - UINT64_C(4095), .number_of_pages = 2u
    };
    map[1] = (chos_efi_memory_descriptor_t){
        .type = 7u, .physical_start = UINT64_C(0x300000000), .number_of_pages = 256u
    };
    assert(chos_pci_enumerate(&boot, &acpi, devices, 4u, &summary));
    assert(summary.buses_scanned == 0u && summary.buses_skipped_unmapped == 1u);
    assert(summary.devices_found == 0u && summary.devices_stored == 0u);

    puts("PCI ECAM inventory: MCFG fallback, RAM-overlap and malformed-map rejection, BAR snapshot, BDF/multifunction scanning and bounded output passed");
    return 0;
}

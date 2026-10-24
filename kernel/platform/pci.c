#include <stddef.h>
#include <stdint.h>
#include "chos/pci.h"

#define PCI_CONFIG_BUS_SIZE (UINT64_C(1) << 20)
#define PCI_DEVICES_PER_BUS 32u
#define PCI_FUNCTIONS_PER_DEVICE 8u
#define PCI_VENDOR_DEVICE_OFFSET 0x00u
#define PCI_CLASS_OFFSET 0x08u
#define PCI_HEADER_OFFSET 0x0cu

static uint32_t config_read32(uint64_t address) {
    return *(volatile const uint32_t *)(uintptr_t)address;
}

static uint16_t low16(uint32_t value) { return (uint16_t)(value & 0xffffu); }
static uint16_t high16(uint32_t value) { return (uint16_t)(value >> 16); }

static int bus_address(const chos_acpi_pci_segment_t *segment, uint8_t bus,
                       uint64_t *address_out) {
    uint64_t bus_delta, address;
    if (segment == NULL || address_out == NULL || bus < segment->start_bus ||
        bus > segment->end_bus) return 0;
    bus_delta = (uint64_t)(bus - segment->start_bus) * PCI_CONFIG_BUS_SIZE;
    if (segment->ecam_base > UINT64_MAX - bus_delta) return 0;
    address = segment->ecam_base + bus_delta;
    if (address > UINT64_MAX - PCI_CONFIG_BUS_SIZE) return 0;
    *address_out = address;
    return 1;
}


/* ACPI MCFG is the authoritative declaration of an ECAM aperture. Some UEFI
 * implementations omit ECAM from GetMemoryMap entirely, so requiring one
 * descriptor to cover every bus can hide all PCI devices. Permit a validated
 * MCFG bus window when the EFI map either labels it Reserved/MMIO or does not
 * describe that range at all. Any overlap with RAM-backed/unknown EFI types is
 * still rejected. The loader enters the kernel with the MMU off, so these are
 * physical accesses; the ACPI parser has already validated the MCFG checksum. */
static int ecam_window_safe(const chos_boot_info_t *boot, uint64_t address, uint64_t length) {
    const uint8_t *map;
    uint64_t end, offset;
    if (!chos_boot_info_validate(boot) || address == 0u || length == 0u ||
        address > UINT64_MAX - length) return 0;
    end = address + length;
    map = (const uint8_t *)(uintptr_t)boot->memory_map_address;
    for (offset = 0u; offset < boot->memory_map_size; offset += boot->memory_descriptor_size) {
        const chos_efi_memory_descriptor_t *desc =
            (const chos_efi_memory_descriptor_t *)(const void *)(map + offset);
        uint64_t bytes, desc_end;
        if (desc->number_of_pages == 0u) continue;
        /* A malformed descriptor makes the map insufficient to prove safety. */
        if (desc->number_of_pages > UINT64_MAX / UINT64_C(4096)) return 0;
        bytes = desc->number_of_pages * UINT64_C(4096);
        if (desc->physical_start > UINT64_MAX - bytes) return 0;
        desc_end = desc->physical_start + bytes;
        if (desc->physical_start >= end || desc_end <= address) continue;
        /* EFI type 0=ReservedMemory, 11=MemoryMappedIO. If any other type
         * overlaps ECAM, do not dereference it as PCI configuration space. */
        if (desc->type != 0u && desc->type != 11u) return 0;
    }
    return 1;
}


static int config_dword_address(const chos_boot_info_t *boot, const chos_acpi_info_t *acpi,
                                uint16_t segment_group, uint8_t bus, uint8_t device,
                                uint8_t function, uint16_t offset, uint64_t *address_out) {
    uint32_t i;
    if (address_out == NULL || device >= PCI_DEVICES_PER_BUS ||
        function >= PCI_FUNCTIONS_PER_DEVICE || (offset & 3u) != 0u || offset >= 4096u ||
        !chos_boot_info_validate(boot) || acpi == NULL || acpi->valid == 0u ||
        acpi->pci_segment_count > CHOS_ACPI_MAX_PCI_SEGMENTS) return 0;
    for (i = 0u; i < acpi->pci_segment_count; ++i) {
        const chos_acpi_pci_segment_t *segment = &acpi->pci_segments[i];
        uint64_t bus_base, delta, address;
        if (segment->segment_group != segment_group || bus < segment->start_bus ||
            bus > segment->end_bus || segment->ecam_base == 0u ||
            (segment->ecam_base & (PCI_CONFIG_BUS_SIZE - 1u)) != 0u) continue;
        if (!bus_address(segment, bus, &bus_base)) continue;
        delta = ((uint64_t)device << 15) + ((uint64_t)function << 12) + offset;
        if (bus_base > UINT64_MAX - delta) return 0;
        address = bus_base + delta;
        if (!ecam_window_safe(boot, bus_base, PCI_CONFIG_BUS_SIZE)) return 0;
        if (address > UINT64_MAX - sizeof(uint32_t)) return 0;
        *address_out = address;
        return 1;
    }
    return 0;
}

int chos_pci_read_config32(const chos_boot_info_t *boot, const chos_acpi_info_t *acpi,
                           uint16_t segment, uint8_t bus, uint8_t device,
                           uint8_t function, uint16_t offset, uint32_t *value_out) {
    uint64_t address;
    if (value_out == NULL || !config_dword_address(boot, acpi, segment, bus, device,
                                                    function, offset, &address)) return 0;
    *value_out = config_read32(address);
    return 1;
}

int chos_pci_write_config32(const chos_boot_info_t *boot, const chos_acpi_info_t *acpi,
                            uint16_t segment, uint8_t bus, uint8_t device,
                            uint8_t function, uint16_t offset, uint32_t value) {
    uint64_t address;
    if (!config_dword_address(boot, acpi, segment, bus, device,
                              function, offset, &address)) return 0;
    *(volatile uint32_t *)(uintptr_t)address = value;
#if defined(__aarch64__)
    __asm__ volatile("dsb sy" ::: "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
    return 1;
}

int chos_pci_decode_memory_bar(const chos_pci_device_t *device, unsigned bar_index,
                               uint64_t *address_out, int *is_64_bit_out) {
    uint32_t low, type;
    uint64_t address;
    int is64;
    if (device == NULL || address_out == NULL || bar_index >= 6u) return 0;
    low = device->bars[bar_index];
    if (low == 0u || (low & 1u) != 0u) return 0; /* I/O BAR or unassigned */
    type = (low >> 1u) & 3u;
    is64 = type == 2u;
    if (type == 1u || type == 3u || (is64 && bar_index == 5u)) return 0;
    address = (uint64_t)(low & ~UINT32_C(0x0f));
    if (is64) address |= (uint64_t)device->bars[bar_index + 1u] << 32u;
    if (address == 0u || (address & 3u) != 0u) return 0;
    *address_out = address;
    if (is_64_bit_out != NULL) *is_64_bit_out = is64;
    return 1;
}

int chos_pci_enable_memory_busmaster(const chos_boot_info_t *boot,
                                    const chos_acpi_info_t *acpi,
                                    const chos_pci_device_t *device) {
    uint32_t command_status;
    uint16_t command;
    if (device == NULL || !chos_pci_read_config32(boot, acpi, device->segment,
            device->bus, device->device, device->function, 0x04u, &command_status)) return 0;
    command = (uint16_t)command_status;
    command = (uint16_t)(command | UINT16_C(0x0006)); /* memory decode + bus master */
    /* The upper half is PCI status with W1C bits; writing zeros avoids clearing them. */
    if (!chos_pci_write_config32(boot, acpi, device->segment, device->bus,
            device->device, device->function, 0x04u, (uint32_t)command)) return 0;
    if (!chos_pci_read_config32(boot, acpi, device->segment, device->bus,
            device->device, device->function, 0x04u, &command_status)) return 0;
    return (((uint16_t)command_status) & UINT16_C(0x0006)) == UINT16_C(0x0006);
}

static void record_function(uint64_t function_address, uint32_t id,
                            uint32_t class_register, uint32_t header_register,
                            const chos_acpi_pci_segment_t *segment, uint8_t bus,
                            uint8_t device, uint8_t function,
                            chos_pci_device_t *output, size_t capacity,
                            chos_pci_scan_summary_t *summary) {
    chos_pci_device_t item;
    uint16_t vendor = low16(id);
    uint16_t product = high16(id);
    if (vendor == 0u || vendor == 0xffffu) return;
    item.segment = segment->segment_group;
    item.bus = bus;
    item.device = device;
    item.function = function;
    item.header_type = (uint8_t)(header_register >> 16);
    item.programming_interface = (uint8_t)(class_register >> 8);
    item.subclass = (uint8_t)(class_register >> 16);
    item.class_code = (uint8_t)(class_register >> 24);
    item.vendor_id = vendor;
    item.device_id = product;
    {
        uint32_t command_status = config_read32(function_address + 0x04u);
        uint32_t header_type = (header_register >> 16u) & 0x7fu;
        uint32_t bar_count = header_type == 0u ? 6u : (header_type == 1u ? 2u : (header_type == 2u ? 1u : 0u));
        uint32_t bar_index;
        item.command = low16(command_status);
        item.status = high16(command_status);
        for (bar_index = 0u; bar_index < 6u; ++bar_index) item.bars[bar_index] = 0u;
        /* Read assigned BAR values for inventory only; never probe by writing all-ones
         * and never enable memory decoding or bus mastering in this discovery pass. */
        for (bar_index = 0u; bar_index < bar_count; ++bar_index)
            item.bars[bar_index] = config_read32(function_address + 0x10u + bar_index * 4u);
    }
    ++summary->devices_found;
    if (summary->devices_stored < capacity) {
        output[summary->devices_stored++] = item;
    } else {
        summary->output_truncated = 1u;
    }
}

int chos_pci_enumerate(const chos_boot_info_t *boot,
                       const chos_acpi_info_t *acpi,
                       chos_pci_device_t *devices,
                       size_t capacity,
                       chos_pci_scan_summary_t *summary) {
    uint32_t seg_index;
    if (summary == NULL || !chos_boot_info_validate(boot) || acpi == NULL ||
        acpi->valid == 0u || acpi->pci_segment_count > CHOS_ACPI_MAX_PCI_SEGMENTS ||
        (capacity != 0u && devices == NULL)) return 0;
    {
        uint8_t *bytes = (uint8_t *)summary;
        size_t i;
        for (i = 0u; i < sizeof(*summary); ++i) bytes[i] = 0u;
    }
    summary->segments_considered = acpi->pci_segment_count;
    for (seg_index = 0u; seg_index < acpi->pci_segment_count; ++seg_index) {
        const chos_acpi_pci_segment_t *segment = &acpi->pci_segments[seg_index];
        uint32_t bus_value;
        if (segment->ecam_base == 0u || segment->end_bus < segment->start_bus ||
            (segment->ecam_base & (PCI_CONFIG_BUS_SIZE - 1u)) != 0u) continue;
        for (bus_value = segment->start_bus; bus_value <= segment->end_bus; ++bus_value) {
            uint64_t base;
            uint32_t dev;
            if (!bus_address(segment, (uint8_t)bus_value, &base)) continue;
            /* A checksum-valid MCFG can declare ECAM even if the EFI memory map
             * omits the aperture. Reject conflicting RAM/unknown descriptors. */
            if (!ecam_window_safe(boot, base, PCI_CONFIG_BUS_SIZE)) {
                ++summary->buses_skipped_unmapped;
                continue;
            }
            ++summary->buses_scanned;
            for (dev = 0u; dev < PCI_DEVICES_PER_BUS; ++dev) {
                uint64_t function0 = base + ((uint64_t)dev << 15);
                uint32_t id0 = config_read32(function0 + PCI_VENDOR_DEVICE_OFFSET);
                uint32_t header0;
                uint32_t function_count, fn;
                if (low16(id0) == 0u || low16(id0) == 0xffffu) continue;
                header0 = config_read32(function0 + PCI_HEADER_OFFSET);
                function_count = (((header0 >> 16) & 0x80u) != 0u)
                               ? PCI_FUNCTIONS_PER_DEVICE : 1u;
                for (fn = 0u; fn < function_count; ++fn) {
                    uint64_t function_address = function0 + ((uint64_t)fn << 12);
                    uint32_t id = fn == 0u ? id0 : config_read32(function_address + PCI_VENDOR_DEVICE_OFFSET);
                    uint32_t cls, header;
                    if (fn != 0u && (low16(id) == 0u || low16(id) == 0xffffu)) continue;
                    cls = config_read32(function_address + PCI_CLASS_OFFSET);
                    header = config_read32(function_address + PCI_HEADER_OFFSET);
                    record_function(function_address, id, cls, header, segment,
                                    (uint8_t)bus_value, (uint8_t)dev, (uint8_t)fn,
                                    devices, capacity, summary);
                }
            }
        }
    }
    return 1;
}

int chos_pci_map_bar(const chos_boot_info_t *boot,const chos_acpi_info_t *acpi,
                     const chos_pci_device_t *d,unsigned index,uint64_t *cpu,uint64_t *size) {
    uint64_t bus,mask,bytes,physical=0;
    uint32_t command,lo=0,hi=0,original_lo,original_hi=0;
    int is64;
    if(!cpu || !size || !chos_pci_decode_memory_bar(d,index,&bus,&is64) ||
       !chos_boot_info_validate(boot)) return 0;
    if(!chos_pci_read_config32(boot,acpi,d->segment,d->bus,d->device,d->function,4,&command) ||
       !chos_pci_read_config32(boot,acpi,d->segment,d->bus,d->device,d->function,(uint16_t)(0x10+index*4),&original_lo)) return 0;
    if(is64 && !chos_pci_read_config32(boot,acpi,d->segment,d->bus,d->device,d->function,(uint16_t)(0x14+index*4),&original_hi)) return 0;
    /* Caller owns this function. Never size a BAR with active decode/DMA. */
    if(!chos_pci_write_config32(boot,acpi,d->segment,d->bus,d->device,d->function,4,(command&0xffffu)&~6u)) return 0;
    int ok=chos_pci_write_config32(boot,acpi,d->segment,d->bus,d->device,d->function,(uint16_t)(0x10+index*4),UINT32_MAX);
    if(is64) ok &= chos_pci_write_config32(boot,acpi,d->segment,d->bus,d->device,d->function,(uint16_t)(0x14+index*4),UINT32_MAX);
    ok &= chos_pci_read_config32(boot,acpi,d->segment,d->bus,d->device,d->function,(uint16_t)(0x10+index*4),&lo);
    if(is64) ok &= chos_pci_read_config32(boot,acpi,d->segment,d->bus,d->device,d->function,(uint16_t)(0x14+index*4),&hi);
    ok &= chos_pci_write_config32(boot,acpi,d->segment,d->bus,d->device,d->function,(uint16_t)(0x10+index*4),original_lo);
    if(is64) ok &= chos_pci_write_config32(boot,acpi,d->segment,d->bus,d->device,d->function,(uint16_t)(0x14+index*4),original_hi);
    ok &= chos_pci_write_config32(boot,acpi,d->segment,d->bus,d->device,d->function,4,command&0xffffu);
    if(!ok) return 0;
    mask=(lo&~UINT32_C(15))|((uint64_t)(is64?hi:UINT32_MAX)<<32);
    bytes=~mask+1;
    if(!bytes || (bytes&(bytes-1)) || bus&(bytes-1) || bus>UINT64_MAX-bytes) return 0;
    unsigned found=0;
    for(unsigned i=0;i<boot->pci_window_count;++i) {
        const chos_pci_window_t *w=&boot->pci_windows[i];
        if(w->segment!=d->segment || bus<w->bus_base || bus-w->bus_base>w->size ||
            bytes>w->size-(bus-w->bus_base)) continue;
        physical=w->cpu_base+(bus-w->bus_base);++found;
    }
    if(found!=1) {
        if(found || !chos_boot_info_contains_mmio(boot,bus,bytes)) return 0;
        physical=bus;
    }
    if(!ecam_window_safe(boot,physical,bytes)) return 0;
    *cpu=physical;*size=bytes;return 1;
}

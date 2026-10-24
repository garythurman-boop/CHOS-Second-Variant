#include <stddef.h>
#include <stdint.h>
#include "chos/acpi.h"

#define ACPI_HEADER_SIZE 36u
#define ACPI_MAX_TABLE_BYTES (1024u * 1024u)
#define ACPI_MAX_ROOT_ENTRIES 256u
#define MADT_FIXED_SIZE 44u
#define EFI_PAGE_SIZE 4096u

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t le64(const uint8_t *p) {
    uint64_t value = 0u;
    unsigned i;
    for (i = 0u; i < 8u; ++i) value |= (uint64_t)p[i] << (i * 8u);
    return value;
}

static int sig_is(const uint8_t *p, const char *s) {
    unsigned i;
    for (i = 0u; i < 4u; ++i) if (p[i] != (uint8_t)s[i]) return 0;
    return 1;
}

static int checksum_ok(const uint8_t *p, uint32_t length) {
    uint8_t sum = 0u;
    uint32_t i;
    for (i = 0u; i < length; ++i) sum = (uint8_t)(sum + p[i]);
    return sum == 0u;
}

static int valid_table(const chos_boot_info_t *boot, uint64_t address,
                       const char *signature, uint32_t minimum, uint32_t *length_out) {
    const uint8_t *table;
    uint32_t length;
    if (address == 0u || !chos_boot_info_contains_memory(boot, address, ACPI_HEADER_SIZE)) return 0;
    table = (const uint8_t *)(uintptr_t)address;
    if (signature != NULL && !sig_is(table, signature)) return 0;
    length = le32(table + 4u);
    if (length < minimum || length > ACPI_MAX_TABLE_BYTES ||
        !chos_boot_info_contains_memory(boot, address, length) || !checksum_ok(table, length)) return 0;
    if (length_out != NULL) *length_out = length;
    return 1;
}

static chos_acpi_error_t parse_madt(const chos_boot_info_t *boot, uint64_t address,
                                    chos_acpi_info_t *out) {
    const uint8_t *table;
    uint32_t length, offset;
    if (!valid_table(boot, address, "APIC", MADT_FIXED_SIZE, &length)) return CHOS_ACPI_ERR_BAD_MADT;
    table = (const uint8_t *)(uintptr_t)address;
    offset = MADT_FIXED_SIZE;
    while (offset < length) {
        uint8_t type, entry_length;
        const uint8_t *entry;
        if (length - offset < 2u) return CHOS_ACPI_ERR_BAD_MADT;
        entry = table + offset;
        type = entry[0]; entry_length = entry[1];
        if (entry_length < 2u || entry_length > length - offset) return CHOS_ACPI_ERR_BAD_MADT;
        /* ACPI 6.x MADT ARM structures: GICD=0x0c, GICR=0x0e, GICC=0x0b. */
        if (type == 0x0cu) {
            if (entry_length < 24u) return CHOS_ACPI_ERR_BAD_MADT;
            out->gic_distributor_base = le64(entry + 8u);
            out->gic_version = le32(entry + 20u);
        } else if (type == 0x0eu) {
            if (entry_length < 16u) return CHOS_ACPI_ERR_BAD_MADT;
            if (out->gic_redistributor_base == 0u) {
                out->gic_redistributor_base = le64(entry + 4u);
                out->gic_redistributor_length = le32(entry + 12u);
            }
        } else if (type == 0x0bu) {
            if (entry_length < 80u) return CHOS_ACPI_ERR_BAD_MADT;
            ++out->gic_cpu_interface_count;
            /* GICC's redistributor base is a fallback when GICR records are absent. */
            if (out->gic_redistributor_base == 0u) {
                uint64_t base = le64(entry + 60u);
                if (base != 0u) out->gic_redistributor_base = base;
            }
        }
        offset += entry_length;
    }
    out->madt_address = address;
    if (out->gic_distributor_base == 0u) return CHOS_ACPI_ERR_BAD_MADT;
    return CHOS_ACPI_OK;
}

static chos_acpi_error_t parse_gtdt(const chos_boot_info_t *boot, uint64_t address,
                                    chos_acpi_info_t *out) {
    const uint8_t *table;
    if (!valid_table(boot, address, "GTDT", 80u, NULL)) return CHOS_ACPI_ERR_BAD_GTDT;
    table = (const uint8_t *)(uintptr_t)address;
    out->physical_timer_irq = le32(table + 56u);
    out->physical_timer_flags = le32(table + 60u);
    out->virtual_timer_irq = le32(table + 64u);
    out->virtual_timer_flags = le32(table + 68u);
    out->gtdt_address = address;
    /* GSIV zero is not a usable interrupt. */
    if (out->physical_timer_irq == 0u) return CHOS_ACPI_ERR_BAD_GTDT;
    return CHOS_ACPI_OK;
}

static chos_acpi_error_t parse_mcfg(const chos_boot_info_t *boot, uint64_t address,
                                    chos_acpi_info_t *out) {
    const uint8_t *table;
    uint32_t length, offset;
    if (!valid_table(boot, address, "MCFG", 44u, &length)) return CHOS_ACPI_ERR_BAD_MCFG;
    table = (const uint8_t *)(uintptr_t)address;
    if (((length - 44u) % 16u) != 0u) return CHOS_ACPI_ERR_BAD_MCFG;
    for (offset = 44u; offset < length; offset += 16u) {
        const uint8_t *entry = table + offset;
        chos_acpi_pci_segment_t segment;
        if (out->pci_segment_count >= CHOS_ACPI_MAX_PCI_SEGMENTS) break;
        segment.ecam_base = le64(entry);
        segment.segment_group = (uint16_t)((uint16_t)entry[8] | ((uint16_t)entry[9] << 8));
        segment.start_bus = entry[10]; segment.end_bus = entry[11];
        if (segment.ecam_base == 0u || segment.end_bus < segment.start_bus ||
            (segment.ecam_base & ((UINT64_C(1) << 20) - 1u)) != 0u) continue;
        out->pci_segments[out->pci_segment_count++] = segment;
    }
    out->mcfg_address = address;
    return CHOS_ACPI_OK;
}

chos_acpi_error_t chos_acpi_parse(const chos_boot_info_t *boot, chos_acpi_info_t *out) {
    const uint8_t *rsdp, *root;
    uint64_t root_address;
    uint32_t revision, root_length, entry_size, count, i;
    uint64_t rsdt_address;
    int xsdt;
    chos_acpi_error_t result;
    if (boot == NULL || out == NULL || !chos_boot_info_validate(boot))
        return CHOS_ACPI_ERR_ARGUMENT;
    {
        uint8_t *p = (uint8_t *)out;
        size_t n;
        for (n = 0u; n < sizeof(*out); ++n) p[n] = 0u;
    }
    out->rsdp_address = boot->acpi_rsdp_address;
    if ((boot->flags & CHOS_BOOT_FLAG_ACPI) == 0u || boot->acpi_rsdp_address == 0u)
        return CHOS_ACPI_ERR_NO_RSDP;
    if ((boot->acpi_rsdp_address & 7u) != 0u ||
        !chos_boot_info_contains_memory(boot, boot->acpi_rsdp_address, 20u))
        return CHOS_ACPI_ERR_BAD_RSDP;
    rsdp = (const uint8_t *)(uintptr_t)boot->acpi_rsdp_address;
    if (rsdp[0] != 'R' || rsdp[1] != 'S' || rsdp[2] != 'D' || rsdp[3] != ' ' ||
        rsdp[4] != 'P' || rsdp[5] != 'T' || rsdp[6] != 'R' || rsdp[7] != ' ' ||
        !checksum_ok(rsdp, 20u)) return CHOS_ACPI_ERR_BAD_RSDP;
    revision = rsdp[15]; out->rsdp_revision = (uint8_t)revision;
    rsdt_address = (uint64_t)rsdp[16] | ((uint64_t)rsdp[17] << 8) |
                   ((uint64_t)rsdp[18] << 16) | ((uint64_t)rsdp[19] << 24);
    root_address = rsdt_address;
    xsdt = 0;
    if (revision >= 2u) {
        uint32_t declared;
        if (!chos_boot_info_contains_memory(boot, boot->acpi_rsdp_address, 36u))
            return CHOS_ACPI_ERR_BAD_RSDP;
        declared = le32(rsdp + 20u);
        if (declared < 36u || declared > 4096u ||
            !chos_boot_info_contains_memory(boot, boot->acpi_rsdp_address, declared) ||
            !checksum_ok(rsdp, declared)) return CHOS_ACPI_ERR_BAD_RSDP;
        /* Prefer XSDT when firmware supplies it; otherwise retain the legacy
         * RSDT pointer for revision-2+ RSDPs that leave XsdtAddress zero. */
        {
            uint64_t xsdt_address = le64(rsdp + 24u);
            if (xsdt_address != 0u) {
                root_address = xsdt_address;
                xsdt = 1;
            }
        }
    }
    if (root_address == 0u || !valid_table(boot, root_address, xsdt ? "XSDT" : "RSDT", 36u, &root_length))
        return CHOS_ACPI_ERR_BAD_ROOT;
    root = (const uint8_t *)(uintptr_t)root_address;
    entry_size = xsdt ? 8u : 4u;
    if ((root_length - 36u) % entry_size != 0u) return CHOS_ACPI_ERR_BAD_ROOT;
    count = (root_length - 36u) / entry_size;
    if (count > ACPI_MAX_ROOT_ENTRIES) return CHOS_ACPI_ERR_BAD_ROOT;
    out->root_table_address = root_address;
    out->root_is_xsdt = (uint8_t)xsdt;
    for (i = 0u; i < count; ++i) {
        uint64_t child = xsdt ? le64(root + 36u + i * 8u) : le32(root + 36u + i * 4u);
        const uint8_t *table;
        uint32_t length;
        if (child == 0u || !chos_boot_info_contains_memory(boot, child, ACPI_HEADER_SIZE)) continue;
        table = (const uint8_t *)(uintptr_t)child;
        /* Known platform tables must fail closed if malformed. Do not silently
         * skip a broken checksum/length and report a partially valid platform. */
        if (sig_is(table, "APIC")) {
            result = parse_madt(boot, child, out);
            if (result != CHOS_ACPI_OK) return result;
        } else if (sig_is(table, "GTDT")) {
            result = parse_gtdt(boot, child, out);
            if (result != CHOS_ACPI_OK) return result;
        } else if (sig_is(table, "MCFG")) {
            result = parse_mcfg(boot, child, out);
            if (result != CHOS_ACPI_OK) return result;
        } else if (sig_is(table, "IORT")) {
            /* IORT can describe MSI routing through an ITS without an SMMU.
             * Accept only bounded ITS/root nodes; any SMMU, unknown node or
             * malformed stream requires native DMA topology support first. */
            out->dma_translation_unverified=1u;
            if(valid_table(boot,child,"IORT",48u,&length)) {
                uint32_t nodes=le32(table+36), offset=le32(table+40), n;
                if(nodes<=256 && offset>=48 && offset<=length) {
                    for(n=0;n<nodes;++n) {
                        if(length-offset<16u) break;
                        const uint8_t *node=table+offset;
                        uint32_t bytes=(uint32_t)node[1]|((uint32_t)node[2]<<8);
                        uint32_t mappings=le32(node+8), map_offset=le32(node+12);
                        if(bytes<20 || bytes>length-offset || (node[0]!=0 && node[0]!=2) ||
                           (node[0]==0 && le32(node+16)>(bytes-20)/4) || (node[0]==2 && bytes<36) ||
                           (mappings && (map_offset<16 || map_offset>bytes || mappings>(bytes-map_offset)/20))) break;
                        offset+=bytes;
                    }
                    if(n==nodes) out->dma_translation_unverified=0;
                }
            }
        } else if (sig_is(table, "SPCR")) {
            if (valid_table(boot, child, "SPCR", 80u, &length) && table[36]==3u &&
                table[40]==0u && table[42]==0u && (table[43]==0u || table[43]==3u)) {
                uint64_t base=le64(table+44);
                if (base && !(base&3u) && chos_boot_info_mmio_safe(boot,base,0x1000u)) out->pl011_base=base;
            }
        } else if (!valid_table(boot, child, NULL, ACPI_HEADER_SIZE, &length)) {
            /* Unknown invalid ACPI tables are not consumed by this parser. */
            continue;
        }
    }
    out->valid = 1u;
    return CHOS_ACPI_OK;
}

const char *chos_acpi_error_string(chos_acpi_error_t error) {
    switch (error) {
        case CHOS_ACPI_OK: return "ok";
        case CHOS_ACPI_ERR_ARGUMENT: return "invalid argument or boot-info ABI";
        case CHOS_ACPI_ERR_NO_RSDP: return "ACPI RSDP not supplied";
        case CHOS_ACPI_ERR_BAD_RSDP: return "RSDP signature, checksum, length, or range invalid";
        case CHOS_ACPI_ERR_BAD_ROOT: return "RSDT/XSDT invalid";
        case CHOS_ACPI_ERR_BAD_MADT: return "MADT/GIC data invalid";
        case CHOS_ACPI_ERR_BAD_GTDT: return "GTDT timer data invalid";
        case CHOS_ACPI_ERR_BAD_MCFG: return "MCFG PCI segments invalid";
        default: return "unknown ACPI error";
    }
}

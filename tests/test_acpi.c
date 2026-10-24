#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "chos/acpi.h"

static uint8_t arena[64u * 1024u] __attribute__((aligned(4096)));
static chos_efi_memory_descriptor_t memory_map[2];

static void put16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void put32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static void put64(uint8_t *p, uint64_t v) { unsigned i; for(i=0;i<8u;i++) p[i]=(uint8_t)(v>>(i*8u)); }
static void checksum(uint8_t *p, size_t length, size_t offset) {
    uint8_t sum=0; size_t i; p[offset]=0;
    for(i=0;i<length;i++) sum=(uint8_t)(sum+p[i]);
    p[offset]=(uint8_t)(0u-sum);
}
static uint8_t *at(size_t offset) { return arena+offset; }
static void init_sdt(uint8_t *p, const char *sig, uint32_t length) {
    memcpy(p,sig,4); put32(p+4,length); p[8]=1; memcpy(p+10,"CHOS  ",6); memcpy(p+16,"ACPI TEST",9); put32(p+24,1); memcpy(p+28,"OPEN",4); put32(p+32,1);
}
static void fix_sdt(uint8_t *p, uint32_t length) { checksum(p,length,9); }

static void make_fixture(chos_boot_info_t *boot) {
    uint8_t *rsdp=at(0x100), *xsdt=at(0x200), *madt=at(0x300), *gtdt=at(0x500), *mcfg=at(0x600);
    uint8_t *gicd, *gicr, *gicc;
    uint32_t madt_len=44u+24u+16u+80u;
    memset(arena,0,sizeof(arena)); memset(memory_map,0,sizeof(memory_map)); memset(boot,0,sizeof(*boot));
    memory_map[0].type=9u; memory_map[0].physical_start=(uint64_t)(uintptr_t)arena;
    memory_map[0].number_of_pages=sizeof(arena)/4096u;
    memory_map[1].type=2u; memory_map[1].physical_start=(uint64_t)(uintptr_t)memory_map;
    memory_map[1].number_of_pages=1u;

    memcpy(rsdp,"RSD PTR ",8); memcpy(rsdp+9,"CHOS  ",6); rsdp[15]=2u;
    put32(rsdp+20,36u); put64(rsdp+24,(uint64_t)(uintptr_t)xsdt);
    checksum(rsdp,20u,8u); checksum(rsdp,36u,32u);

    init_sdt(xsdt,"XSDT",36u+3u*8u);
    put64(xsdt+36,(uint64_t)(uintptr_t)madt);
    put64(xsdt+44,(uint64_t)(uintptr_t)gtdt);
    put64(xsdt+52,(uint64_t)(uintptr_t)mcfg);
    fix_sdt(xsdt,60u);

    init_sdt(madt,"APIC",madt_len); put32(madt+36,0u); put32(madt+40,1u);
    gicd=madt+44; gicd[0]=0x0cu; gicd[1]=24u; put32(gicd+4,0u); put64(gicd+8,0x2f000000u); put32(gicd+16,0u); put32(gicd+20,3u);
    gicr=gicd+24; gicr[0]=0x0eu; gicr[1]=16u; put64(gicr+4,0x2f100000u); put32(gicr+12,0x20000u);
    gicc=gicr+16; gicc[0]=0x0bu; gicc[1]=80u; put32(gicc+4,0u); put32(gicc+8,0u); put32(gicc+12,1u); put64(gicc+60,0u);
    fix_sdt(madt,madt_len);

    init_sdt(gtdt,"GTDT",80u); put32(gtdt+56,30u); put32(gtdt+60,1u); put32(gtdt+64,27u); put32(gtdt+68,1u); fix_sdt(gtdt,80u);

    init_sdt(mcfg,"MCFG",60u); put64(mcfg+44,0x40000000u); put16(mcfg+52,0u); mcfg[54]=0u; mcfg[55]=255u; fix_sdt(mcfg,60u);

    boot->magic=CHOS_BOOT_INFO_MAGIC; boot->version=CHOS_BOOT_INFO_VERSION; boot->struct_size=sizeof(*boot);
    boot->flags=CHOS_BOOT_FLAG_UEFI|CHOS_BOOT_FLAG_ACPI;
    boot->memory_map_address=(uint64_t)(uintptr_t)memory_map; boot->memory_map_size=sizeof(memory_map);
    boot->memory_descriptor_size=sizeof(memory_map[0]); boot->memory_descriptor_version=1u; boot->memory_descriptor_count=2u;
    boot->kernel_physical_base=(uint64_t)(uintptr_t)arena+0x3000u; boot->kernel_image_size=0x1000u;
    boot->kernel_entry=boot->kernel_physical_base+0x100u; boot->acpi_rsdp_address=(uint64_t)(uintptr_t)rsdp;
}

int main(void) {
    chos_boot_info_t boot; chos_acpi_info_t acpi;
    make_fixture(&boot);
    assert(chos_acpi_parse(&boot,&acpi)==CHOS_ACPI_OK);
    assert(acpi.valid==1u && acpi.root_is_xsdt==1u);
    assert(acpi.gic_distributor_base==0x2f000000u && acpi.gic_version==3u);
    assert(acpi.gic_redistributor_base==0x2f100000u && acpi.gic_redistributor_length==0x20000u);
    assert(acpi.gic_cpu_interface_count==1u);
    assert(acpi.physical_timer_irq==30u && acpi.virtual_timer_irq==27u);
    assert(acpi.pci_segment_count==1u && acpi.pci_segments[0].ecam_base==0x40000000u);
    assert(acpi.pci_segments[0].end_bus==255u);

    /* Optional SPCR must be checksum-valid, PL011, SystemMemory, aligned,
     * and must not alias an EFI RAM descriptor. */
    init_sdt(at(0x800),"SPCR",80);at(0x800)[36]=3;at(0x800)[41]=32;at(0x800)[43]=3;
    put64(at(0x800)+44,0x09000000);fix_sdt(at(0x800),80);
    put32(at(0x200)+4,68);put64(at(0x200)+60,(uintptr_t)at(0x800));fix_sdt(at(0x200),68);
    assert(chos_acpi_parse(&boot,&acpi)==CHOS_ACPI_OK && acpi.pl011_base==0x09000000);
    at(0x800)[9]^=1;assert(chos_acpi_parse(&boot,&acpi)==CHOS_ACPI_OK && !acpi.pl011_base);
    make_fixture(&boot);
    init_sdt(at(0x900),"IORT",68);put32(at(0x900)+36,1);put32(at(0x900)+40,48);
    at(0x900)[48]=0;put16(at(0x900)+49,20);fix_sdt(at(0x900),68);
    put32(at(0x200)+4,68);put64(at(0x200)+60,(uintptr_t)at(0x900));fix_sdt(at(0x200),68);
    assert(chos_acpi_parse(&boot,&acpi)==CHOS_ACPI_OK && !acpi.dma_translation_unverified);
    at(0x900)[48]=4;fix_sdt(at(0x900),68);
    assert(chos_acpi_parse(&boot,&acpi)==CHOS_ACPI_OK && acpi.dma_translation_unverified);
    make_fixture(&boot);
    /* Root corruption is rejected rather than exposing table contents. */
    at(0x200)[9] ^= 1u;
    assert(chos_acpi_parse(&boot,&acpi)==CHOS_ACPI_ERR_BAD_ROOT);

    /* A recognized child table with a bad checksum must not be silently ignored. */
    make_fixture(&boot);
    at(0x300)[9] ^= 1u;
    assert(chos_acpi_parse(&boot,&acpi)==CHOS_ACPI_ERR_BAD_MADT);

    make_fixture(&boot);
    boot.acpi_rsdp_address=(uint64_t)(uintptr_t)arena+sizeof(arena)+4096u;
    assert(chos_acpi_parse(&boot,&acpi)==CHOS_ACPI_ERR_BAD_RSDP);

    make_fixture(&boot);
    /* Entry lengths must make forward progress and stay inside MADT. */
    at(0x300)[45]=1u;
    fix_sdt(at(0x300),164u);
    assert(chos_acpi_parse(&boot,&acpi)==CHOS_ACPI_ERR_BAD_MADT);
    puts("ACPI parser: RSDP/XSDT checksums, MADT GICD/GICR/GICC, GTDT timers, MCFG ECAM and malformed-table bounds passed");
    return 0;
}

/* Native UEFI identity translation profile. Data caching remains disabled until
 * DMA ownership is explicitly implemented. This adds CPU privilege isolation,
 * not IOMMU isolation or a cache-coherent DMA API. */
#include "chos/uefi_vmm.h"
#include "chos/boot_info.h"
#include "chos/acpi.h"
#include "chos/pci.h"
#include "chos/address_space.h"
#include "chos/task_stack_layout.h"
static chos_uefi_vmm_tables_t tables __attribute__((aligned(4096)));
extern uint8_t __text_start,__text_end,__rodata_start,__rodata_end;
extern uint8_t __stack_guard_start,__task_stacks_start,__task_stacks_end;
static int mapped;
static uint32_t normal_pages,device_pages;
static int ram_type(uint32_t t){return (t>=1u && t<=7u)||t==9u||t==10u||t==14u;}
static int range(uint64_t a,uint64_t n,vmm_memory_type_t type){
    if(!n || a>=CHOS_UVMM_PHYS_LIMIT || n>CHOS_UVMM_PHYS_LIMIT-a)return 0;
    uint64_t end=(a+n+4095u)&~UINT64_C(4095);a&=~UINT64_C(4095);
    return end<=CHOS_UVMM_PHYS_LIMIT && chos_uefi_vmm_map(&tables,a,end-a,type,
        type==VMM_MEMORY_DEVICE?VMM_PERM_DEVICE:VMM_PERM_KERNEL_DATA);
}
static int device(const chos_boot_info_t *b,uint64_t a,uint64_t n){
    /* Validate full page coverage too, preventing RAM/Device aliases at edges. */
    if(!n || a>=CHOS_UVMM_PHYS_LIMIT || n>CHOS_UVMM_PHYS_LIMIT-a)return 0;
    uint64_t end=(a+n+4095u)&~UINT64_C(4095),start=a&~UINT64_C(4095);
    if(!chos_boot_info_mmio_safe(b,start,end-start))return 0;
    return range(start,end-start,VMM_MEMORY_DEVICE);
}
static int protect(uint64_t a,uint64_t end,vmm_page_permissions_t p){
    if((a&4095u)||(end&4095u)||a>=end)return 0;
    for(;a<end;a+=4096u)if(!chos_uefi_vmm_protect(&tables,a,p,1))return 0;
    return 1;
}
static int retained(uint64_t a,uint64_t n){
    if(!n || a>=CHOS_UVMM_PHYS_LIMIT || n>CHOS_UVMM_PHYS_LIMIT-a)return 0;
    uint64_t end=a+n;vmm_mapping_info_t i;
    for(a&=~UINT64_C(4095);a<end;a+=4096u)
        if(!chos_uefi_vmm_query(&tables,a,&i)||i.physical_address!=a||i.memory_type!=VMM_MEMORY_NORMAL||
           !i.writable||i.executable_at_el1||i.accessible_at_el0||i.executable_at_el0)return 0;
    return 1;
}
static int supported(const chos_pci_device_t *d){
    return d->vendor_id==0x1af4 || (d->vendor_id==0x8086 && d->device_id==0x10d3) ||
      (d->vendor_id==0x15ad && d->device_id==0x07b0) ||
      (d->class_code==1 && d->subclass==8 && d->programming_interface==2) ||
      (d->class_code==12 && d->subclass==3 && d->programming_interface==48);
}
int vmm_uefi_build(const chos_boot_info_t *b,const chos_acpi_info_t *acpi,
                   const chos_pci_device_t *devices,size_t count,uint64_t virtio_mmio){
    mapped=0;normal_pages=device_pages=0;
    if(!chos_boot_info_validate(b)||!acpi||!acpi->valid||acpi->dma_translation_unverified||
       acpi->pci_segment_count>CHOS_ACPI_MAX_PCI_SEGMENTS || count>CHOS_PCI_MAX_DEVICES || (count && !devices) ||
       !chos_uefi_vmm_init(&tables,(uint64_t)(uintptr_t)&tables))return 0;
    if(b->memory_descriptor_count>1024u)return 0;
    const uint8_t *m=(const uint8_t *)(uintptr_t)b->memory_map_address;
    /* Firmware descriptors must be page aligned, bounded and disjoint. */
    for(uint64_t o=0;o<b->memory_map_size;o+=b->memory_descriptor_size){
        const chos_efi_memory_descriptor_t *d=(const void *)(m+o);
        if(!d->number_of_pages || (d->physical_start&4095u) ||
           d->physical_start>=CHOS_UVMM_PHYS_LIMIT ||
           d->number_of_pages>(CHOS_UVMM_PHYS_LIMIT-d->physical_start)/4096u)return 0;
        uint64_t end=d->physical_start+d->number_of_pages*4096u;
        for(uint64_t q=0;q<o;q+=b->memory_descriptor_size){
            const chos_efi_memory_descriptor_t *e=(const void *)(m+q);
            if(d->physical_start<e->physical_start+e->number_of_pages*4096u && e->physical_start<end)return 0;
        }
    }
    for(uint64_t off=0;off<b->memory_map_size;off+=b->memory_descriptor_size){
        const chos_efi_memory_descriptor_t *d=(const void *)(m+off);
        if(!d->number_of_pages)continue;
        if(d->number_of_pages>CHOS_UVMM_PHYS_LIMIT/4096u)return 0;
        if(ram_type(d->type) && !range(d->physical_start,d->number_of_pages*4096u,VMM_MEMORY_NORMAL))return 0;
    }
    if(!protect((uintptr_t)&__text_start,(uintptr_t)&__text_end,VMM_PERM_KERNEL_TEXT)||
       !protect((uintptr_t)&__rodata_start,(uintptr_t)&__rodata_end,VMM_PERM_KERNEL_RODATA)||
       !chos_uefi_vmm_protect(&tables,(uintptr_t)&__stack_guard_start,VMM_PERM_KERNEL_DATA,0))return 0;
    uint64_t ts=(uintptr_t)&__task_stacks_start,te=(uintptr_t)&__task_stacks_end;
    if((ts&4095u)||te<ts||te-ts!=CHOS_TASK_STACK_REGION_SIZE)return 0;
    for(;ts<te;ts+=CHOS_TASK_STACK_SLOT_SIZE)
        if(!chos_uefi_vmm_protect(&tables,ts,VMM_PERM_KERNEL_DATA,0))return 0;
    /* Mapping broad root-bridge windows could occupy the private EL0 arena.
     * Map only validated architectural windows and supported assigned BARs. */
    if(virtio_mmio && !device(b,virtio_mmio,0x4000u))return 0;
    if(!device(b,acpi->gic_distributor_base,0x10000u)||
       !device(b,acpi->gic_redistributor_base,acpi->gic_redistributor_length)||
       (acpi->pl011_base && !device(b,acpi->pl011_base,4096u)))return 0;
    for(uint32_t i=0;i<acpi->pci_segment_count;++i){
        const chos_acpi_pci_segment_t *s=&acpi->pci_segments[i];
        if(s->end_bus<s->start_bus)return 0;
        uint64_t size=((uint64_t)s->end_bus-s->start_bus+1u)<<20;
        if(!device(b,s->ecam_base,size))return 0;
    }
    for(size_t i=0;i<count;++i){
        const chos_pci_device_t *d=&devices[i];
        if((d->header_type&127u)||!supported(d))continue;
        for(unsigned bar=0;bar<6;++bar){
            uint64_t a,n;int wide;
            if(!chos_pci_decode_memory_bar(d,bar,&a,&wide))continue;
            if(chos_pci_map_bar(b,acpi,d,bar,&a,&n)){
                if(n>UINT64_C(0x40000000)||!device(b,a,n))return 0;
            }else return 0;
            if(wide)++bar;
        }
    }
    if(b->flags&CHOS_BOOT_FLAG_FRAMEBUFFER){
        if(!b->framebuffer_size || b->framebuffer_base>=CHOS_UVMM_PHYS_LIMIT ||
           b->framebuffer_size>CHOS_UVMM_PHYS_LIMIT-b->framebuffer_base)return 0;
        vmm_mapping_info_t fb;
        if(!chos_uefi_vmm_query(&tables,b->framebuffer_base,&fb)){
            if(!device(b,b->framebuffer_base,b->framebuffer_size))return 0;
        }else{
            /* RAMFB uses LoaderData. Its entire range must keep Normal attributes. */
            uint64_t a=b->framebuffer_base&~UINT64_C(4095),end=b->framebuffer_base+b->framebuffer_size;
            for(;a<end;a+=4096u)
                if(!chos_uefi_vmm_query(&tables,a,&fb)||fb.memory_type!=VMM_MEMORY_NORMAL||!fb.writable)return 0;
        }
    }
    /* Fail closed when firmware RAM/MMIO overlaps the ABI's private arena. */
    for(uint64_t a=CHOS_AS_USER_VA_BASE;a<CHOS_AS_USER_VA_LIMIT;a+=0x200000u){
        uint64_t *l2=(uint64_t *)(uintptr_t)chos_uefi_vmm_l2_address(&tables,0);
        if(l2 && l2[(a>>21)&511u])return 0;
    }
    if(!retained((uintptr_t)&tables,sizeof(tables)) || !retained((uintptr_t)b,sizeof(*b)) || !retained(b->memory_map_address,b->memory_map_size) ||
       (b->installer_payload_size && !retained(b->installer_payload_address,b->installer_payload_size)))return 0;
    for(unsigned i=0;i<tables.l2_used;++i)for(unsigned j=0;j<512u;++j){
        uint64_t d=tables.l2[i][j];
        if((d&3u)==1u){if(((d>>2)&7u)==1u)device_pages+=512u;else normal_pages+=512u;}
    }
    for(unsigned i=0;i<tables.l3_used;++i)for(unsigned j=0;j<512u;++j){
        uint64_t d=tables.l3[i][j];if((d&3u)!=3u)continue;
        if(((d>>2)&7u)==1u)++device_pages;else ++normal_pages;
    }
    mapped=1;return 1;
}
uint64_t vmm_uefi_root(void){return mapped?(uint64_t)(uintptr_t)tables.root:0;}
int vmm_uefi_copy_l2(unsigned i,uint64_t *out){
    if(!mapped||!out||i>=512)return 0;
    uint64_t pa=chos_uefi_vmm_l2_address(&tables,i);
    const uint64_t *source=(const uint64_t *)(uintptr_t)pa;
    for(unsigned j=0;j<512;++j)out[j]=source?source[j]:0;return 1;
}
int vmm_uefi_copy_l1(uint64_t *out){
    if(!mapped||!out)return 0;for(unsigned j=0;j<512;++j)out[j]=tables.l1[0][j];return 1;
}

void vmm_uefi_map_stats(uint32_t *normal,uint32_t *device,uint32_t *l3){
    *normal=normal_pages;*device=device_pages;*l3=tables.l3_used;
}

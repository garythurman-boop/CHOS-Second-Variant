#include <stddef.h>
#include <stdint.h>
#include "chos/uefi_vmm.h"
#define PAGE_MASK UINT64_C(0x0000fffffffff000)
#define BLOCK_MASK UINT64_C(0x0000ffffffe00000)
#define BLOCK_SIZE UINT64_C(0x200000)
static void zero(void *p,size_t n){uint8_t *b=p;for(size_t i=0;i<n;++i)b[i]=0;}
static uint64_t address(const chos_uefi_vmm_tables_t *t,const void *p){
    return t->physical_base+(uint64_t)((const uint8_t *)p-(const uint8_t *)t);
}
static uint64_t *table(const chos_uefi_vmm_tables_t *t,uint64_t d){
    uint64_t a=d&PAGE_MASK;
    if((d&3u)!=3u || a<t->physical_base ||
       a-t->physical_base>offsetof(chos_uefi_vmm_tables_t,physical_base)-4096u)return NULL;
    return (uint64_t *)(void *)((uint8_t *)(uintptr_t)t+(size_t)(a-t->physical_base));
}
int chos_uefi_vmm_init(chos_uefi_vmm_tables_t *t,uint64_t physical_base){
    if(!t || (physical_base&4095u) || physical_base==0 ||
       physical_base>=CHOS_UVMM_PHYS_LIMIT ||
       sizeof(*t)>CHOS_UVMM_PHYS_LIMIT-physical_base)return 0;
    zero(t,sizeof(*t));t->physical_base=physical_base;
    for(unsigned i=0;i<2;++i)t->root[i]=vmm_make_table_descriptor(address(t,t->l1[i]));
    return t->root[0]!=0 && t->root[1]!=0;
}
static uint64_t *l2_entry(chos_uefi_vmm_tables_t *t,uint64_t va,int create){
    uint64_t *entry=&t->l1[va>>39][(va>>30)&511u],*l2;
    if(!*entry && create){
        if(t->l2_used==CHOS_UVMM_L2_TABLES)return NULL;
        l2=t->l2[t->l2_used++];*entry=vmm_make_table_descriptor(address(t,l2));
    }
    l2=table(t,*entry);return l2?&l2[(va>>21)&511u]:NULL;
}
static uint64_t *split_page(chos_uefi_vmm_tables_t *t,uint64_t va,uint64_t *entry){
    uint64_t *l3;
    if((*entry&3u)==3u)return table(t,*entry);
    if(t->l3_used==CHOS_UVMM_L3_TABLES)return NULL;
    if(*entry && (*entry&3u)!=1u)return NULL;
    l3=t->l3[t->l3_used++];
    if(*entry){
        uint64_t flags=*entry&~BLOCK_MASK,base=*entry&BLOCK_MASK;
        for(unsigned i=0;i<512;++i)l3[i]=(base+(uint64_t)i*4096u)|flags|2u;
    }
    *entry=vmm_make_table_descriptor(address(t,l3));(void)va;return l3;
}
int chos_uefi_vmm_map(chos_uefi_vmm_tables_t *t,uint64_t base,uint64_t size,
                     vmm_memory_type_t type,vmm_page_permissions_t perm){
    if(!t || !t->physical_base || !size || (base&4095u) || (size&4095u) ||
       base>=CHOS_UVMM_PHYS_LIMIT || size>CHOS_UVMM_PHYS_LIMIT-base ||
       !vmm_make_page_descriptor(base,type,perm))return 0;
    uint64_t end=base+size;
    while(base<end){
        uint64_t *e=l2_entry(t,base,1),d=vmm_make_page_descriptor(base,type,perm);
        if(!e)return 0;
        if(!(base&(BLOCK_SIZE-1)) && end-base>=BLOCK_SIZE && (!*e || *e==(d&~UINT64_C(2)))){
            *e=d&~UINT64_C(2);base+=BLOCK_SIZE;continue;
        }
        uint64_t *l3=split_page(t,base,e);
        if(!l3)return 0;
        uint64_t *p=&l3[(base>>12)&511u];
        if(*p && *p!=d)return 0;
        *p=d;base+=4096u;
    }
    return 1;
}
int chos_uefi_vmm_protect(chos_uefi_vmm_tables_t *t,uint64_t page,
                         vmm_page_permissions_t perm,int present){
    if(!t || page>=CHOS_UVMM_PHYS_LIMIT || (page&4095u) ||
       perm==VMM_PERM_DEVICE || (unsigned)perm>VMM_PERM_KERNEL_DATA)return 0;
    uint64_t *e=l2_entry(t,page,0);if(!e || !*e)return 0;
    if((*e&3u)==1u && (((*e>>2)&7u)!=0 || (*e&BLOCK_MASK)!=(page&~(BLOCK_SIZE-1))))return 0;
    uint64_t *l3=split_page(t,page,e);if(!l3)return 0;
    uint64_t *p=&l3[(page>>12)&511u];
    if((*p&3u)!=3u || ((*p>>2)&7u)!=0 || (*p&PAGE_MASK)!=page)return 0;
    *p=present?vmm_make_page_descriptor(page,VMM_MEMORY_NORMAL,perm):0;return 1;
}
int chos_uefi_vmm_query(const chos_uefi_vmm_tables_t *t,uint64_t va,vmm_mapping_info_t *out){
    if(!t || !out || va>=CHOS_UVMM_PHYS_LIMIT)return 0;
    uint64_t *l2=table(t,t->l1[va>>39][(va>>30)&511u]);if(!l2)return 0;
    uint64_t d=l2[(va>>21)&511u],pa;
    if((d&3u)==1u)pa=(d&BLOCK_MASK)|(va&(BLOCK_SIZE-1));
    else if((d&3u)==3u){uint64_t *l3=table(t,d);if(!l3)return 0;
        d=l3[(va>>12)&511u];if((d&3u)!=3u)return 0;pa=(d&PAGE_MASK)|(va&4095u);
    }else return 0;
    uint64_t ap=(d>>6)&3u;
    out->physical_address=pa;out->memory_type=((d>>2)&7u)==1?VMM_MEMORY_DEVICE:VMM_MEMORY_NORMAL;
    out->writable=ap==0 || ap==1;out->accessible_at_el0=ap==1 || ap==3;
    out->executable_at_el1=!(d&(UINT64_C(1)<<53));out->executable_at_el0=!(d&(UINT64_C(1)<<54));return 1;
}
uint64_t chos_uefi_vmm_l1_address(const chos_uefi_vmm_tables_t *t,unsigned index){
    return t && index<2?address(t,t->l1[index]):0;
}
uint64_t chos_uefi_vmm_l2_address(const chos_uefi_vmm_tables_t *t,unsigned index){
    if(!t || index>=512)return 0;
    return t->l1[0][index]&PAGE_MASK;
}

#include "chos/elf_image.h"
static uint16_t le16(const uint8_t *p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint64_t le64(const uint8_t *p){return le32(p)|((uint64_t)le32(p+4)<<32);}
static int range(uint64_t start,uint64_t size,uint64_t limit){return start<=limit && size<=limit-start;}
int chos_elf_validate(const void *data,size_t size,chos_elf_image_t *out){
    const uint8_t *p=data;
    chos_elf_image_t plan={0};
    if(out)*out=plan;
    if(!p || !out || size<64 || size>CHOS_ELF_MAX_FILE)return 0;
    if(p[0]!=0x7f || p[1]!='E' || p[2]!='L' || p[3]!='F' ||
       p[4]!=2 || p[5]!=1 || p[6]!=1 || p[7]!=0 || p[8]!=0)return 0;
    for(unsigned i=9;i<16;i++)if(p[i])return 0;
    if(le16(p+16)!=2 || le16(p+18)!=183 || le32(p+20)!=1 ||
       le32(p+48)!=0 || le16(p+52)!=64 || le16(p+54)!=56)return 0;
    uint64_t entry=le64(p+24),table=le64(p+32);
    unsigned count=le16(p+56);
    if(!count || count>CHOS_ELF_MAX_SEGMENTS || table<64 ||
       !range(table,(uint64_t)count*56,size) || (entry&3u))return 0;
    int have_entry=0,have_abi=0;
    for(unsigned n=0;n<count;n++){
        const uint8_t *h=p+(size_t)table+n*56u;
        uint32_t type=le32(h),flags=le32(h+4);
        uint64_t offset=le64(h+8),va=le64(h+16),files=le64(h+32),memory=le64(h+40),align=le64(h+48);
        if(!range(offset,files,size))return 0;
        if(type==0)continue;
        if(type==0x6474e551u){if(flags&1u)return 0;continue;} /* non-executable GNU_STACK */
        if(type==4){
            if(files>4096)return 0;
            uint64_t pos=0;
            while(pos<files){
                if(!range(pos,12,files))return 0;
                const uint8_t *note=p+(size_t)offset+(size_t)pos;
                uint32_t names=le32(note),desc=le32(note+4);
                uint64_t name_span=((uint64_t)names+3u)&~UINT64_C(3);
                uint64_t desc_span=((uint64_t)desc+3u)&~UINT64_C(3);
                if(!range(pos+12,name_span,files) || !range(pos+12+name_span,desc_span,files))return 0;
                if(le32(note+8)==0x43484f53u && names==5 && note[12]=='C' && note[13]=='H' &&
                   note[14]=='O' && note[15]=='S' && note[16]==0){
                    if(have_abi || desc!=4 || le32(note+12+(size_t)name_span)!=1)return 0;
                    have_abi=1;
                }
                pos+=12+name_span+desc_span;
            }
            continue;
        }
        if(type==6 || type==0x6474e550u)continue; /* PHDR/EH_FRAME metadata */
        if(type!=1)return 0; /* DYNAMIC, INTERP, TLS and unsupported RELRO fail closed. */
        if((flags&~7u) || !(flags&4u) || (flags&3u)==3u || files>memory)return 0;
        if(!memory){if(files)return 0;continue;}
        if(va<CHOS_AS_USER_VA_BASE || !range(va-CHOS_AS_USER_VA_BASE,memory,CHOS_ELF_MAX_SPAN) ||
           (va&4095u) || (offset&4095u) || align<4096 || align>CHOS_ELF_MAX_SPAN ||
           (align&(align-1u)) || (va&(align-1u))!=(offset&(align-1u)))return 0;
        uint32_t relative=(uint32_t)(va-CHOS_AS_USER_VA_BASE);
        uint32_t end=(relative+(uint32_t)memory+4095u)&~4095u;
        for(unsigned i=0;i<plan.segment_count;i++){
            const chos_elf_segment_t *s=&plan.segments[i];
            uint32_t high=(s->virtual_offset+s->memory_size+4095u)&~4095u;
            if(relative<high && end>s->virtual_offset)return 0;
            if(files && s->file_size && offset<(uint64_t)s->offset+s->file_size && offset+files>s->offset)return 0;
        }
        if((flags&1u) && entry>=va && range(entry-va,4,files))have_entry=1;
        chos_elf_segment_t *s=&plan.segments[plan.segment_count++];
        *s=(chos_elf_segment_t){(uint32_t)offset,relative,(uint32_t)files,(uint32_t)memory,
             (flags&1u)?AS_PERM_USER_RX:(flags&2u)?AS_PERM_USER_RW:AS_PERM_USER_RO};
        if(end>plan.virtual_size)plan.virtual_size=end;
    }
    if(!have_entry || !have_abi || !plan.segment_count)return 0;
    plan.entry_offset=(uint32_t)(entry-CHOS_AS_USER_VA_BASE);
    *out=plan;return 1;
}
int chos_elf_page(const chos_elf_image_t *plan,uint32_t offset,chos_elf_page_t *out){
    if(!plan || !out || (offset&4095u) || offset>=plan->virtual_size ||
       plan->segment_count>CHOS_ELF_MAX_SEGMENTS)return 0;
    for(unsigned i=0;i<plan->segment_count;i++){
        const chos_elf_segment_t *s=&plan->segments[i];
        if(offset<s->virtual_offset || offset-s->virtual_offset>=s->memory_size)continue;
        uint32_t delta=offset-s->virtual_offset;
        out->permissions=s->permissions;out->file_offset=0;out->file_bytes=0;
        if(delta<s->file_size){out->file_offset=s->offset+delta;out->file_bytes=s->file_size-delta;if(out->file_bytes>4096)out->file_bytes=4096;}
        return 1;
    }
    return 0; /* gaps stay unmapped, not implicit writable/executable pages */
}

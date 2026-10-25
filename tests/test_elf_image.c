#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "chos/elf_image.h"
static uint8_t bytes[16384];
static void w16(uint8_t *p,unsigned v){p[0]=v;p[1]=v>>8;}
static void w32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=v>>(i*8);}
static void w64(uint8_t *p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=v>>(i*8);}
static void segment(unsigned i,uint32_t flags,unsigned off,uint64_t va,unsigned file,unsigned memory){
    uint8_t *h=bytes+64+i*56;w32(h,1);w32(h+4,flags);w64(h+8,off);w64(h+16,va);
    w64(h+32,file);w64(h+40,memory);w64(h+48,4096);
}
static void valid(void){
    memset(bytes,0,sizeof(bytes));memcpy(bytes,"\177ELF\2\1\1",7);
    w16(bytes+16,2);w16(bytes+18,183);w32(bytes+20,1);w64(bytes+24,CHOS_AS_USER_VA_BASE);
    w64(bytes+32,64);w16(bytes+52,64);w16(bytes+54,56);w16(bytes+56,4);
    segment(0,5,4096,CHOS_AS_USER_VA_BASE,100,100);
    segment(1,4,8192,CHOS_AS_USER_VA_BASE+0x10000,17,17);
    segment(2,6,12288,CHOS_AS_USER_VA_BASE+0x80000,7,0x41001);
    uint8_t *h=bytes+64+3*56;w32(h,4);w64(h+8,512);w64(h+32,24);
    w32(bytes+512,5);w32(bytes+516,4);w32(bytes+520,0x43484f53);memcpy(bytes+524,"CHOS",5);w32(bytes+532,1);
}
int main(void){
    chos_elf_image_t plan;chos_elf_page_t page;
    valid();assert(chos_elf_validate(bytes,sizeof(bytes),&plan));assert(plan.virtual_size==0xc2000 && plan.segment_count==3);
    assert(chos_elf_page(&plan,0,&page)&&page.permissions==AS_PERM_USER_RX&&page.file_bytes==100&&page.file_offset==4096);
    assert(chos_elf_page(&plan,0x10000,&page)&&page.permissions==AS_PERM_USER_RO&&page.file_bytes==17);
    assert(chos_elf_page(&plan,0x80000,&page)&&page.permissions==AS_PERM_USER_RW&&page.file_bytes==7);
    assert(chos_elf_page(&plan,0x81000,&page)&&page.file_bytes==0&&!chos_elf_page(&plan,0x30000,&page));
    assert(!chos_elf_page(&plan,1,&page));
    for(size_t n=0;n<64;n++)assert(!chos_elf_validate(bytes,n,&plan));
    valid();w64(bytes+32,UINT64_MAX);assert(!chos_elf_validate(bytes,sizeof(bytes),&plan));
    valid();w16(bytes+56,9);assert(!chos_elf_validate(bytes,sizeof(bytes),&plan));
    valid();w16(bytes+56,3);assert(!chos_elf_validate(bytes,sizeof(bytes),&plan)); /* no CHOS ABI */
    valid();w32(bytes+532,2);assert(!chos_elf_validate(bytes,sizeof(bytes),&plan));
    valid();w32(bytes+512,UINT32_MAX);assert(!chos_elf_validate(bytes,sizeof(bytes),&plan));
    valid();w16(bytes+16,3);assert(!chos_elf_validate(bytes,sizeof(bytes),&plan));
    valid();w16(bytes+18,62);assert(!chos_elf_validate(bytes,sizeof(bytes),&plan));
    valid();bytes[5]=2;assert(!chos_elf_validate(bytes,sizeof(bytes),&plan));
    valid();w32(bytes+68,7);assert(!chos_elf_validate(bytes,sizeof(bytes),&plan));
    valid();w64(bytes+24,CHOS_AS_USER_VA_BASE+0x10000);assert(!chos_elf_validate(bytes,sizeof(bytes),&plan));
    valid();w64(bytes+24,CHOS_AS_USER_VA_BASE+100);assert(!chos_elf_validate(bytes,sizeof(bytes),&plan));
    valid();segment(1,4,8192,CHOS_AS_USER_VA_BASE,17,17);assert(!chos_elf_validate(bytes,sizeof(bytes),&plan));
    valid();segment(1,4,4096,CHOS_AS_USER_VA_BASE+0x10000,17,17);assert(!chos_elf_validate(bytes,sizeof(bytes),&plan));
    valid();segment(1,4,8192,CHOS_AS_USER_VA_BASE+CHOS_ELF_MAX_SPAN,17,17);assert(!chos_elf_validate(bytes,sizeof(bytes),&plan));
    valid();segment(1,4,8193,CHOS_AS_USER_VA_BASE+0x10000,17,17);assert(!chos_elf_validate(bytes,sizeof(bytes),&plan));
    valid();segment(1,4,8192,CHOS_AS_USER_VA_BASE+0x10000,18,17);assert(!chos_elf_validate(bytes,sizeof(bytes),&plan));
    valid();w64(bytes+64+48,8191);assert(!chos_elf_validate(bytes,sizeof(bytes),&plan));
    for(unsigned type=2;type<=7;type++)if(type!=4&&type!=6){valid();w32(bytes+64,type);assert(!chos_elf_validate(bytes,sizeof(bytes),&plan));}
    valid();w32(bytes+64,0x6474e551);w32(bytes+68,7);assert(!chos_elf_validate(bytes,sizeof(bytes),&plan));
    for(unsigned i=0;i<sizeof(bytes);i++){
        valid();bytes[i]^=(uint8_t)(i*13u+1u);(void)chos_elf_validate(bytes,sizeof(bytes),&plan);
    }
    puts("native ELF: static AArch64 format, bounded segments, RX entry, W^X, sparse pages and malformed ranges passed");
}

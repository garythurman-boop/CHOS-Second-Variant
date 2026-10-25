#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/mm/user_memory.c"
static struct {uint64_t pa;int used,mapped;} frames[4][1024];
static uint64_t mappings[4][512];
static unsigned generations[4]={1,1,1,1},allocations,allocation_fail,map_calls,map_fail,unmap_fail,free_fail;
static unsigned index_of(uint64_t va){assert(va>=CHOS_USER_MAP_BASE&&va<CHOS_USER_MAP_BASE+CHOS_USER_MAP_BYTES);return (unsigned)((va-CHOS_USER_MAP_BASE)/4096);}
uint64_t chos_spin_lock_irqsave(chos_spinlock_t *p){assert(!p->held);p->held=1;return 0;}
void chos_spin_unlock_irqrestore(chos_spinlock_t *p,uint64_t state){(void)state;assert(p->held);p->held=0;}
int address_space_get_info(address_space_handle_t h,address_space_info_t *out){if(h.slot>=4||h.generation!=generations[h.slot]||h.id!=h.slot+1)return 0;*out=(address_space_info_t){.live=1};return 1;}
int address_space_query(address_space_handle_t h,uint64_t va,vmm_mapping_info_t *out){uint64_t pa=mappings[h.slot][index_of(va)];if(!pa)return 0;memset(out,0,sizeof(*out));return 1;}
address_space_error_t address_space_alloc_user_page(address_space_handle_t h,uint64_t *pa){if(++allocations==allocation_fail)return AS_ERR_OUT_OF_MEMORY;for(unsigned i=0;i<1024;i++)if(!frames[h.slot][i].used){frames[h.slot][i].used=1;*pa=frames[h.slot][i].pa=0x50000000u+h.slot*0x400000u+i*4096;return AS_OK;}return AS_ERR_FRAME_CAPACITY;}
address_space_error_t address_space_map_user_page(address_space_handle_t h,uint64_t va,uint64_t pa,address_space_user_permissions_t perm){assert(perm==AS_PERM_USER_RW);if(++map_calls==map_fail)return AS_ERR_OUT_OF_MEMORY;unsigned i=(unsigned)((pa-0x50000000u-h.slot*0x400000u)/4096);assert(frames[h.slot][i].used&&!frames[h.slot][i].mapped);frames[h.slot][i].mapped=1;mappings[h.slot][index_of(va)]=pa;return AS_OK;}
address_space_error_t address_space_unmap_user_page(address_space_handle_t h,uint64_t va,uint64_t *pa){if(unmap_fail)return AS_ERR_BAD_DESCRIPTOR;uint64_t p=mappings[h.slot][index_of(va)];assert(p);if(pa)*pa=p;frames[h.slot][(p-0x50000000u-h.slot*0x400000u)/4096].mapped=0;mappings[h.slot][index_of(va)]=0;return AS_OK;}
address_space_error_t address_space_free_user_page(address_space_handle_t h,uint64_t pa){unsigned i=(unsigned)((pa-0x50000000u-h.slot*0x400000u)/4096);assert(frames[h.slot][i].used&&!frames[h.slot][i].mapped);if(free_fail)return AS_ERR_BAD_DESCRIPTOR;frames[h.slot][i].used=0;return AS_OK;}
static unsigned used(unsigned slot){unsigned n=0;for(unsigned i=0;i<1024;i++)n+=frames[slot][i].used;return n;}
static void reset(void){memset(owners,0,sizeof(owners));memset(frames,0,sizeof(frames));memset(mappings,0,sizeof(mappings));allocations=allocation_fail=map_calls=map_fail=unmap_fail=free_fail=0;}
int main(void){
 address_space_handle_t a={.slot=0,.id=1,.generation=1},b={.slot=1,.id=2,.generation=1};reset();
 assert(chos_user_memory_map(a,0,0)==-CHOS_ERRNO_EINVAL);assert(chos_user_memory_map(a,(size_t)-1,0)==-CHOS_ERRNO_EINVAL);assert(chos_user_memory_map(a,4096,1)==-CHOS_ERRNO_EINVAL);
 int64_t va=chos_user_memory_map(a,8192,0);assert(va>0&&used(0)==2);
 assert(chos_user_memory_unmap(b,(uint64_t)va,8192)==-CHOS_ERRNO_EINVAL);
 assert(chos_user_memory_unmap(a,(uint64_t)va,4096)==-CHOS_ERRNO_EINVAL);
 assert(chos_user_memory_unmap(a,CHOS_AS_USER_VA_BASE,4096)==-CHOS_ERRNO_EINVAL);
 assert(chos_user_memory_unmap(a,(uint64_t)va,8192)==0&&!used(0));assert(chos_user_memory_unmap(a,(uint64_t)va,8192)==-CHOS_ERRNO_EINVAL);
 reset();allocation_fail=2;assert(chos_user_memory_map(a,8192,0)==-CHOS_ERRNO_ENOMEM&&!used(0));
 reset();map_fail=2;assert(chos_user_memory_map(a,8192,0)==-CHOS_ERRNO_ENOMEM&&!used(0));
 reset();va=chos_user_memory_map(a,8192,0);unmap_fail=1;assert(chos_user_memory_unmap(a,(uint64_t)va,8192)==-CHOS_ERRNO_EIO&&used(0)==2);
 assert(chos_user_memory_unmap(a,(uint64_t)va,8192)==-CHOS_ERRNO_EINVAL);unmap_fail=0;assert(chos_user_memory_release(a));
 for(unsigned i=0;i<512;i++)assert(!mappings[0][i]);
 assert(used(0)==2); /* frames stay owned until AS destroy */
 reset();va=chos_user_memory_map(a,4096,0);free_fail=1;
 assert(chos_user_memory_unmap(a,(uint64_t)va,4096)==-CHOS_ERRNO_EIO&&used(0)==1);
 assert(chos_user_memory_release(a)&&used(0)==1); /* deferred AS owner still retains frame */
 reset();mappings[0][1]=0x50000000;assert(chos_user_memory_map(a,4096,0)==-CHOS_ERRNO_EBUSY&&!allocations);
 reset();for(unsigned i=0;i<16;i++)assert(chos_user_memory_map(a,4096,0)>0);assert(chos_user_memory_map(a,4096,0)==-CHOS_ERRNO_ENOMEM);assert(chos_user_memory_release(a));
 reset();assert(chos_user_memory_map(a,4096,0)>0);assert(chos_user_memory_map(b,4096,0)>0);assert(used(0)==1&&used(1)==1);
 a.generation=2;assert(chos_user_memory_map(a,4096,0)==-CHOS_ERRNO_ESRCH);
 puts("anonymous user mappings: bounds, owner separation, rollback, quota, non-executable pages and quarantine passed");
}

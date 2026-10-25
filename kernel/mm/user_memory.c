#include "chos/user_memory.h"
#include "chos/syscall.h"
#include "chos/spinlock.h"
typedef struct {uint64_t first;uint32_t pages,state;} region_t;
typedef struct {uint32_t generation,id;region_t regions[CHOS_USER_MAP_REGIONS];} map_owner_t;
static map_owner_t owners[CHOS_AS_MAX_SPACES];
static chos_spinlock_t memory_lock=CHOS_SPINLOCK_INITIALIZER;
static map_owner_t *owner(address_space_handle_t h){
 address_space_info_t info;
 if(h.slot>=CHOS_AS_MAX_SPACES || !address_space_get_info(h,&info))return NULL;
 map_owner_t *o=&owners[h.slot];
 if(o->generation!=h.generation||o->id!=h.id){
  *o=(map_owner_t){.generation=h.generation,.id=h.id};
 }
 return o;
}
static int length_pages(size_t n,uint32_t *pages){
 if(!n||n>CHOS_USER_MAP_MAX_BYTES)return 0;
 *pages=(uint32_t)((n+VMM_PAGE_SIZE-1u)/VMM_PAGE_SIZE);return 1;
}
static int remove_pages(address_space_handle_t h,region_t *r,int free_frames){
 int ok=1;
 for(uint32_t p=0;p<r->pages;p++){
  uint64_t va=r->first+(uint64_t)p*VMM_PAGE_SIZE,physical;vmm_mapping_info_t mapping;
  if(!address_space_query(h,va,&mapping))continue;
  if(address_space_unmap_user_page(h,va,&physical)!=AS_OK){ok=0;continue;}
  if(free_frames && address_space_free_user_page(h,physical)!=AS_OK)ok=0;
 }
 if(ok)*r=(region_t){0};else r->state=2; /* quarantine partial failures until reaping */
 return ok;
}
int64_t chos_user_memory_map(address_space_handle_t h,size_t n,unsigned flags){
 uint32_t pages;if(flags||!length_pages(n,&pages))return -CHOS_ERRNO_EINVAL;
 uint64_t irq=chos_spin_lock_irqsave(&memory_lock);map_owner_t *o=owner(h);
 if(!o){chos_spin_unlock_irqrestore(&memory_lock,irq);return -CHOS_ERRNO_ESRCH;}
 unsigned slot;for(slot=0;slot<CHOS_USER_MAP_REGIONS && o->regions[slot].state;slot++){}
 if(slot==CHOS_USER_MAP_REGIONS){chos_spin_unlock_irqrestore(&memory_lock,irq);return -CHOS_ERRNO_ENOMEM;}
 uint64_t candidate=CHOS_USER_MAP_BASE+VMM_PAGE_SIZE,end=CHOS_USER_MAP_BASE+CHOS_USER_MAP_BYTES;
 for(;;){
  if(candidate+(uint64_t)(pages+1u)*VMM_PAGE_SIZE>end){chos_spin_unlock_irqrestore(&memory_lock,irq);return -CHOS_ERRNO_ENOMEM;}
  int overlap=0;
  for(unsigned i=0;i<CHOS_USER_MAP_REGIONS;i++){
   const region_t *r=&o->regions[i];if(!r->state)continue;
   uint64_t high=r->first+(uint64_t)(r->pages+1u)*VMM_PAGE_SIZE;
   if(candidate-VMM_PAGE_SIZE<high && candidate+(uint64_t)(pages+1u)*VMM_PAGE_SIZE>r->first-VMM_PAGE_SIZE){candidate=high+VMM_PAGE_SIZE;overlap=1;break;}
  }
  if(!overlap)break;
 }
 /* Refuse unexpected external mappings rather than overwrite code/device pages. */
 for(uint32_t p=0;p<pages;p++){vmm_mapping_info_t m;if(address_space_query(h,candidate+(uint64_t)p*VMM_PAGE_SIZE,&m)){chos_spin_unlock_irqrestore(&memory_lock,irq);return -CHOS_ERRNO_EBUSY;}}
 region_t *r=&o->regions[slot];*r=(region_t){.first=candidate,.pages=pages,.state=1};
 for(uint32_t p=0;p<pages;p++){
  uint64_t physical;
  if(address_space_alloc_user_page(h,&physical)!=AS_OK){(void)remove_pages(h,r,1);chos_spin_unlock_irqrestore(&memory_lock,irq);return -CHOS_ERRNO_ENOMEM;}
  if(address_space_map_user_page(h,candidate+(uint64_t)p*VMM_PAGE_SIZE,physical,AS_PERM_USER_RW)!=AS_OK){
   (void)address_space_free_user_page(h,physical);(void)remove_pages(h,r,1);chos_spin_unlock_irqrestore(&memory_lock,irq);return -CHOS_ERRNO_ENOMEM;
  }
 }
 chos_spin_unlock_irqrestore(&memory_lock,irq);return (int64_t)candidate;
}
int chos_user_memory_unmap(address_space_handle_t h,uint64_t address,size_t n){
 uint32_t pages;if((address&(VMM_PAGE_SIZE-1u))||!length_pages(n,&pages))return -CHOS_ERRNO_EINVAL;
 uint64_t irq=chos_spin_lock_irqsave(&memory_lock);map_owner_t *o=owner(h);
 if(!o){chos_spin_unlock_irqrestore(&memory_lock,irq);return -CHOS_ERRNO_ESRCH;}
 for(unsigned i=0;i<CHOS_USER_MAP_REGIONS;i++){
  region_t *r=&o->regions[i];
  if(r->state==1 && r->first==address && r->pages==pages){
   for(uint32_t p=0;p<pages;p++){vmm_mapping_info_t m;if(!address_space_query(h,address+(uint64_t)p*VMM_PAGE_SIZE,&m)){r->state=2;chos_spin_unlock_irqrestore(&memory_lock,irq);return -CHOS_ERRNO_EIO;}}
   int ok=remove_pages(h,r,1);chos_spin_unlock_irqrestore(&memory_lock,irq);return ok?0:-CHOS_ERRNO_EIO;
  }
 }
 chos_spin_unlock_irqrestore(&memory_lock,irq);return -CHOS_ERRNO_EINVAL;
}
int chos_user_memory_release(address_space_handle_t h){
 uint64_t irq=chos_spin_lock_irqsave(&memory_lock);map_owner_t *o=owner(h);int ok=1;
 if(o)for(unsigned i=0;i<CHOS_USER_MAP_REGIONS;i++)if(o->regions[i].state && !remove_pages(h,&o->regions[i],0))ok=0;
 chos_spin_unlock_irqrestore(&memory_lock,irq);return ok;
}

#include "chos/user_memory.h"
#include "chos/syscall.h"
#include "chos/spinlock.h"
_Static_assert(CHOS_USER_MAP_MAX_BYTES+2u*VMM_PAGE_SIZE<=CHOS_USER_MAP_BYTES,"mapping cap must retain both guards");
typedef struct {uint64_t first;uint32_t pages,state,pins;} region_t;
typedef struct {uint32_t generation,id,region_limit,releasing,release_busy;
 uint64_t map_base,map_end;region_t regions[CHOS_RESOURCE_ENGINE_REGIONS];} map_owner_t;
static map_owner_t owners[CHOS_AS_MAX_SPACES];
static chos_spinlock_t memory_lock=CHOS_SPINLOCK_INITIALIZER;
static map_owner_t *owner_locked(address_space_handle_t h,int releasing){
 address_space_info_t info;
 if(h.slot>=CHOS_AS_MAX_SPACES || !address_space_get_info(h,&info))return NULL;
 map_owner_t *o=&owners[h.slot];
 if(o->generation!=h.generation||o->id!=h.id){
  chos_execution_profile_t profile;
  if(!chos_execution_profile_get(info.resource_profile,&profile))return NULL;
  *o=(map_owner_t){.generation=h.generation,.id=h.id,.region_limit=profile.map_regions,
   .map_base=profile.map_base,.map_end=profile.map_base+profile.map_bytes};
 }
 if(o->releasing&&!releasing)return NULL;
 return o;
}
static map_owner_t *owner(address_space_handle_t h){return owner_locked(h,0);}
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
 unsigned slot;for(slot=0;slot<o->region_limit && o->regions[slot].state;slot++){}
 if(slot==o->region_limit){chos_spin_unlock_irqrestore(&memory_lock,irq);return -CHOS_ERRNO_ENOMEM;}
 uint64_t candidate=o->map_base+VMM_PAGE_SIZE,end=o->map_end;
 for(;;){
  if(candidate+(uint64_t)(pages+1u)*VMM_PAGE_SIZE>end){chos_spin_unlock_irqrestore(&memory_lock,irq);return -CHOS_ERRNO_ENOMEM;}
  int overlap=0;
  for(unsigned i=0;i<o->region_limit;i++){
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
 for(unsigned i=0;i<o->region_limit;i++){
  region_t *r=&o->regions[i];
  if(r->state==1 && r->first==address && r->pages==pages){
   if(r->pins){chos_spin_unlock_irqrestore(&memory_lock,irq);return -CHOS_ERRNO_EBUSY;}
   for(uint32_t p=0;p<pages;p++){vmm_mapping_info_t m;if(!address_space_query(h,address+(uint64_t)p*VMM_PAGE_SIZE,&m)){r->state=2;chos_spin_unlock_irqrestore(&memory_lock,irq);return -CHOS_ERRNO_EIO;}}
   int ok=remove_pages(h,r,1);chos_spin_unlock_irqrestore(&memory_lock,irq);return ok?0:-CHOS_ERRNO_EIO;
  }
 }
 chos_spin_unlock_irqrestore(&memory_lock,irq);return -CHOS_ERRNO_EINVAL;
}
int chos_user_memory_release(address_space_handle_t h){
 uint64_t irq=chos_spin_lock_irqsave(&memory_lock);
 map_owner_t *o=owner_locked(h,1);address_space_info_t info;
 if(!o||o->release_busy||!address_space_get_info(h,&info)||info.task_references){
  chos_spin_unlock_irqrestore(&memory_lock,irq);return 0;
 }
 for(unsigned i=0;i<o->region_limit;i++)if(o->regions[i].state&&o->regions[i].pins){
  chos_spin_unlock_irqrestore(&memory_lock,irq);return 0;
 }
 /* Pin the quiescent AS across the unlocked unmap loop. Its slot/generation
  * cannot be destroyed/reused while region metadata is being disposed. */
 if(address_space_task_retain(h)!=AS_OK){chos_spin_unlock_irqrestore(&memory_lock,irq);return 0;}
 o->releasing=o->release_busy=1;
 unsigned limit=o->region_limit;
 chos_spin_unlock_irqrestore(&memory_lock,irq);
 int ok=1;
 for(unsigned i=0;i<limit;i++)if(o->regions[i].state&&!remove_pages(h,&o->regions[i],0))ok=0;
 irq=chos_spin_lock_irqsave(&memory_lock);
 o->release_busy=0;
 /* Releasing stays set: ordinary operations cannot reopen this terminal owner.
  * A failed unmap may be retried by the reaper, with the same owned AS handle. */
 chos_spin_unlock_irqrestore(&memory_lock,irq);
 if(address_space_task_release(h)!=AS_OK)return 0;
 return ok;
}

static int pin_change(address_space_handle_t h,uint64_t base,size_t bytes,int add){
 if(!bytes||(bytes&4095u)||(base&4095u))return 0;
 uint64_t irq=chos_spin_lock_irqsave(&memory_lock);map_owner_t *o=owner(h);int ok=0;
 if(o)for(unsigned i=0;i<o->region_limit;i++){
  region_t *r=&o->regions[i];
  if(r->state==1&&r->first==base&&(uint64_t)r->pages*4096u==bytes){
   if(add&&!r->pins){r->pins=1;ok=1;}
   if(!add&&r->pins){r->pins=0;ok=1;}break;
  }
 }
 chos_spin_unlock_irqrestore(&memory_lock,irq);return ok;
}
int chos_user_memory_pin(address_space_handle_t h,uint64_t base,size_t bytes){return pin_change(h,base,bytes,1);}
int chos_user_memory_unpin(address_space_handle_t h,uint64_t base,size_t bytes){return pin_change(h,base,bytes,0);}

int chos_user_memory_retire_stack(address_space_handle_t h,uint64_t base,size_t bytes){
 uint32_t pages;
 if((base&4095u)||(bytes&4095u)||!length_pages(bytes,&pages))return -CHOS_ERRNO_EINVAL;
 uint64_t irq=chos_spin_lock_irqsave(&memory_lock);map_owner_t *o=owner(h);
 if(!o){chos_spin_unlock_irqrestore(&memory_lock,irq);return -CHOS_ERRNO_ESRCH;}
 for(unsigned i=0;i<o->region_limit;i++){
  region_t *r=&o->regions[i];
  if(r->state!=1||r->first!=base||r->pages!=pages||r->pins!=1)continue;
  /* The pin has never been relinquished. No other thread can unmap/remap this
   * address between ownership validation and disposal (including an ABA). */
  r->pins=0;
  for(uint32_t p=0;p<pages;p++){
   vmm_mapping_info_t m;
   if(!address_space_query(h,base+(uint64_t)p*VMM_PAGE_SIZE,&m)){
    r->state=2;chos_spin_unlock_irqrestore(&memory_lock,irq);return -CHOS_ERRNO_EIO;
   }
  }
  int ok=remove_pages(h,r,1);chos_spin_unlock_irqrestore(&memory_lock,irq);
  return ok?0:-CHOS_ERRNO_EIO;
 }
 chos_spin_unlock_irqrestore(&memory_lock,irq);return -CHOS_ERRNO_EINVAL;
}

static region_t *containing_region(map_owner_t *o,uint64_t address,uint32_t pages){
 if(!o || address>UINT64_MAX-(uint64_t)pages*VMM_PAGE_SIZE)return NULL;
 uint64_t end=address+(uint64_t)pages*VMM_PAGE_SIZE;
 for(unsigned i=0;i<o->region_limit;i++){
  region_t *r=&o->regions[i];
  if(r->state==1 && address>=r->first && end<=r->first+(uint64_t)r->pages*VMM_PAGE_SIZE)return r;
 }
 return NULL;
}
int chos_user_memory_control(address_space_handle_t h,uint64_t address,size_t bytes,unsigned operation){
 uint32_t pages;
 if((address&(VMM_PAGE_SIZE-1u))||!length_pages(bytes,&pages))return -CHOS_ERRNO_EINVAL;
 if(operation!=CHOS_MEMORY_PROTECT_NONE && operation!=CHOS_MEMORY_PROTECT_READ &&
    operation!=CHOS_MEMORY_PROTECT_RW && operation!=CHOS_MEMORY_DISCARD && operation!=CHOS_MEMORY_VALIDATE)return -CHOS_ERRNO_EINVAL;
 uint64_t irq=chos_spin_lock_irqsave(&memory_lock);map_owner_t *o=owner(h);
 if(!o){chos_spin_unlock_irqrestore(&memory_lock,irq);return -CHOS_ERRNO_ESRCH;}
 region_t *r=containing_region(o,address,pages);
 if(!r){chos_spin_unlock_irqrestore(&memory_lock,irq);return -CHOS_ERRNO_EINVAL;}
 if(r->pins){chos_spin_unlock_irqrestore(&memory_lock,irq);return -CHOS_ERRNO_EBUSY;}
 address_space_error_t error=AS_OK;
 if(operation==CHOS_MEMORY_VALIDATE){
  for(uint32_t p=0;p<pages;p++){
   vmm_mapping_info_t m;
   if(!address_space_query(h,address+(uint64_t)p*VMM_PAGE_SIZE,&m)||
      m.memory_type!=VMM_MEMORY_NORMAL||m.executable_at_el0||m.executable_at_el1){error=AS_ERR_BAD_DESCRIPTOR;break;}
  }
 }else if(operation==CHOS_MEMORY_DISCARD)error=address_space_discard_user_range(h,address,(size_t)pages*VMM_PAGE_SIZE);
 else error=address_space_protect_user_range(h,address,(size_t)pages*VMM_PAGE_SIZE,
     operation==CHOS_MEMORY_PROTECT_NONE?AS_PERM_USER_NONE:
     operation==CHOS_MEMORY_PROTECT_READ?AS_PERM_USER_RO:AS_PERM_USER_RW);
 chos_spin_unlock_irqrestore(&memory_lock,irq);return error==AS_OK?0:-CHOS_ERRNO_EIO;
}
int chos_user_memory_unmap_range(address_space_handle_t h,uint64_t address,size_t bytes){
 uint32_t pages;
 if((address&(VMM_PAGE_SIZE-1u))||!length_pages(bytes,&pages))return -CHOS_ERRNO_EINVAL;
 uint64_t irq=chos_spin_lock_irqsave(&memory_lock);map_owner_t *o=owner(h);
 if(!o){chos_spin_unlock_irqrestore(&memory_lock,irq);return -CHOS_ERRNO_ESRCH;}
 region_t *r=containing_region(o,address,pages);
 if(!r){chos_spin_unlock_irqrestore(&memory_lock,irq);return -CHOS_ERRNO_EINVAL;}
 if(r->pins){chos_spin_unlock_irqrestore(&memory_lock,irq);return -CHOS_ERRNO_EBUSY;}
 uint32_t left=(uint32_t)((address-r->first)/VMM_PAGE_SIZE),right=r->pages-left-pages;
 region_t *tail=NULL;
 if(left&&right){
  for(unsigned i=0;i<o->region_limit;i++)if(!o->regions[i].state){tail=&o->regions[i];break;}
  if(!tail){chos_spin_unlock_irqrestore(&memory_lock,irq);return -CHOS_ERRNO_ENOMEM;}
 }
 /* Preflight and reserve metadata before modifying any page. On an unexpected
  * table/free failure quarantine the entire original region until process reap. */
 for(uint32_t p=0;p<pages;p++){
  vmm_mapping_info_t m;
  if(!address_space_query(h,address+(uint64_t)p*VMM_PAGE_SIZE,&m)){
   r->state=2;chos_spin_unlock_irqrestore(&memory_lock,irq);return -CHOS_ERRNO_EIO;
  }
 }
 region_t part={.first=address,.pages=pages,.state=1};
 if(!remove_pages(h,&part,1)){r->state=2;chos_spin_unlock_irqrestore(&memory_lock,irq);return -CHOS_ERRNO_EIO;}
 uint64_t after=address+(uint64_t)pages*VMM_PAGE_SIZE;
 if(left){r->pages=left;if(right)*tail=(region_t){.first=after,.pages=right,.state=1};}
 else if(right){r->first=after;r->pages=right;}
 else *r=(region_t){0};
 chos_spin_unlock_irqrestore(&memory_lock,irq);return 0;
}

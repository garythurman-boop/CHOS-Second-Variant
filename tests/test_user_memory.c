#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/mm/user_memory.c"
static struct {uint64_t pa;int used,mapped;} frames[4][32768];
static uint64_t mappings[4][24576];
static address_space_user_permissions_t permissions[4][24576];
static unsigned protect_calls,discard_calls,control_fail;
static unsigned profiles[4],references[4],retain_fail,release_fail,release_probe,release_observed;
static unsigned generations[4]={1,1,1,1},allocations,allocation_fail,map_calls,map_fail,unmap_fail,free_fail;
static unsigned index_of(uint64_t va){
 if(va>=CHOS_RESOURCE_ENGINE_MAP_BASE && va<CHOS_RESOURCE_ENGINE_MAP_BASE+CHOS_RESOURCE_ENGINE_MAP_BYTES)
  return (unsigned)((va-CHOS_RESOURCE_ENGINE_MAP_BASE)/4096u);
 assert(va>=CHOS_USER_MAP_BASE&&va<CHOS_USER_MAP_BASE+CHOS_USER_MAP_BYTES);
 return (unsigned)((va-CHOS_USER_MAP_BASE)/4096u);
}
uint64_t chos_spin_lock_irqsave(chos_spinlock_t *p){assert(!p->held);p->held=1;return 0;}
void chos_spin_unlock_irqrestore(chos_spinlock_t *p,uint64_t state){(void)state;assert(p->held);p->held=0;}
int address_space_get_info(address_space_handle_t h,address_space_info_t *out){if(h.slot>=4||h.generation!=generations[h.slot]||h.id!=h.slot+1)return 0;*out=(address_space_info_t){.live=1,.resource_profile=profiles[h.slot],.task_references=references[h.slot]};return 1;}
address_space_error_t address_space_task_retain(address_space_handle_t h){if(retain_fail)return AS_ERR_BAD_HANDLE;++references[h.slot];return AS_OK;}
address_space_error_t address_space_task_release(address_space_handle_t h){assert(references[h.slot]);if(release_fail)return AS_ERR_BAD_REFERENCE;--references[h.slot];return AS_OK;}

int address_space_query(address_space_handle_t h,uint64_t va,vmm_mapping_info_t *out){uint64_t pa=mappings[h.slot][index_of(va)];if(!pa)return 0;memset(out,0,sizeof(*out));out->physical_address=pa;out->memory_type=VMM_MEMORY_NORMAL;out->accessible_at_el0=permissions[h.slot][index_of(va)]!=AS_PERM_USER_NONE;out->writable=permissions[h.slot][index_of(va)]==AS_PERM_USER_RW;return 1;}
address_space_error_t address_space_alloc_user_page(address_space_handle_t h,uint64_t *pa){if(++allocations==allocation_fail)return AS_ERR_OUT_OF_MEMORY;for(unsigned i=0;i<32768;i++)if(!frames[h.slot][i].used){frames[h.slot][i].used=1;*pa=frames[h.slot][i].pa=0x50000000u+h.slot*0x10000000u+i*4096;return AS_OK;}return AS_ERR_FRAME_CAPACITY;}
address_space_error_t address_space_map_user_page(address_space_handle_t h,uint64_t va,uint64_t pa,address_space_user_permissions_t perm){assert(perm==AS_PERM_USER_RW);if(++map_calls==map_fail)return AS_ERR_OUT_OF_MEMORY;unsigned i=(unsigned)((pa-0x50000000u-h.slot*0x10000000u)/4096);assert(frames[h.slot][i].used&&!frames[h.slot][i].mapped);frames[h.slot][i].mapped=1;mappings[h.slot][index_of(va)]=pa;permissions[h.slot][index_of(va)]=perm;return AS_OK;}
address_space_error_t address_space_unmap_user_page(address_space_handle_t h,uint64_t va,uint64_t *pa){
 if(release_probe&&owners[h.slot].release_busy){
  release_probe=0;++release_observed;assert(!memory_lock.held&&references[h.slot]==1);
  assert(chos_user_memory_map(h,4096,0)==-CHOS_ERRNO_ESRCH);
  assert(!chos_user_memory_release(h));
  address_space_handle_t peer={.slot=2,.id=3,.generation=1};
  int64_t other=chos_user_memory_map(peer,4096,0);assert(other>0);
  assert(!chos_user_memory_unmap(peer,(uint64_t)other,4096));
 }
if(unmap_fail)return AS_ERR_BAD_DESCRIPTOR;
 uint64_t p=mappings[h.slot][index_of(va)];assert(p);if(pa)*pa=p;frames[h.slot][(p-0x50000000u-h.slot*0x10000000u)/4096].mapped=0;mappings[h.slot][index_of(va)]=0;return AS_OK;}
address_space_error_t address_space_free_user_page(address_space_handle_t h,uint64_t pa){unsigned i=(unsigned)((pa-0x50000000u-h.slot*0x10000000u)/4096);assert(frames[h.slot][i].used&&!frames[h.slot][i].mapped);if(free_fail)return AS_ERR_BAD_DESCRIPTOR;frames[h.slot][i].used=0;return AS_OK;}
address_space_error_t address_space_protect_user_range(address_space_handle_t h,uint64_t va,size_t n,address_space_user_permissions_t p){
 protect_calls++;if(control_fail)return AS_ERR_BAD_DESCRIPTOR;
 assert(!(n%4096)&&n&&p!=AS_PERM_USER_RX);
 for(size_t at=0;at<n;at+=4096){assert(mappings[h.slot][index_of(va+at)]);permissions[h.slot][index_of(va+at)]=p;}
 return AS_OK;
}
address_space_error_t address_space_discard_user_range(address_space_handle_t h,uint64_t va,size_t n){
 discard_calls++;if(control_fail)return AS_ERR_BAD_DESCRIPTOR;
 assert(n&&!(n%4096));for(size_t at=0;at<n;at+=4096)assert(mappings[h.slot][index_of(va+at)]);return AS_OK;
}
static unsigned used(unsigned slot){unsigned n=0;for(unsigned i=0;i<32768;i++)n+=frames[slot][i].used;return n;}
static void reset(void){memset(owners,0,sizeof(owners));memset(profiles,0,sizeof(profiles));memset(references,0,sizeof(references));retain_fail=release_fail=release_probe=release_observed=0;memset(frames,0,sizeof(frames));memset(mappings,0,sizeof(mappings));allocations=allocation_fail=map_calls=map_fail=unmap_fail=free_fail=protect_calls=discard_calls=control_fail=0;}
static void engine_profiles(void){
 address_space_handle_t a={.slot=0,.id=1,.generation=1},b={.slot=1,.id=2,.generation=1};
 reset();profiles[0]=CHOS_RESOURCE_ENGINE;
 for(unsigned i=0;i<CHOS_RESOURCE_ENGINE_REGIONS;i++){
  int64_t va=chos_user_memory_map(a,4096,0);assert(va>0&&(uint64_t)va>CHOS_RESOURCE_ENGINE_MAP_BASE);
 }
 assert(used(0)==512&&owners[0].region_limit==512);
 assert(chos_user_memory_map(a,4096,0)==-CHOS_ERRNO_ENOMEM);
 for(unsigned i=0;i<CHOS_USER_MAP_REGIONS;i++)assert(chos_user_memory_map(b,4096,0)>0);
 assert(chos_user_memory_map(b,4096,0)==-CHOS_ERRNO_ENOMEM);
 release_probe=1;assert(chos_user_memory_release(a)&&release_observed==1&&!references[0]);
 assert(owners[0].releasing&&!owners[0].release_busy&&used(0)==512);
 assert(chos_user_memory_map(a,4096,0)==-CHOS_ERRNO_ESRCH);
 assert(chos_user_memory_release(a)); /* idempotent terminal reaper retry */
 for(unsigned i=0;i<24576;i++)assert(!mappings[0][i]);
 reset();profiles[0]=CHOS_RESOURCE_ENGINE;
 for(unsigned i=0;i<8;i++)assert(chos_user_memory_map(a,CHOS_USER_MAP_MAX_BYTES,0)>0);
 assert(used(0)==2048);assert(chos_user_memory_release(a)&&used(0)==2048);
 reset();int64_t va=chos_user_memory_map(a,4096,0);assert(va>0);
 references[0]=1;assert(!chos_user_memory_release(a)&&!owners[0].releasing);
 references[0]=0;retain_fail=1;assert(!chos_user_memory_release(a)&&!owners[0].releasing);
 retain_fail=0;assert(chos_user_memory_map(a,4096,0)>0);
 unmap_fail=1;assert(!chos_user_memory_release(a)&&owners[0].releasing&&!references[0]);
 unmap_fail=0;assert(chos_user_memory_release(a)&&!references[0]);
 reset();assert(chos_user_memory_map(a,4096,0)>0);release_fail=1;
 assert(!chos_user_memory_release(a)&&references[0]==1&&owners[0].releasing);
 reset();profiles[0]=2;assert(chos_user_memory_map(a,4096,0)==-CHOS_ERRNO_ESRCH&&!allocations);
}
int main(void){
 address_space_handle_t a={.slot=0,.id=1,.generation=1},b={.slot=1,.id=2,.generation=1};reset();
 assert(chos_user_memory_map(a,0,0)==-CHOS_ERRNO_EINVAL);assert(chos_user_memory_map(a,(size_t)-1,0)==-CHOS_ERRNO_EINVAL);assert(chos_user_memory_map(a,4096,1)==-CHOS_ERRNO_EINVAL);
 int64_t va=chos_user_memory_map(a,8192,0);assert(va>0&&used(0)==2);
 assert(chos_user_memory_pin(a,(uint64_t)va,8192));
 assert(!chos_user_memory_pin(a,(uint64_t)va,8192));
 assert(!chos_user_memory_pin(b,(uint64_t)va,8192));
 assert(!chos_user_memory_pin(a,(uint64_t)va+4096,4096));
 assert(chos_user_memory_unmap(a,(uint64_t)va,8192)==-CHOS_ERRNO_EBUSY);
 assert(!chos_user_memory_release(a)&&used(0)==2);
 assert(chos_user_memory_unpin(a,(uint64_t)va,8192));
 assert(!chos_user_memory_unpin(a,(uint64_t)va,8192));
 assert(chos_user_memory_unmap(b,(uint64_t)va,8192)==-CHOS_ERRNO_EINVAL);
 assert(chos_user_memory_unmap(a,(uint64_t)va,4096)==-CHOS_ERRNO_EINVAL);
 assert(chos_user_memory_unmap(a,CHOS_AS_USER_VA_BASE,4096)==-CHOS_ERRNO_EINVAL);
 assert(chos_user_memory_unmap(a,(uint64_t)va,8192)==0&&!used(0));assert(chos_user_memory_unmap(a,(uint64_t)va,8192)==-CHOS_ERRNO_EINVAL);
 reset();allocation_fail=2;assert(chos_user_memory_map(a,8192,0)==-CHOS_ERRNO_ENOMEM&&!used(0));
 reset();map_fail=2;assert(chos_user_memory_map(a,8192,0)==-CHOS_ERRNO_ENOMEM&&!used(0));
 /* Full-size mappings retain aperture guards and failure rollback at every
  * representative allocation/map boundary, including beyond the old32pages. */
 reset();assert(chos_user_memory_map(a,CHOS_USER_MAP_MAX_BYTES+1u,0)==-CHOS_ERRNO_EINVAL);
 va=chos_user_memory_map(a,CHOS_USER_MAP_MAX_BYTES,0);assert(va>0&&used(0)==CHOS_USER_MAP_MAX_BYTES/4096u);
 assert(chos_user_memory_map(a,CHOS_USER_MAP_MAX_BYTES,0)==-CHOS_ERRNO_ENOMEM);
 assert(!chos_user_memory_unmap(a,(uint64_t)va,CHOS_USER_MAP_MAX_BYTES)&&!used(0));
 const unsigned boundaries[]={1,32,33,CHOS_USER_MAP_MAX_BYTES/4096u};
 for(unsigned sample=0;sample<sizeof(boundaries)/sizeof(boundaries[0]);sample++){
  unsigned boundary=boundaries[sample];
  reset();allocation_fail=boundary;assert(chos_user_memory_map(a,CHOS_USER_MAP_MAX_BYTES,0)==-CHOS_ERRNO_ENOMEM&&!used(0));
  for(unsigned i=0;i<512;i++)assert(!mappings[0][i]);
  reset();map_fail=boundary;assert(chos_user_memory_map(a,CHOS_USER_MAP_MAX_BYTES,0)==-CHOS_ERRNO_ENOMEM&&!used(0));
  for(unsigned i=0;i<512;i++)assert(!mappings[0][i]);
 }
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
 /* Genuine range ownership and split bookkeeping, independent of the native wrapper. */
 a.generation=1;reset();va=chos_user_memory_map(a,5*4096,0);
 assert(!chos_user_memory_control(a,va+4096,1,CHOS_MEMORY_PROTECT_NONE));
 assert(permissions[0][index_of(va)]==AS_PERM_USER_RW&&permissions[0][index_of(va+4096)]==AS_PERM_USER_NONE);
 assert(!chos_user_memory_control(a,va+4096,4096,CHOS_MEMORY_PROTECT_READ));
 assert(!chos_user_memory_control(a,va+4096,4096,CHOS_MEMORY_DISCARD)&&discard_calls==1);
 assert(!chos_user_memory_control(a,va,5*4096,CHOS_MEMORY_VALIDATE));
 assert(chos_user_memory_control(b,va,4096,CHOS_MEMORY_PROTECT_RW)==-CHOS_ERRNO_EINVAL);
 assert(chos_user_memory_control(a,va-4096,4096,CHOS_MEMORY_PROTECT_RW)==-CHOS_ERRNO_EINVAL);
 assert(chos_user_memory_control(a,va,4096,4)==-CHOS_ERRNO_EINVAL);
 assert(chos_user_memory_control(a,va+1,4096,0)==-CHOS_ERRNO_EINVAL);
 assert(chos_user_memory_control(a,UINT64_MAX-4095u,4096,0)==-CHOS_ERRNO_EINVAL);
 assert(chos_user_memory_pin(a,va,5*4096));unsigned before=protect_calls;
 assert(chos_user_memory_control(a,va,4096,0)==-CHOS_ERRNO_EBUSY&&protect_calls==before);
 assert(chos_user_memory_control(a,va,4096,CHOS_MEMORY_DISCARD)==-CHOS_ERRNO_EBUSY);
 assert(chos_user_memory_unmap_range(a,va+4096,4096)==-CHOS_ERRNO_EBUSY);
 assert(chos_user_memory_unpin(a,va,5*4096));
 control_fail=1;assert(chos_user_memory_control(a,va,4096,0)==-CHOS_ERRNO_EIO);control_fail=0;
 assert(!chos_user_memory_unmap_range(a,va+2*4096,4096)&&used(0)==4);
 assert(chos_user_memory_control(a,va,5*4096,0)==-CHOS_ERRNO_EINVAL);
 assert(chos_user_memory_unmap_range(a,va,5*4096)==-CHOS_ERRNO_EINVAL);
 assert(!chos_user_memory_unmap_range(a,va,1)&&used(0)==3); /* prefix trimming, rounded length */
 assert(!chos_user_memory_unmap_range(a,va+4096,4096)&&used(0)==2);
 assert(!chos_user_memory_unmap_range(a,va+4*4096,4096)&&used(0)==1); /* suffix trimming */
 assert(!chos_user_memory_unmap_range(a,va+3*4096,4096)&&!used(0));
 assert(chos_user_memory_unmap_range(a,va,4096)==-CHOS_ERRNO_EINVAL);
 reset();va=chos_user_memory_map(a,3*4096,0);
 for(unsigned i=1;i<16;i++)assert(chos_user_memory_map(a,4096,0)>0);
 unsigned count=used(0);assert(chos_user_memory_unmap_range(a,va+4096,4096)==-CHOS_ERRNO_ENOMEM&&used(0)==count);
 assert(mappings[0][index_of(va+4096)]); /* split capacity failure changes no page */
 reset();va=chos_user_memory_map(a,3*4096,0);unmap_fail=1;
 assert(chos_user_memory_unmap_range(a,va+4096,4096)==-CHOS_ERRNO_EIO&&used(0)==3);
 assert(chos_user_memory_control(a,va,4096,3)==-CHOS_ERRNO_EINVAL);
 unmap_fail=0;assert(chos_user_memory_release(a));
 /* Managed stacks retain their pin until one atomic ownership/disposal step.
  * Rejected owners/partial sizes cannot release it or consume a new mapping. */
 reset();va=chos_user_memory_map(a,8192,0);assert(chos_user_memory_pin(a,va,8192));
 assert(chos_user_memory_retire_stack(b,va,8192)==-CHOS_ERRNO_EINVAL&&used(0)==2);
 assert(chos_user_memory_retire_stack(a,va,4096)==-CHOS_ERRNO_EINVAL&&used(0)==2);
 assert(chos_user_memory_retire_stack(a,va+1,8192)==-CHOS_ERRNO_EINVAL);
 assert(chos_user_memory_unmap(a,va,8192)==-CHOS_ERRNO_EBUSY);
 assert(!chos_user_memory_retire_stack(a,va,8192)&&!used(0));
 int64_t reused=chos_user_memory_map(a,8192,0);assert(reused==va);
 assert(chos_user_memory_retire_stack(a,va,8192)==-CHOS_ERRNO_EINVAL&&used(0)==2);
 assert(!chos_user_memory_unmap(a,reused,8192)&&!used(0));
 /* Failed disposal consumes the pin into quarantine: the process reaper can
  * finish unmapping and AS destruction retains every uncertain owned frame. */
 reset();va=chos_user_memory_map(a,8192,0);assert(chos_user_memory_pin(a,va,8192));unmap_fail=1;
 assert(chos_user_memory_retire_stack(a,va,8192)==-CHOS_ERRNO_EIO&&used(0)==2);
 unmap_fail=0;assert(chos_user_memory_release(a)&&used(0)==2);
 reset();va=chos_user_memory_map(a,8192,0);assert(chos_user_memory_pin(a,va,8192));free_fail=1;
 assert(chos_user_memory_retire_stack(a,va,8192)==-CHOS_ERRNO_EIO&&used(0)==2);
 free_fail=0;assert(chos_user_memory_release(a)&&used(0)==2);
 reset();va=chos_user_memory_map(a,8192,0);assert(chos_user_memory_pin(a,va,8192));mappings[0][index_of(va)]=0;
 assert(chos_user_memory_retire_stack(a,va,8192)==-CHOS_ERRNO_EIO&&used(0)==2);
 assert(chos_user_memory_release(a));
 engine_profiles();
 puts("anonymous user mappings: bounds, owner separation, rollback, quota, non-executable pages, managed stack disposal and quarantine passed");
}

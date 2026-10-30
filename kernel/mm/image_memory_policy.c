#include "chos/image_memory_policy.h"
#include "chos/image_memory_api.h"
#include "chos/syscall.h"
int chos_image_memory_range(uint64_t base,uint32_t pages,const uint64_t *mapped,const uint64_t *inaccessible,
 uint64_t address,uint64_t bytes,uint64_t operation,uint32_t *first,uint32_t *count){
 if(!pages||!mapped||!inaccessible||!first||!count||(base&4095u)||(address&4095u)||address<base||
    !bytes||bytes>CHOS_IMAGE_PROTECT_MAX_BYTES||
    (operation!=CHOS_MEMORY_PROTECT_NONE&&operation!=CHOS_MEMORY_PROTECT_READ))return -CHOS_ERRNO_EINVAL;
 uint64_t start=(address-base)/4096u,n=(bytes+4095u)/4096u;
 if(start>=pages||n>pages-start||n*4096u>UINT64_MAX-address)return -CHOS_ERRNO_EINVAL;
 for(uint64_t i=start;i<start+n;i++){
  uint64_t bit=UINT64_C(1)<<(i%64u);
  if(!(mapped[i/64u]&bit))return -CHOS_ERRNO_EINVAL;
  if(operation==CHOS_MEMORY_PROTECT_READ&&(inaccessible[i/64u]&bit))return -CHOS_ERRNO_EPERM;
 }
 *first=(uint32_t)start;*count=(uint32_t)n;return 0;
}

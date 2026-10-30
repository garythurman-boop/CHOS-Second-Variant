#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "chos/image_memory_api.h"
#include "chos/image_memory_policy.h"
#include "chos/syscall.h"
static uint64_t random_state=UINT64_C(0x494d414745524f31);
static uint64_t random64(void){random_state^=random_state<<13;random_state^=random_state>>7;random_state^=random_state<<17;return random_state;}
static uint64_t mapped[16],inaccessible[16];
static unsigned char present[1024],blocked[1024];
int main(void){
 uint32_t first=17,count=19;const uint64_t base=UINT64_C(0x20000000);
 for(unsigned sample=0;sample<50000;sample++){
  uint32_t pages=1+random64()%1024;memset(mapped,0,sizeof mapped);memset(inaccessible,0,sizeof inaccessible);
  for(unsigned i=0;i<pages;i++){
   present[i]=(sample&1)||random64()%4!=0;blocked[i]=random64()%8==0;
   if(present[i])mapped[i/64]|=UINT64_C(1)<<(i%64);
   if(blocked[i])inaccessible[i/64]|=UINT64_C(1)<<(i%64);
  }
  uint64_t address=base+(random64()%1200)*4096,bytes=random64()%(CHOS_IMAGE_PROTECT_MAX_BYTES+2u),operation=random64()%4;
  if(!(sample%7))address++;
  if(!(sample%11))address=UINT64_MAX-4095;
  if(!(sample%13))operation=UINT64_C(0x100000001);
  __int128 end=(__int128)address+((__int128)bytes+4095)/4096*4096;
  int expected=bytes&&bytes<=CHOS_IMAGE_PROTECT_MAX_BYTES&&!(address&4095)&&address>=base&&(operation==0||operation==1)&&
   end<=(__int128)base+pages*4096&&end<=UINT64_MAX;
  if(expected)for(unsigned i=0;i<pages;i++){
   __int128 low=(__int128)base+i*4096,high=low+4096;
   if(low<end&&high>address&&(!present[i]||(operation==1&&blocked[i])))expected=0;
  }
  first=17;count=19;int r=chos_image_memory_range(base,pages,mapped,inaccessible,address,bytes,operation,&first,&count);
  assert((!r)==expected);
  if(!r){assert(first==(address-base)/4096&&count==(bytes+4095)/4096);}
  else assert(first==17&&count==19);
 }
 mapped[0]=7;inaccessible[0]=2;
 assert(chos_image_memory_range(base,3,mapped,inaccessible,base,8192,1,&first,&count)==-CHOS_ERRNO_EPERM);
 assert(!chos_image_memory_range(base,3,mapped,inaccessible,base,8192,0,&first,&count)&&first==0&&count==2);
 assert(!chos_image_memory_range(base,3,mapped,inaccessible,base+8192,1,1,&first,&count)&&count==1);
 assert(chos_image_memory_range(base,3,mapped,inaccessible,base+8192,4097,1,&first,&count)==-CHOS_ERRNO_EINVAL);
 assert(chos_image_memory_range(base,3,0,inaccessible,base,4096,1,&first,&count)==-CHOS_ERRNO_EINVAL);
 puts("image protection:50000 independent sparse interval/set references,full-width modes,rounded ends and unchanged error outputs passed");return 0;
}

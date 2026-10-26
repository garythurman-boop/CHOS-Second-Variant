#include <assert.h>
#include "chos/process_caps.h"
#include "chos/syscall.h"
int main(void){
 uint64_t out=123;
 assert(chos_process_cap_reduction(CHOS_PROCESS_CAP_ALL,0,&out));
 assert(out==CHOS_PROCESS_CAP_REQUIRED);
 uint64_t reduced=CHOS_PROCESS_CAP_REQUIRED|CHOS_PROCESS_CAP(CHOS_SYS_WRITE);
 assert(chos_process_cap_reduction(CHOS_PROCESS_CAP_ALL,reduced,&out)&&out==reduced);
 assert(chos_process_cap_reduction(reduced,reduced,&out)&&out==reduced);
 out=123;assert(!chos_process_cap_reduction(reduced,CHOS_PROCESS_CAP_ALL,&out)&&out==123);
 assert(!chos_process_cap_reduction(CHOS_PROCESS_CAP_ALL,UINT64_MAX,&out)&&out==123);
 assert(!chos_process_cap_reduction(CHOS_PROCESS_CAP_ALL,1,&out)&&out==123);
 assert(!chos_process_cap_reduction(0,0,&out)&&out==123);
 assert(!chos_process_cap_reduction(CHOS_PROCESS_CAP_ALL,0,0));
 for(unsigned i=1;i<=CHOS_PROCESS_SYSCALL_LIMIT;i++){
  uint64_t cap=CHOS_PROCESS_CAP(i);
  assert(chos_process_cap_reduction(CHOS_PROCESS_CAP_ALL,cap,&out));
  assert(out==(cap|CHOS_PROCESS_CAP_REQUIRED));
  for(unsigned j=1;j<=CHOS_PROCESS_SYSCALL_LIMIT;j++)
   if(j!=i && !(CHOS_PROCESS_CAP(j)&CHOS_PROCESS_CAP_REQUIRED))assert(!chos_process_cap_reduction(out,CHOS_PROCESS_CAP(j),&reduced));
 }
 return 0;
}

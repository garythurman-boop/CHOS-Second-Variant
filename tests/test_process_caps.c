#include <assert.h>
#include "chos/process_caps.h"
#include "chos/syscall.h"
#include "chos/file_write_api.h"
#include "chos/file_metadata_api.h"
#include "chos/directory_api.h"
int main(void){
 assert(CHOS_SYS_DIRECTORY_PAGE==77&&sizeof(chos_directory_page_t)==640&&chos_process_syscall_capability(77)==CHOS_PROCESS_CAP(CHOS_SYS_LIST_DIR));
 assert(CHOS_SYS_FILE_METADATA==75&&CHOS_SYS_PATH_METADATA==76&&sizeof(chos_file_metadata_t)==64&&sizeof(chos_file_identity_t)==24);
 assert(chos_process_syscall_capability(75)==CHOS_PROCESS_CAP(CHOS_SYS_READ)&&chos_process_syscall_capability(76)==CHOS_PROCESS_CAP(CHOS_SYS_READ));
 assert(CHOS_SYS_FILE_OPEN_WRITE==70&&CHOS_SYS_FILE_WRITE==71&&CHOS_SYS_FILE_TRUNCATE==72&&CHOS_SYS_FILE_FLUSH==73&&CHOS_SYS_FILE_ABORT==74);
 assert(sizeof(chos_file_open_write_t)==32&&CHOS_FILE_WRITE_VERSION==1);
 for(unsigned n=70;n<=73;n++)assert(chos_process_syscall_capability(n)==CHOS_PROCESS_CAP(CHOS_SYS_WRITE_FILE));
 assert(chos_process_syscall_capability(74)==CHOS_PROCESS_CAP(CHOS_SYS_CLOSE));
 assert(CHOS_SYS_FILE_INFO==68&&CHOS_SYS_READ_AT==69);
 assert(chos_process_syscall_capability(68)==CHOS_PROCESS_CAP(CHOS_SYS_READ)&&chos_process_syscall_capability(69)==CHOS_PROCESS_CAP(CHOS_SYS_READ));
 assert(CHOS_SYS_THREAD_CONTEXT==66&&chos_process_syscall_capability(66)==CHOS_PROCESS_CAP(CHOS_SYS_THREAD_INFO));
 assert(CHOS_SYS_MEMORY_CONTROL==64&&CHOS_SYS_UNMAP_RANGE==65);
 assert(chos_process_syscall_capability(64)==CHOS_PROCESS_CAP(CHOS_SYS_MAP_MEMORY));
 assert(chos_process_syscall_capability(65)==CHOS_PROCESS_CAP(CHOS_SYS_UNMAP_MEMORY));
 assert((int64_t)CHOS_PROCESS_CAP_ALL>0);
 assert(CHOS_SYS_SEEK==63&&CHOS_SYSCALL_LIMIT==77);
 assert(chos_process_syscall_capability(CHOS_SYS_SEEK)==CHOS_PROCESS_CAP(CHOS_SYS_READ));
 assert(CHOS_PROCESS_CAP_SEEK==CHOS_PROCESS_CAP(CHOS_SYS_READ));
 assert(!chos_process_syscall_capability(0)&&!chos_process_syscall_capability(CHOS_SYSCALL_LIMIT+1u)&&!chos_process_syscall_capability(UINT32_MAX));
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

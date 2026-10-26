#include "chos/user.h"
static int fail(unsigned line){const char *s="[FAIL] process permissions\n";chos_write(s,chos_strlen(s));return (int)line;}
#define CHECK(x) do{if(!(x))return fail(__LINE__);}while(0)
static uint64_t child_mask(void){return CHOS_PROCESS_CAP_REQUIRED|CHOS_PROCESS_CAP(CHOS_SYS_WRITE)|
 CHOS_PROCESS_CAP(CHOS_SYS_GETPID)|CHOS_PROCESS_CAP(CHOS_SYS_GETSTATUS)|CHOS_PROCESS_CAP(CHOS_SYS_WAIT)|
 CHOS_PROCESS_CAP(CHOS_SYS_YIELD)|CHOS_PROCESS_CAP(CHOS_SYS_SLEEP)|CHOS_PROCESS_CAP(CHOS_SYS_CLOCK_INFO)|
 CHOS_PROCESS_CAP(CHOS_SYS_MAP_MEMORY)|CHOS_PROCESS_CAP(CHOS_SYS_UNMAP_MEMORY)|
 CHOS_PROCESS_CAP(CHOS_SYS_SPAWN)|CHOS_PROCESS_CAP(CHOS_SYS_SPAWN_ARGS)|CHOS_PROCESS_CAP(CHOS_SYS_SPAWN_LIMITED);}
static int verify_denials(void){
 char data[64];
 CHECK((uint64_t)chos_capabilities()==child_mask());
 CHECK(chos_syscall0(999)==-CHOS_ERRNO_ENOSYS);
 CHECK(chos_open("/DOCUMENTS/SECRET.TXT")==-CHOS_ERRNO_EPERM);
 CHECK(chos_write_file("/DOCUMENTS/ESCAPE.TXT","bad",3)==-CHOS_ERRNO_EPERM);
 CHECK(chos_syscall0(CHOS_SYS_NET_OPEN)==-CHOS_ERRNO_EPERM);
 CHECK(chos_syscall2(CHOS_SYS_RANDOM_BYTES,(long)(uintptr_t)data,sizeof(data))==-CHOS_ERRNO_EPERM);
 CHECK(chos_syscall2(CHOS_SYS_GUI_SUBMIT,0,0)==-CHOS_ERRNO_EPERM);
 CHECK(chos_spawn_limited("/BIN/CAPTEST.ELF","leaf",CHOS_PROCESS_CAP_ALL)==-CHOS_ERRNO_EPERM);
 long page=chos_map_memory(4096);CHECK(page>0);*(volatile uint64_t *)(uintptr_t)page=0x43484f53;
 CHECK(!chos_unmap_memory((void *)(uintptr_t)page,4096));
 return 0;
}
int main(int argc,char **argv){
 long mask=chos_capabilities();CHECK(mask>0);
 if((uint64_t)mask!=CHOS_PROCESS_CAP_ALL){
  int rc=verify_denials();if(rc)return rc;
  if(argc>1&&chos_strcmp(argv[1],"child")==0){
   long pid=chos_spawn("/BIN/CAPTEST.ELF");CHECK(pid>0&&chos_wait((uint32_t)pid)==0);
   pid=chos_spawn_args("/BIN/CAPTEST.ELF","leaf");CHECK(pid>0&&chos_wait((uint32_t)pid)==0);
   pid=chos_spawn_limited("/BIN/HELLO.CHX","",0);CHECK(pid>0);
   /* Child has only EXIT/query rights: HELLO's write denied, exit remains valid. */
   CHECK(chos_wait((uint32_t)pid)>=0);
  }
  const char *s="[ OK ] reduced process and inherited children denied elevated operations\n";chos_write(s,chos_strlen(s));return 0;
 }
 CHECK(chos_spawn_limited("/BIN/CAPTEST.ELF","child",UINT64_MAX)==-CHOS_ERRNO_EPERM);
 CHECK(chos_spawn_limited("/BIN/CAPTEST.ELF",(const char *)(uintptr_t)UINT64_MAX,child_mask())==-CHOS_ERRNO_EFAULT);
 for(unsigned i=0;i<5;i++){
  long pid=chos_spawn_limited("/BIN/CAPTEST.ELF","child",child_mask());CHECK(pid>0);
  CHECK(chos_wait((uint32_t)pid)==0);
 }
 CHECK((uint64_t)chos_capabilities()==CHOS_PROCESS_CAP_ALL);
 const char *s="[ OK ] actual process permission reduction, inheritance and no escalation passed\n";chos_write(s,chos_strlen(s));return 0;
}

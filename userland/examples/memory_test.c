/* Live direct-boot EL0 mapping and W^X regression. No engine is claimed. */
#include "chos/user.h"
#include "chos/user_memory_limits.h"
static int failure(void){const char *s="[FAIL] EL0 anonymous mappings\n";chos_write(s,chos_strlen(s));return 1;}
int main(int argc,char **argv){
 if(chos_unmap_memory((void *)((uintptr_t)main&~(uintptr_t)4095u),4096)!=-CHOS_ERRNO_EINVAL)return failure();
 size_t first_size=(argc>1&&(!chos_strcmp(argv[1],"guard-large")||!chos_strcmp(argv[1],"execute-large")))?CHOS_USER_MAP_MAX_BYTES:8192u;
 long result=chos_map_memory(first_size);if(result<0)return failure();
 volatile unsigned char *p=(volatile unsigned char *)(uintptr_t)result;
 if(argc>1&&!chos_strcmp(argv[1],"guard")){p[-1]=1;return failure();}
 if(argc>1&&!chos_strcmp(argv[1],"guard-large")){p[first_size]=1;return failure();}
 if(argc>1&&(!chos_strcmp(argv[1],"execute")||!chos_strcmp(argv[1],"execute-large"))){
  *(volatile uint32_t *)(uintptr_t)p=0xd65f03c0u;
  ((void (*)(void))(uintptr_t)p)();return failure();
 }
 for(size_t i=0;i<8192;i++)if(p[i])return failure();
 for(size_t i=0;i<8192;i++)p[i]=(unsigned char)i;
 if(chos_unmap_memory((void *)(uintptr_t)p,4096)!=-CHOS_ERRNO_EINVAL || p[7000]!=(unsigned char)7000)return failure();
 if(chos_unmap_memory((void *)(uintptr_t)p,8192)!=0||chos_unmap_memory((void *)(uintptr_t)p,8192)!=-CHOS_ERRNO_EINVAL)return failure();
 if(chos_map_memory(0)!=-CHOS_ERRNO_EINVAL||chos_map_memory(CHOS_USER_MAP_MAX_BYTES+1u)!=-CHOS_ERRNO_EINVAL||
    chos_syscall2(CHOS_SYS_MAP_MEMORY,4096,0x100000000ull)!=-CHOS_ERRNO_EINVAL)return failure();
 result=chos_map_memory(CHOS_USER_MAP_MAX_BYTES);if(result<0)return failure();p=(volatile unsigned char *)(uintptr_t)result;
 for(size_t i=0;i<CHOS_USER_MAP_MAX_BYTES;i++)if(p[i])return failure();
 if(chos_map_memory(CHOS_USER_MAP_MAX_BYTES)!=-CHOS_ERRNO_ENOMEM)return failure();
 for(size_t i=0;i<CHOS_USER_MAP_MAX_BYTES;i++)p[i]=(unsigned char)(i*17u+3u);
 /* Leave one live region: the deferred process reaper must unmap/release it. */
 const char *s="[ OK ] EL0 anonymous RW/XN mappings and cleanup contract passed\n";chos_write(s,chos_strlen(s));return 0;
}

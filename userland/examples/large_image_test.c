#include "chos/user.h"
#include "chos/user_memory_limits.h"
#include <stdint.h>
extern const unsigned char chos_large_read_only[3u*1024u*1024u];
static volatile uint64_t writable=UINT64_C(0x123456789abcdef0),zeroed[2];
static int fail(void){const char *s="[FAIL] native large image\n";chos_write(s,chos_strlen(s));return 1;}
int main(int argc,char **argv){
 if(writable!=UINT64_C(0x123456789abcdef0)||zeroed[0]||zeroed[1])return fail();
 writable=7;zeroed[0]=9;
 if(argc>1&&!chos_strcmp(argv[1],"write-ro")){*(volatile unsigned char *)(uintptr_t)(chos_large_read_only+2u*1024u*1024u)=1;return fail();}
 if(argc>1&&!chos_strcmp(argv[1],"execute-ro")){((void (*)(void))(uintptr_t)(chos_large_read_only+2u*1024u*1024u))();return fail();}
 const unsigned char ret[]={0xc0,0x03,0x5f,0xd6};
 for(size_t i=0;i<sizeof(chos_large_read_only);i++){
  unsigned char expected=i>=2u*1024u*1024u&&i<2u*1024u*1024u+4u?ret[i-2u*1024u*1024u]:0x5a;
  if(chos_large_read_only[i]!=expected)return fail();
 }
 long mapped=chos_map_memory(CHOS_USER_MAP_MAX_BYTES);if(mapped<0)return fail();
 volatile unsigned char *p=(volatile unsigned char *)(uintptr_t)mapped;
 for(size_t i=0;i<CHOS_USER_MAP_MAX_BYTES;i++)if(p[i])return fail();
 for(size_t i=0;i<CHOS_USER_MAP_MAX_BYTES;i++)p[i]=(unsigned char)(i*29u+7u);
 /* This actually exceeds the old4MiB frame budget with code, stack and data;
  * leave the mapping owned so the process reaper must reclaim every frame. */
 const char *s="[ OK ] native3MiB read-only image and1MiB private mapping passed\n";chos_write(s,chos_strlen(s));return 0;
}

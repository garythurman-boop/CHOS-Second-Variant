#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "chos/user.h"
#include "chos/libc.h"
int chos_native_cpp_api_smoke(void);

static int fail(unsigned line){
    const char *s="[FAIL] native C allocator/string runtime\n";chos_write(s,strlen(s));
    return (int)(line?line:1);
}
#define CHECK(x) do{if(!(x))return fail(__LINE__);}while(0)
int main(int argc,char **argv){
    chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&!stats.regions);
    CHECK(!chos_native_cpp_api_smoke());
    if(argc>1&&!strcmp(argv[1],"abandon")){
        unsigned char *p=malloc(100000),*q=calloc(8000,8);CHECK(p&&q);
        for(unsigned i=0;i<100000;i++)p[i]=(unsigned char)i;
        for(unsigned i=0;i<64000;i++)CHECK(!q[i]);
        CHECK(!chos_libc_heap_info(&stats)&&stats.regions==2);
        const char *s="[ OK ] native C abandoned private heap exits for process reaper\n";chos_write(s,strlen(s));return 0;
    }
    unsigned char *p=malloc(47);CHECK(p&&!((uintptr_t)p%16));memset(p,0x43,47);
    p=realloc(p,90000);CHECK(p);for(unsigned i=0;i<47;i++)CHECK(p[i]==0x43);
    unsigned char *unchanged=p;errno=0;CHECK(!realloc(p,SIZE_MAX)&&errno==ENOMEM);
    for(unsigned i=0;i<47;i++)CHECK(unchanged[i]==0x43);
    CHECK(realloc(p,7)==p);free(p);
    errno=0;CHECK(!calloc(SIZE_MAX,2)&&errno==ENOMEM);
    errno=0;CHECK(!aligned_alloc(64,65)&&errno==EINVAL);
    void *aligned=(void *)(uintptr_t)1;
    errno=77;CHECK(posix_memalign(&aligned,3,64)==EINVAL&&aligned==(void *)(uintptr_t)1&&errno==77);
    CHECK(!posix_memalign(&aligned,65536,129)&&!((uintptr_t)aligned%65536));memset(aligned,0x55,129);free(aligned);
    p=aligned_alloc(4096,4096);CHECK(p&&!((uintptr_t)p%4096));memset(p,0x53,4096);free(p);
    p=calloc(97,11);CHECK(p);for(unsigned i=0;i<1067;i++)CHECK(!p[i]);free(p);
    p=malloc(0);CHECK(p);free(p);free(0);
    void *large[CHOS_HEAP_REGIONS];
    for(unsigned i=0;i<CHOS_HEAP_REGIONS;i++){large[i]=malloc(100000);CHECK(large[i]);memset(large[i],(int)i,100000);}
    errno=0;CHECK(!malloc(100000)&&errno==ENOMEM);
    for(unsigned i=0;i<CHOS_HEAP_REGIONS;i++){
        unsigned char *bytes=large[i];for(unsigned j=0;j<100000;j++)CHECK(bytes[j]==i);free(large[i]);
    }
    CHECK(!chos_libc_heap_info(&stats)&&!stats.regions);
    char text[64];strcpy(text,"CHOS");strcat(text," runtime");strncat(text," strings omitted",8);
    CHECK(!strcmp(text,"CHOS runtime strings"));CHECK(strlen(text)==20);
    CHECK(strchr(text,'r')==text+5&&strrchr(text,'r')==text+15);
    CHECK(strstr(text,"runtime")==text+5&&strstr(text,"")==text);
    memmove(text+2,text,20);CHECK(!memcmp(text+2,"CHOS runtime strings",20));
    memmove(text,text+2,20);text[20]=0;CHECK(!strncmp(text,"CHOS runtime strings",21));
    unsigned char *slots[48]={0};size_t sizes[48]={0};
    for(unsigned round=0;round<8;round++){
        for(unsigned i=0;i<48;i++){
            size_t size=(i*791u+round*127u)%6000u+1u;
            if(slots[i])for(size_t j=0;j<sizes[i];j++)CHECK(slots[i][j]==(unsigned char)i);
            unsigned char *next=realloc(slots[i],size);CHECK(next);
            size_t keep=sizes[i]<size?sizes[i]:size;
            for(size_t j=0;j<keep;j++)CHECK(next[j]==(unsigned char)i);
            slots[i]=next;sizes[i]=size;memset(next,(int)i,size);
        }
        for(unsigned i=round%3u;i<48;i+=3){free(slots[i]);slots[i]=0;sizes[i]=0;}
        CHECK(!chos_libc_heap_info(&stats)&&stats.allocations==32);
    }
    for(unsigned i=0;i<48;i++)free(slots[i]);
    CHECK(!chos_libc_heap_info(&stats)&&!stats.regions&&!stats.allocations&&!stats.quarantined_regions);
    const char *s="[ OK ] native C malloc/calloc/realloc/alignment, strings and heap reclamation passed\n";
    chos_write(s,strlen(s));return 0;
}

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <chos/libc.h>
#include <chos/user.h>
#include <chos/resource_profile_user.h>
#define CHECK(x) do{if(!(x)){printf("[FAIL] native large heap line %u\n",__LINE__);return 1;}}while(0)
#define MIB (1024u*1024u)
static int empty(void){chos_heap_stats_t s;return !chos_libc_heap_info(&s)&&!s.allocations&&!s.mapped_bytes&&!s.quarantined_regions;}
static int sequential(void){
    CHECK(empty());
    const size_t sizes[]={MIB-32u,MIB,8u*MIB,16u*MIB,32u*MIB-32u};
    for(unsigned k=0;k<sizeof(sizes)/sizeof(*sizes);k++){
        size_t n=sizes[k];unsigned char *p=malloc(n);CHECK(p&&!((uintptr_t)p%16));
        for(size_t i=0;i<n;i++)CHECK(!p[i]);
        for(size_t i=0;i<n;i++)p[i]=(unsigned char)(i*31u+k);
        for(size_t i=0;i<n;i++)CHECK(p[i]==(unsigned char)(i*31u+k));
        chos_heap_stats_t s;CHECK(!chos_libc_heap_info(&s)&&s.allocations==1&&s.requested_bytes==n);
        CHECK(s.mapped_bytes>=n&&s.mapped_bytes<=32u*MIB);
        free(p);CHECK(empty());
    }
    CHECK(!malloc(32u*MIB)&&errno==ENOMEM);CHECK(!malloc(SIZE_MAX)&&errno==ENOMEM);
    CHECK(!calloc(SIZE_MAX,2)&&errno==ENOMEM);CHECK(empty());
    unsigned char *p=calloc(3u*1024u,2048);CHECK(p);
    for(size_t i=0;i<6u*MIB;i++)CHECK(!p[i]);free(p);CHECK(empty());
    void *a=aligned_alloc(MIB,2u*MIB);CHECK(a&&!((uintptr_t)a%MIB));memset(a,0x55,2u*MIB);free(a);CHECK(empty());
    a=(void *)0x1234;CHECK(posix_memalign(&a,3,8)==EINVAL&&a==(void *)0x1234);
    CHECK(!posix_memalign(&a,65536,4u*MIB)&&!((uintptr_t)a%65536));memset(a,0x42,4u*MIB);free(a);CHECK(empty());
    p=malloc(39);CHECK(p);memset(p,0x51,39);p=realloc(p,8u*MIB);CHECK(p);
    for(unsigned i=0;i<39;i++)CHECK(p[i]==0x51);memset(p,0x34,8u*MIB);
    unsigned char *old=p;CHECK(!realloc(p,32u*MIB)&&errno==ENOMEM);
    for(size_t i=0;i<8u*MIB;i++)CHECK(old[i]==0x34);
    p=realloc(old,16u*MIB);CHECK(p);for(size_t i=0;i<8u*MIB;i++)CHECK(p[i]==0x34);
    CHECK(realloc(p,20)==p);CHECK(!realloc(p,0));CHECK(empty());
    puts("[ OK ] native contiguous32MiB boundary,zeroing,alignment and realloc passed");return 0;
}
static void *worker(void *raw){
    unsigned index=(uintptr_t)raw;
    for(unsigned round=0;round<3;round++){
        unsigned char *p=malloc(8u*MIB);if(!p)return (void *)1;
        unsigned char value=(unsigned char)(0x40+index+round);
        for(size_t i=0;i<8u*MIB;i++)p[i]=(unsigned char)(value+i);
        for(size_t i=0;i<8u*MIB;i++)if(p[i]!=(unsigned char)(value+i))return (void *)2;
        free(p);
    }
    return NULL;
}
int main(int argc,char **argv){
    if(chos_native_resource_profile()!=CHOS_RESOURCE_ENGINE){
        CHECK(!malloc(MIB)&&errno==ENOMEM);CHECK(!malloc(8u*MIB)&&errno==ENOMEM);
        unsigned char *p=malloc(MIB-32u);CHECK(p);memset(p,0x31,MIB-32u);free(p);CHECK(empty());
        puts("[ OK ] legacy heap keeps1MiB mapping limit");return 0;
    }
    if(argc==2){
        unsigned char *p=malloc(8u*MIB);CHECK(p);uintptr_t address=0;
        if(!strcmp(argv[1],"low"))address=(uintptr_t)p-33u;
        else if(!strcmp(argv[1],"high"))address=((uintptr_t)p+8u*MIB+4095u)&~(uintptr_t)4095u;
        else return 2;
        puts("[ OK ] entering actual large allocation guard fault");
        *(volatile unsigned char *)address=1;return 3;
    }
    CHECK(!sequential());pthread_t threads[3];pthread_attr_t attr;
    CHECK(!pthread_attr_init(&attr)&&!pthread_attr_setstacksize(&attr,128u*1024u));
    for(unsigned i=0;i<3;i++)CHECK(!pthread_create(&threads[i],&attr,worker,(void *)(uintptr_t)i));
    for(unsigned i=0;i<3;i++){void *result=NULL;CHECK(!pthread_join(threads[i],&result)&&!result);}
    CHECK(!pthread_attr_destroy(&attr));CHECK(empty());
    puts("[ OK ] actual native large heap workers and cleanup passed");return 0;
}

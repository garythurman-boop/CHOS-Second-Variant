#include <chos/user.h>
#include <chos/resource_profile_user.h>
#include <pthread.h>
#include <sys/mman.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cerrno>

alignas(4096) static volatile unsigned char large_bss[48u*1024u*1024u];
extern "C" char __chos_image_end[];
static bool failure(unsigned line){std::printf("[FAIL] engine executable line %u\n",line);return false;}
#define CHECK(v) do{if(!(v))return failure(__LINE__);}while(0)
static bool stack_probe(){
    alignas(16) volatile unsigned char bytes[65536];
    for(unsigned i=0;i<sizeof(bytes);i++)bytes[i]=(unsigned char)(i*7u);
    chos_thread_context_t context{};
    CHECK(!chos_thread_context(&context));
    CHECK(context.stack_bytes==128u*1024u && context.flags==7);
    CHECK((uintptr_t)bytes>=context.stack_base &&
          (uintptr_t)bytes+sizeof(bytes)<=context.stack_base+context.stack_bytes);
    for(unsigned i=0;i<sizeof(bytes);i++)CHECK(bytes[i]==(unsigned char)(i*7u));
    return true;
}
static void *worker(void*){return (void*)(uintptr_t)(stack_probe()?0x43484f53u:1u);}
static bool normal(){
    CHECK(chos_native_resource_profile()==CHOS_RESOURCE_ENGINE);
    for(unsigned i=0;i<sizeof(large_bss);i++)CHECK(!large_bss[i]);
    for(unsigned i=0;i<sizeof(large_bss);i+=4096)for(unsigned k=0;k<4;k++)
        large_bss[i+k]=(unsigned char)((1u+i/4096u)>>(k*8u));
    CHECK(stack_probe());
    pthread_attr_t attr;size_t stack_bytes=0;
    CHECK(!pthread_attr_init(&attr)&&!pthread_attr_getstacksize(&attr,&stack_bytes));
    CHECK(stack_bytes==128u*1024u&&!pthread_attr_destroy(&attr));
    pthread_t thread;void *status=nullptr;
    CHECK(!pthread_create(&thread,nullptr,worker,nullptr));
    CHECK(!pthread_join(thread,&status)&&(uintptr_t)status==0x43484f53u);
    void *allocated[32]{};
    for(unsigned i=0;i<32;i++){
        allocated[i]=std::malloc(100000);CHECK(allocated[i]);
        std::memset(allocated[i],(int)i,100000);
    }
    for(unsigned i=0;i<32;i++){
        auto p=(unsigned char*)allocated[i];CHECK(p[0]==i&&p[99999]==i);std::free(p);
    }
    unsigned char *blocks[8]{};
    constexpr size_t chunk=1024u*1024u;
    for(unsigned i=0;i<8;i++){
        auto p=mmap(nullptr,chunk,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        CHECK(p!=MAP_FAILED&&(uintptr_t)p>CHOS_RESOURCE_ENGINE_MAP_BASE);
        blocks[i]=(unsigned char*)p;std::memset(p,(int)(i+16u),chunk);
    }
    CHECK(!mprotect(blocks[0],chunk,PROT_NONE));
    CHECK(!mprotect(blocks[0],chunk,PROT_READ|PROT_WRITE)&&blocks[0][0]==16);
    CHECK(mprotect(blocks[0],4096,PROT_READ|PROT_EXEC)==-1&&errno==EACCES);
    CHECK(!madvise(blocks[1],chunk,MADV_DONTNEED));
    for(unsigned i=0;i<chunk;i++)CHECK(!blocks[1][i]);
    CHECK(!munmap(blocks[0]+128u*4096u,4096));
    CHECK(!munmap(blocks[0],128u*4096u));
    CHECK(!munmap(blocks[0]+129u*4096u,127u*4096u));
    for(unsigned i=1;i<5;i++)CHECK(!munmap(blocks[i],chunk));
    void *small[32]{};
    for(unsigned i=0;i<32;i++){
        small[i]=mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        CHECK(small[i]!=MAP_FAILED);*(volatile uint32_t*)small[i]=i;
    }
    for(unsigned k=0;k<32;k++){
        unsigned i=(k*13u)%32u;CHECK(*(volatile uint32_t*)small[i]==i);
        CHECK(!munmap(small[i],4096));
    }
    for(unsigned i=0;i<sizeof(large_bss);i+=4096)for(unsigned k=0;k<4;k++)
        CHECK(large_bss[i+k]==(unsigned char)((1u+i/4096u)>>(k*8u)));
    // Three full mappings intentionally remain for terminal process cleanup.
    std::puts("[ OK ] native engine48MiB image,128KiB stacks,expanded heap and isolated mappings passed");
    return true;
}
int main(int argc,char **argv){
    if(argc==1)return normal()?0:1;
    chos_thread_context_t context{};
    if(chos_thread_context(&context)||context.stack_bytes!=128u*1024u)return 2;
    if(!std::strcmp(argv[1],"image-guard"))*(volatile char*) __chos_image_end=1;
    else if(!std::strcmp(argv[1],"stack-guard"))*(volatile char*)(uintptr_t)(context.stack_base-1u)=1;
    else if(!std::strcmp(argv[1],"profile-write")){
        auto address=(uintptr_t)&__chos_resource_profile_id;
        __asm__ volatile("str wzr,[%0]"::"r"(address):"memory");
    }
    else if(!std::strcmp(argv[1],"execute-heap")){
        void *p=mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(p==MAP_FAILED)return 3;
        *(volatile uint32_t*)p=0xd65f03c0u;
        reinterpret_cast<void(*)()>(p)();
    }else return 4;
    std::puts("[FAIL] engine fault probe unexpectedly returned");return 5;
}

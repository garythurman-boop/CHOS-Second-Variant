#include <chos/user.h>
#include <chos/resource_profile_user.h>
#include <chos/user_memory_limits.h>
#include <sys/mman.h>
#include <pthread.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <cstdint>
#include <time.h>
static constexpr size_t span=32u*1024u*1024u,chunk=CHOS_USER_MAP_MAX_BYTES,page=4096;
static bool failed(unsigned line){std::printf("[FAIL] engine reservation line%u errno%d\n",line,errno);return false;}
#define CHECK(v) do{if(!(v))return failed(__LINE__);}while(0)
static bool usage(chos_resource_info_t& value){CHECK(!chos_resource_info(&value));CHECK(value.flags&CHOS_RESOURCE_MEMORY_VALID);return true;}
static bool delta(const chos_resource_info_t& base,uint64_t bytes){
    chos_resource_info_t now{};CHECK(usage(now));
    CHECK(now.resident_bytes==base.resident_bytes+bytes&&now.mapped_bytes==base.mapped_bytes+bytes);return true;
}
static bool release(unsigned char *p,size_t n){
    while(n){size_t step=n>chunk?chunk:n;CHECK(!chos_unmap_range(p,step));p+=step;n-=step;}return true;
}
static bool normal(){
    errno=0;(void)pthread_self();auto warm=std::malloc(16);CHECK(warm);std::free(warm);
    std::puts("[INFO] native reservation resource fixture begins");
    chos_resource_info_t base{};CHECK(usage(base));
    long raw=chos_reserve_memory(span,span);CHECK(raw>0&&!(static_cast<uintptr_t>(raw)%span));
    auto p=reinterpret_cast<unsigned char*>(static_cast<uintptr_t>(raw));CHECK(delta(base,0));
    CHECK(chos_reserve_memory(span+page,page)==-EINVAL);
    CHECK(!mprotect(p,chunk,PROT_NONE)&&delta(base,0));
    CHECK(!chos_memory_control(p,chunk,CHOS_MEMORY_DECOMMIT)&&delta(base,0));
    for(unsigned i=0;i<span/chunk;i++){
        CHECK(!mprotect(p+i*chunk,chunk,PROT_READ|PROT_WRITE));
        auto data=static_cast<volatile unsigned char*>(p+i*chunk);
        for(size_t k=0;k<chunk;k++)CHECK(!data[k]);
        std::memset(p+i*chunk,static_cast<int>(i+1),chunk);
    }
    CHECK(delta(base,span));
    CHECK(!mprotect(p,page,PROT_READ)&&p[0]==1&&delta(base,span));
    CHECK(!mprotect(p,page,PROT_NONE)&&delta(base,span));
    CHECK(!mprotect(p,page,PROT_READ|PROT_WRITE)&&p[0]==1);
    CHECK(mprotect(p,page,PROT_READ|PROT_EXEC)==-1&&errno==EACCES);
    for(unsigned i=0;i<span/chunk;i+=2)CHECK(!chos_memory_control(p+i*chunk,chunk,CHOS_MEMORY_DECOMMIT));
    CHECK(delta(base,span/2));
    for(unsigned i=0;i<span/chunk;i+=2){
        CHECK(!mprotect(p+i*chunk,chunk,PROT_READ|PROT_WRITE));
        auto data=static_cast<volatile unsigned char*>(p+i*chunk);
        for(size_t k=0;k<chunk;k++)CHECK(!data[k]);
        std::memset(p+i*chunk,static_cast<int>(i+1),chunk);
    }
    CHECK(delta(base,span));
    for(unsigned i=0;i<span/chunk;i++)for(size_t k=0;k<chunk;k+=page)CHECK(p[i*chunk+k]==i+1);
    size_t hole=span/2;CHECK(!chos_unmap_range(p+hole,page));
    CHECK(mprotect(p+hole,page,PROT_READ|PROT_WRITE)==-1&&errno==EINVAL);
    CHECK(release(p,hole)&&release(p+hole+page,span-hole-page)&&delta(base,0));
    void *logical[512]{};unsigned count=0;
    for(;count<512;count++){
        long address=chos_reserve_memory(page,page);
        if(address<0){CHECK(address==-ENOMEM);break;}
        logical[count]=reinterpret_cast<void*>(static_cast<uintptr_t>(address));
    }
    CHECK(count>480&&count<=512&&delta(base,0));
    if(count==512)CHECK(chos_reserve_memory(page,page)==-ENOMEM);
    for(unsigned i=0;i<count;i++)CHECK(!chos_unmap_range(logical[i],page));
    CHECK(delta(base,0));
    raw=chos_reserve_memory(span,span);CHECK(raw>0);p=reinterpret_cast<unsigned char*>(static_cast<uintptr_t>(raw));
    const unsigned indices[]={0u,15u,31u};
    for(unsigned i:indices){CHECK(!mprotect(p+i*chunk,chunk,PROT_READ|PROT_WRITE));p[i*chunk]=static_cast<unsigned char>(i+1);}
    CHECK(delta(base,3*chunk)); // Intentionally left for terminal process cleanup.
    std::puts("[ OK ] native aligned32MiB reservation,sparse zero commit,decommit,split release and budgets passed");return true;
}
static std::atomic<unsigned> ready{};
static std::atomic<unsigned> cycles{};
static void* live_worker(void*){
    long raw=chos_reserve_memory(span,span);if(raw<0){ready.store(2);return nullptr;}
    auto p=reinterpret_cast<unsigned char*>(static_cast<uintptr_t>(raw));
    if(mprotect(p,chunk,PROT_READ|PROT_WRITE)){ready.store(2);return nullptr;}
    ready.store(1,std::memory_order_release);
    for(;;){
        if(chos_memory_control(p,chunk,CHOS_MEMORY_DECOMMIT)||mprotect(p,chunk,PROT_READ|PROT_WRITE)){ready.store(2);return nullptr;}
        p[0]=0x5a;
        cycles.fetch_add(1,std::memory_order_release);
    }
}
int main(int argc,char**argv){
    if(chos_native_resource_profile()!=CHOS_RESOURCE_ENGINE){
        if(chos_reserve_memory(page,page)!=-ENOTSUP)return 2;
        std::puts("[ OK ] legacy executable refuses engine virtual reservation");return 0;
    }
    if(argc==1)return normal()?0:1;
    if(!std::strcmp(argv[1],"live-worker")){
        pthread_t t;if(pthread_create(&t,nullptr,live_worker,nullptr))return 3;
        while(ready.load(std::memory_order_acquire)!=2&&cycles.load(std::memory_order_acquire)<3)chos_sleep_ticks(1);
        if(ready.load()!=1)return 4;
        std::puts("[INFO] native reservation worker loop active");
        timespec begin{},now{};if(clock_gettime(CLOCK_MONOTONIC,&begin))return 11;
        do {
            chos_sleep_ticks(1);if(clock_gettime(CLOCK_MONOTONIC,&now))return 12;
        }while((now.tv_sec-begin.tv_sec)*1000000000LL+now.tv_nsec-begin.tv_nsec<2000000000LL);
        if(ready.load()!=1)return 13;
        std::puts("[ OK ] native reservation worker remains live for process termination");return 0;
    }
    long raw=chos_reserve_memory(span,span);if(raw<0)return 5;
    auto p=reinterpret_cast<unsigned char*>(static_cast<uintptr_t>(raw));
    if(!std::strcmp(argv[1],"uncommitted"))*static_cast<volatile unsigned char*>(p)=1;
    else if(!std::strcmp(argv[1],"guard"))*(static_cast<volatile unsigned char*>(p)-1)=1;
    else if(!std::strcmp(argv[1],"readonly")){
        if(mprotect(p,page,PROT_READ))return 6;*static_cast<volatile unsigned char*>(p)=1;
    }else if(!std::strcmp(argv[1],"decommitted")){
        if(mprotect(p,page,PROT_READ|PROT_WRITE)||chos_memory_control(p,page,CHOS_MEMORY_DECOMMIT))return 7;
        *static_cast<volatile unsigned char*>(p)=1;
    }else if(!std::strcmp(argv[1],"execute")){
        if(mprotect(p,page,PROT_READ|PROT_WRITE))return 8;
        *reinterpret_cast<volatile uint32_t*>(p)=0xd65f03c0u;reinterpret_cast<void(*)()>(p)();
    }else return 9;
    std::puts("[FAIL] reservation fault unexpectedly returned");return 10;
}

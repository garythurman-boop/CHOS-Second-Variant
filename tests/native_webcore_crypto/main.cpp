#include "config.h"
#include <pal/ExportMacros.h>
#include <pal/crypto/CryptoDigest.h>
#include <wtf/Threading.h>
#include "cases.h"
#include "faults.h"
#include <pthread.h>
#include <cstdio>
#include <cstring>
#include <chos/libc.h>
using PAL::Crypto::CryptoDigest;
#define CHECK(x) do{if(!(x)){std::printf("[FAIL] actual PAL digest line %u\n",__LINE__);return false;}}while(0)
static bool cases(){
    static const size_t fragments[]={1,7,4096};
    for(const auto&c:digest_cases)for(size_t f=0;f<3;++f){if(c.length>65537&&f<2)continue;
        auto d=CryptoDigest::create(static_cast<CryptoDigest::Algorithm>(c.algorithm-1));CHECK(d);d->addBytes({});uint8_t data[4096];
        for(size_t p=0;p<c.length;){size_t n=c.length-p;if(n>fragments[f])n=fragments[f];digest_case_input(c.kind,p,data,n);d->addBytes(std::span<const uint8_t>(data,n));p+=n;}
        auto result=d->computeHash();CHECK(result.size()==c.digest_bytes&&!std::memcmp(result.span().data(),c.expected,c.digest_bytes));
        auto again=d->computeHash();CHECK(again==result);d->addBytes(std::span<const uint8_t>(data,1));CHECK(d->computeHash().isEmpty());
    }
    auto hex=CryptoDigest::create(CryptoDigest::Algorithm::SHA_256);CHECK(hex);
    hex->addBytes(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>("abc"),3));
    /* Original WTF::toHexString uses uppercase ASCII hexadecimal. */
    const char expected[]="BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD";
    CHECK(hex->toHexString()==WTF::String::fromLatin1(expected));
    return true;
}
static bool failures(){
    CHECK(!CryptoDigest::create(static_cast<CryptoDigest::Algorithm>(99)));
    for(unsigned a=1;a<=5;++a)for(unsigned operation=1;operation<=3;++operation){
        digest_arm_fault(operation==1?digest_fault_id(a,1):0);auto d=CryptoDigest::create(static_cast<CryptoDigest::Algorithm>(a-1));
        if(operation==1){CHECK(!d&&digest_fault_hits()==1);continue;}CHECK(d);
        digest_arm_fault(digest_fault_id(a,operation));d->addBytes(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>("abc"),3));
        CHECK(d->computeHash().isEmpty()&&digest_fault_hits()==1);
    }
    digest_arm_fault(10);CHECK(!CryptoDigest::create(CryptoDigest::Algorithm::SHA_256)&&digest_fault_hits()==1);digest_arm_fault(0);
    CryptoDigest uninitialized;CHECK(uninitialized.computeHash().isEmpty());return true;
}
static void*worker(void*){return cases()?nullptr:reinterpret_cast<void*>(1);}
static bool run(){
    CHECK(!digest_backend_tests());CHECK(cases()&&failures());
    pthread_t threads[3];pthread_attr_t attr;CHECK(!pthread_attr_init(&attr)&&!pthread_attr_setstacksize(&attr,128*1024));
    for(unsigned i=0;i<3;++i)CHECK(!pthread_create(threads+i,&attr,worker,nullptr));
    for(unsigned i=0;i<3;++i){void*r;CHECK(!pthread_join(threads[i],&r)&&!r);}CHECK(!pthread_attr_destroy(&attr));
    puts("[ OK ] original PAL CryptoDigest five algorithms,80 cases,errors and three threads passed");return true;
}
int main(){WTF::initialize();chos_heap_stats_t warm={};for(unsigned round=0;round<3;++round){if(!run())return 1;chos_heap_stats_t now;if(chos_libc_heap_info(&now))return 1;
    std::printf("[PAL DIGEST HEAP] round=%u allocations=%lu mapped=%lu\n",round,(unsigned long)now.allocations,(unsigned long)now.mapped_bytes);
    if(!round)warm=now;else if(now.allocations!=warm.allocations||now.mapped_bytes!=warm.mapped_bytes){puts("[FAIL] PAL digest warm heap changed");return 1;}}
    puts("[ OK ] actual PAL digest application completed");return 0;}

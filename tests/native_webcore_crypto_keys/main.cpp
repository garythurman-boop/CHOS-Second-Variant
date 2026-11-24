#include "PlatformCryptoKeyCHOS.h"
#include "faults.h"
#include "key-cases.h"
#ifdef KEY_HAVE_EXPORT_ORACLE
#include "key-export-oracle.h"
#endif
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __CHOS__
#include <chos/user.h>
#include <chos/libc.h>
#else
#include <errno.h>
#include <sys/random.h>
#endif

#define CHECK(x) do { if(!(x)){printf("[FAIL] crypto keys line %u: %s\n",__LINE__,#x);return 1;} } while(0)
using Key=WebCore::PlatformECKeyContainer;
static size_t checked_keys,checked_exports,refusals,allocation_trials;
static int os_rng(void *,unsigned char *out,size_t bytes) {
#ifdef __CHOS__
    return chos_webcore_key_entropy(nullptr, out, bytes);
#else
    size_t offset=0;
    while(offset<bytes){
        long n=getrandom(out+offset,bytes-offset,0);
        if(n<0&&errno==EINTR)continue;
        if(n<=0||(size_t)n>bytes-offset)return CHOS_KEY_NO_ENTROPY;
        offset+=(size_t)n;
    }
    return 0;
#endif
}
static int no_rng(void *,unsigned char *,size_t) { return CHOS_KEY_NO_ENTROPY; }
static int export_check(const CryptoKeyCase& spec,chos_webcore_key *key,unsigned index,bool instrument,const char *dump) {
    uint8_t *bytes=nullptr;size_t length=0;
    CHECK(!chos_webcore_key_export_spki(key,&bytes,&length));
    CHECK(length==spec.spki.size&&!memcmp(bytes,spec.spki.data,length));
    chos_webcore_key_free_bytes(bytes,length);
    chos_webcore_key_info info;CHECK(!chos_webcore_key_get_info(key,&info));
    if(!info.is_private)return 0;
    CHECK(!chos_webcore_key_export_pkcs8(key,&bytes,&length)&&length);
#ifdef KEY_HAVE_EXPORT_ORACLE
    CHECK(index<KEY_EXPORT_ORACLE_COUNT);
    const auto& oracle = key_export_oracles[index];
    size_t common = length < oracle.size ? length : oracle.size;
    size_t offset = 0;
    while (offset < common && bytes[offset] == oracle.data[offset])
        ++offset;
    if (offset != common || length != oracle.size)
        printf("[KEY DER] case=%s bytes=%zu expected=%zu first difference=%zu\n", spec.name, length, oracle.size, offset);
    CHECK(length == oracle.size && offset == common);
#else
    (void)index;
#endif
    int error=0;
    Key imported(chos_webcore_key_import_pkcs8(spec.family,spec.curve_bits,bytes,length,os_rng,nullptr,&error));
    CHECK(imported&&!error);
    uint8_t *pub=nullptr;size_t pub_length=0;
    CHECK(!chos_webcore_key_export_spki(imported.get(),&pub,&pub_length));
    CHECK(pub_length==spec.spki.size&&!memcmp(pub,spec.spki.data,pub_length));
    chos_webcore_key_free_bytes(pub,pub_length);
#ifndef __CHOS__
    if(dump){
        char name[1024];int n=snprintf(name,sizeof(name),"%s/%s.export.pkcs8.der",dump,spec.name);
        CHECK(n>0&&(size_t)n<sizeof(name));FILE *f=fopen(name,"wb");CHECK(f);
        CHECK(fwrite(bytes,1,length,f)==length&&!fclose(f));
    }
#else
    (void)dump;
#endif
    chos_webcore_key_free_bytes(bytes,length);
    if(instrument)++checked_exports;
    return 0;
}
static int cases(bool instrument,const char *dump=nullptr) {
    for(unsigned i=0;i<KEY_CASE_COUNT;++i){
        const auto& spec=key_cases[i];int error=0;
        Key public_key(chos_webcore_key_import_spki(spec.family,spec.curve_bits,spec.spki.data,spec.spki.size,&error));
        CHECK(public_key&&!error);chos_webcore_key_info info;
        CHECK(!chos_webcore_key_get_info(public_key.get(),&info));
        CHECK(info.family==spec.family&&info.curve==spec.curve_bits&&info.bits==spec.bits&&!info.is_private);
        CHECK(!export_check(spec,public_key.get(),i,instrument,nullptr));
        uint8_t *unchanged=(uint8_t *)(uintptr_t)1;size_t unchanged_size=97;
        CHECK(chos_webcore_key_export_pkcs8(public_key.get(),&unchanged,&unchanged_size)==CHOS_KEY_BAD_INPUT);
        CHECK(unchanged==(uint8_t *)(uintptr_t)1&&unchanged_size==97);
        Key private_key(chos_webcore_key_import_pkcs8(spec.family,spec.curve_bits,spec.pkcs8.data,spec.pkcs8.size,os_rng,nullptr,&error));
        CHECK(private_key&&!error);CHECK(!chos_webcore_key_get_info(private_key.get(),&info));
        CHECK(info.family==spec.family&&info.curve==spec.curve_bits&&info.bits==spec.bits&&info.is_private);
        Key moved=std::move(private_key);CHECK(!private_key&&moved);
        CHECK(!export_check(spec,moved.get(),i,instrument,dump));
        moved.reset();public_key.reset();
        if(instrument)checked_keys+=2;
    }
    return 0;
}
static int errors() {
    for(const auto& spec:rejected_key_cases){
        int error=0;Key key(spec.format==KEY_FIXTURE_SPKI
            ?chos_webcore_key_import_spki(spec.family,spec.curve_bits,spec.input.data,spec.input.size,&error)
            :chos_webcore_key_import_pkcs8(spec.family,spec.curve_bits,spec.input.data,spec.input.size,os_rng,nullptr,&error));
        CHECK(!key&&error);++refusals;
    }
    const auto& spec=key_cases[1]; // Actual P-256 private consistency requires RNG.
    int error=0;
    Key no_entropy(chos_webcore_key_import_pkcs8(spec.family,spec.curve_bits,spec.pkcs8.data,spec.pkcs8.size,no_rng,nullptr,&error));
    CHECK(!no_entropy&&error);
    Key missing_callback(chos_webcore_key_import_pkcs8(spec.family,spec.curve_bits,spec.pkcs8.data,spec.pkcs8.size,nullptr,nullptr,&error));
    CHECK(!missing_callback&&error);refusals+=2;
    CHECK(!chos_webcore_key_import_spki(999,0,spec.spki.data,spec.spki.size,&error)&&error);
    CHECK(!chos_webcore_key_import_spki(spec.family,spec.curve_bits,spec.spki.data,65537,&error)&&error);
    chos_webcore_key_info info;memset(&info,0xa5,sizeof(info));auto saved=info;
    CHECK(chos_webcore_key_get_info(nullptr,&info)==CHOS_KEY_BAD_INPUT&&!memcmp(&info,&saved,sizeof(info)));
    uint8_t *out=(uint8_t *)(uintptr_t)1;size_t bytes=97;
    CHECK(chos_webcore_key_export_spki(nullptr,&out,&bytes)==CHOS_KEY_BAD_INPUT&&out==(uint8_t *)(uintptr_t)1&&bytes==97);
    chos_webcore_key_destroy(nullptr);chos_webcore_key_free_bytes(nullptr,0);return 0;
}
// Replay one real entropy stream in this allocation-fault test. With fresh
// blinding values on every attempt, rejection sampling can change the number
// of allocations and the requested fault index may never be reached.
struct SweepEntropy {
    unsigned char bytes[8192];
    size_t used;
    size_t position;
    bool recording;
};

static int sweep_rng(void *closure, unsigned char *output, size_t length)
{
    auto& stream = *static_cast<SweepEntropy*>(closure);
    if (stream.recording) {
        if (length > sizeof(stream.bytes) - stream.used || os_rng(nullptr, output, length))
            return CHOS_KEY_NO_ENTROPY;
        memcpy(stream.bytes + stream.used, output, length);
        stream.used += length;
        return 0;
    }
    if (length > stream.used - stream.position)
        return CHOS_KEY_NO_ENTROPY;
    memcpy(output, stream.bytes + stream.position, length);
    stream.position += length;
    return 0;
}

static int sweep_import(const CryptoKeyCase& spec,bool private_key) {
    SweepEntropy stream = {};
    stream.recording = true;
    key_fault_begin(0);int error=0;
    { Key key(private_key?chos_webcore_key_import_pkcs8(spec.family,spec.curve_bits,spec.pkcs8.data,spec.pkcs8.size,sweep_rng,&stream,&error)
        :chos_webcore_key_import_spki(spec.family,spec.curve_bits,spec.spki.data,spec.spki.size,&error));CHECK(key&&!error); }
    size_t attempts=key_fault_end();CHECK(attempts>0&&!key_fault_live());
    stream.recording = false;
    for(size_t n=1;n<=attempts;++n){
        stream.position = 0;
        key_fault_begin(n);error=0;
        { Key key(private_key?chos_webcore_key_import_pkcs8(spec.family,spec.curve_bits,spec.pkcs8.data,spec.pkcs8.size,sweep_rng,&stream,&error)
            :chos_webcore_key_import_spki(spec.family,spec.curve_bits,spec.spki.data,spec.spki.size,&error));
          if(!key)CHECK(error); }
        key_fault_end();CHECK(key_fault_injected()&&!key_fault_live());++allocation_trials;
    }
    return 0;
}
static int sweep_export(const CryptoKeyCase& spec) {
    int error=0;Key key(chos_webcore_key_import_pkcs8(spec.family,spec.curve_bits,spec.pkcs8.data,spec.pkcs8.size,os_rng,nullptr,&error));CHECK(key&&!error);
    key_fault_begin(0);uint8_t *bytes=nullptr;size_t length=0;
    CHECK(!chos_webcore_key_export_pkcs8(key.get(),&bytes,&length));chos_webcore_key_free_bytes(bytes,length);
    size_t attempts=key_fault_end();CHECK(attempts>0&&!key_fault_live());
    for(size_t n=1;n<=attempts;++n){
        key_fault_begin(n);bytes=(uint8_t *)(uintptr_t)1;length=97;
        int rc=chos_webcore_key_export_pkcs8(key.get(),&bytes,&length);
        if(!rc)chos_webcore_key_free_bytes(bytes,length);else CHECK(!bytes&&!length);
        key_fault_end();CHECK(key_fault_injected()&&!key_fault_live());++allocation_trials;
    }
    return 0;
}
static void *worker(void *) { return (void *)(uintptr_t)cases(false); }
struct SharedKey { const CryptoKeyCase *spec; chos_webcore_key *key; unsigned index; };
static SharedKey shared_keys[KEY_CASE_COUNT];
static void *shared_worker(void *) {
    for(unsigned round=0;round<3;++round)for(const auto& item:shared_keys){
        chos_webcore_key_info info;
        if(chos_webcore_key_get_info(item.key,&info)||!info.is_private)return (void*)1;
        uint8_t *bytes=nullptr;size_t length=0;
        if(chos_webcore_key_export_spki(item.key,&bytes,&length))return (void*)2;
        int mismatch=length!=item.spec->spki.size||memcmp(bytes,item.spec->spki.data,length);
        chos_webcore_key_free_bytes(bytes,length);if(mismatch)return (void*)3;
        if(chos_webcore_key_export_pkcs8(item.key,&bytes,&length))return (void*)4;
#ifdef KEY_HAVE_EXPORT_ORACLE
        mismatch=length!=key_export_oracles[item.index].size||memcmp(bytes,key_export_oracles[item.index].data,length);
#endif
        chos_webcore_key_free_bytes(bytes,length);if(mismatch)return (void*)5;
    }
    return nullptr;
}
static int workers() {
    pthread_t threads[3];pthread_attr_t attr;CHECK(!pthread_attr_init(&attr)&&!pthread_attr_setstacksize(&attr,128*1024));
    for(unsigned i=0;i<3;++i)CHECK(!pthread_create(&threads[i],&attr,worker,nullptr));
    for(unsigned i=0;i<3;++i){void *result=nullptr;CHECK(!pthread_join(threads[i],&result)&&!result);}
    Key retained[KEY_CASE_COUNT];
    for(unsigned i=0;i<KEY_CASE_COUNT;++i){
        const auto& spec=key_cases[i];int error=0;
        retained[i].reset(chos_webcore_key_import_pkcs8(spec.family,spec.curve_bits,spec.pkcs8.data,spec.pkcs8.size,os_rng,nullptr,&error));
        CHECK(retained[i]&&!error);shared_keys[i]={&spec,retained[i].get(),i};
    }
    for(unsigned i=0;i<3;++i)CHECK(!pthread_create(&threads[i],&attr,shared_worker,nullptr));
    for(unsigned i=0;i<3;++i){void *result=nullptr;CHECK(!pthread_join(threads[i],&result)&&!result);}
    CHECK(!pthread_attr_destroy(&attr));return 0;
}
int main(int argc,char **argv) {
    puts("[KEY PROFILE] actual SPKI/PKCS8 ownership; TEST-ONLY keys; no signatures or key generation claimed");
    const char *dump=argc==2?argv[1]:nullptr;
    CHECK(!cases(true,dump)&&!errors());
    CHECK(!sweep_import(key_cases[0],false)&&!sweep_import(key_cases[1],false)&&!sweep_import(key_cases[0],true));
#ifndef __CHOS__
    // The complete P-256 private sweep has more than 25,000 allocation points.
    // Host sanitizers exercise every point; native runs retain all normal EC
    // paths and deterministic RSA/private + EC/public failure coverage.
    CHECK(!sweep_import(key_cases[1],true));
#endif
    CHECK(!sweep_export(key_cases[0])&&!sweep_export(key_cases[1]));
    CHECK(!workers());
#ifdef __CHOS__
    chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&!stats.allocations&&!stats.mapped_bytes);
#endif
    printf("[KEY CHECKS] keys=%zu exports=%zu refusals=%zu allocation_trials=%zu\n",checked_keys,checked_exports,refusals,allocation_trials);
    puts("[ OK ] actual EC RSA key imports exports failures threads and cleanup passed");return 0;
}

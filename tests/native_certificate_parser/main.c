#include "CertificateBackend.h"
#include "certificate-cases.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#ifdef __CHOS__
#include <chos/libc.h>
#endif
#define CHECK(x) do {if(!(x)){printf("[FAIL] certificate parser line %u\n",__LINE__);fflush(stdout);return 1;}}while(0)
static size_t valid_cases,refusals,allocation_trials;
typedef struct {void *pointer;size_t size;} Block;
static struct {int active,failed;size_t attempts,fail_at,live;Block blocks[512];} ledger;
extern void *__real_malloc(size_t);
extern void *__real_calloc(size_t,size_t);
extern void *__real_realloc(void *,size_t);
extern void __real_free(void *);
static int reject(void){if(!ledger.active)return 0;++ledger.attempts;if(ledger.attempts==ledger.fail_at){ledger.failed=1;return 1;}return 0;}
static void remember(void *p,size_t bytes){if(!ledger.active||!p)return;unsigned n=0;while(n<512&&ledger.blocks[n].pointer)++n;if(n==512)abort();ledger.blocks[n]=(Block){p,bytes};ledger.live+=bytes;}
static void forget(void *p){if(!ledger.active||!p)return;for(unsigned n=0;n<512;++n)if(ledger.blocks[n].pointer==p){ledger.live-=ledger.blocks[n].size;ledger.blocks[n]=(Block){0};return;}}
void *__wrap_malloc(size_t bytes){if(reject())return NULL;void *p=__real_malloc(bytes);remember(p,bytes);return p;}
void *__wrap_calloc(size_t n,size_t bytes){if(reject()||(bytes&&n>SIZE_MAX/bytes))return NULL;void *p=__real_calloc(n,bytes);remember(p,n*bytes);return p;}
void *__wrap_realloc(void *old,size_t bytes){if(!ledger.active)return __real_realloc(old,bytes);if(reject())return NULL;unsigned slot=0;while(slot<512&&ledger.blocks[slot].pointer!=old)++slot;void *p=__real_realloc(old,bytes);if(p||!bytes){if(slot<512){ledger.live-=ledger.blocks[slot].size;ledger.blocks[slot]=(Block){0};}remember(p,bytes);}return p;}
void __wrap_free(void *p){forget(p);__real_free(p);}

static int inspect(const struct CertificateCase *spec)
{
    chos_certificate *parsed=chos_certificate_parse(spec->der,spec->size);
    if(!spec->accepted){CHECK(!parsed);return 0;}
    CHECK(parsed);
    size_t length=0;int tag=0;
    const unsigned char *subject=chos_certificate_subject(parsed,&length,&tag);
    CHECK(subject&&length==spec->subject_size&&!memcmp(subject,spec->subject,length));
    CHECK(tag==12||tag==19||tag==20||tag==22||tag==30);
    CHECK(chos_certificate_valid_from(parsed)==spec->from&&chos_certificate_valid_until(parsed)==spec->until);
    CHECK(chos_certificate_signature_is_sha1(parsed)==spec->sha1);
    CHECK(chos_certificate_san_count(parsed)==spec->san_count);
    for(size_t i=0;i<spec->san_count;++i){int type=0;const unsigned char *bytes=chos_certificate_san(parsed,i,&length,&type);
        CHECK(bytes&&type==spec->sans[i].type&&length==spec->sans[i].size&&!memcmp(bytes,spec->sans[i].data,length));}
    CHECK(!chos_certificate_san(parsed,spec->san_count,&length,&tag));
    chos_certificate_destroy(parsed);return 0;
}
static int positives(void){for(size_t i=0;i<CERTIFICATE_CASE_COUNT;++i){CHECK(!inspect(&certificate_cases[i]));++valid_cases;}return 0;}
static int negatives(void)
{
    CHECK(!chos_certificate_parse(NULL,4)&&!chos_certificate_parse((const uint8_t *)"",0));refusals+=2;
    CHECK(!chos_certificate_parse(certificate_cases[0].der,CHOS_CERTIFICATE_MAX_DER_BYTES+1));++refusals;
    CHECK(!chos_certificate_parse(certificate_pem,sizeof(certificate_pem)));++refusals;
    for(size_t i=0;i<CERTIFICATE_CASE_COUNT;++i){const struct CertificateCase *s=&certificate_cases[i];if(!s->accepted)continue;
        for(size_t n=1;n<s->size;n+=31){CHECK(!chos_certificate_parse(s->der,n));++refusals;}
        CHECK(!chos_certificate_parse(s->der,s->size-1));++refusals;
        unsigned char *copy=malloc(s->size+1);CHECK(copy);memcpy(copy,s->der,s->size);copy[s->size]=0;
        CHECK(!chos_certificate_parse(copy,s->size+1));++refusals;
        copy[0]=0x31;CHECK(!chos_certificate_parse(copy,s->size));++refusals;free(copy);
    }
    return 0;
}
static int fail_allocations(void)
{
    ledger.active=1;ledger.attempts=0;
    chos_certificate *parsed=chos_certificate_parse(certificate_cases[0].der,certificate_cases[0].size);CHECK(parsed);
    chos_certificate_destroy(parsed);CHECK(!ledger.live);size_t count=ledger.attempts;
    for(size_t n=1;n<=count;++n){ledger.attempts=0;ledger.failed=0;ledger.fail_at=n;
        parsed=chos_certificate_parse(certificate_cases[0].der,certificate_cases[0].size);
        CHECK(ledger.failed&&!parsed&&!ledger.live);++allocation_trials;
    }
    ledger.active=0;ledger.fail_at=0;return 0;
}
static void *worker(void *unused){(void)unused;for(unsigned n=0;n<10;++n)for(size_t i=0;i<CERTIFICATE_CASE_COUNT;++i)if(inspect(&certificate_cases[i]))return (void *)1;return NULL;}
static int workers(void)
{
    pthread_t threads[3];pthread_attr_t attr;CHECK(!pthread_attr_init(&attr)&&!pthread_attr_setstacksize(&attr,128*1024));
    for(unsigned i=0;i<3;++i)CHECK(!pthread_create(&threads[i],&attr,worker,NULL));
    for(unsigned i=0;i<3;++i){void *result=NULL;CHECK(!pthread_join(threads[i],&result)&&!result);}
    CHECK(!pthread_attr_destroy(&attr));return 0;
}
int main(void)
{
    CHECK(!positives()&&!negatives()&&!fail_allocations()&&!workers());
    printf("[ OK ] real MbedTLS certificate metadata cases=%zu refusals=%zu allocation_trials=%zu workers=3 rounds=10\n",valid_cases,refusals,allocation_trials);
#ifdef __CHOS__
    chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&!stats.allocations&&!stats.mapped_bytes);
#endif
    puts("[ OK ] certificate parser resources cleaned up");return 0;
}

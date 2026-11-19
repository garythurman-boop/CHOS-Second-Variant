#include "DigestBackend.h"
#include "cases.h"
#include "faults.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){printf("[FAIL] digest backend line %u\n",__LINE__);return 1;}}while(0)
void digest_case_input(unsigned kind,size_t offset,uint8_t*out,size_t n){for(size_t i=0;i<n;++i){size_t p=offset+i;out[i]=kind==1?(uint8_t)"abc"[p]:kind==2?'a':(uint8_t)(p*73+(p>>8)*19+31);}}
int digest_backend_tests(void){
    static const size_t fragments[]={1,7,4096};
    for(size_t i=0;i<DIGEST_CASE_COUNT;++i){const struct digest_case*c=&digest_cases[i];
        for(size_t f=0;f<3;++f){if(c->length>65537&&f<2)continue;
            struct chos_digest*d=chos_digest_create((enum chos_digest_algorithm)c->algorithm);CHECK(d);CHECK(!chos_digest_add(d,NULL,0));
            uint8_t data[4096],result[64],again[64];size_t n=0,again_n=0;
            for(size_t p=0;p<c->length;){size_t count=c->length-p;if(count>fragments[f])count=fragments[f];digest_case_input(c->kind,p,data,count);CHECK(!chos_digest_add(d,data,count));p+=count;}
            CHECK(!chos_digest_finish(d,result,&n)&&n==c->digest_bytes&&!memcmp(result,c->expected,n));
            CHECK(!chos_digest_finish(d,again,&again_n)&&again_n==n&&!memcmp(result,again,n));
            CHECK(chos_digest_add(d,data,1));CHECK(chos_digest_finish(d,result,&n)&&!n);for(unsigned j=0;j<64;++j)CHECK(!result[j]);chos_digest_destroy(d);
        }
    }
    CHECK(!chos_digest_create((enum chos_digest_algorithm)99));
    for(unsigned a=1;a<=5;++a){for(unsigned operation=1;operation<=3;++operation){
        digest_arm_fault(operation==1?digest_fault_id(a,1):0);
        struct chos_digest*d=chos_digest_create((enum chos_digest_algorithm)a);
        if(operation==1){CHECK(!d&&digest_fault_hits()==1);continue;}
        CHECK(d);uint8_t result[64];size_t n=999;
        digest_arm_fault(digest_fault_id(a,operation));int rc=chos_digest_add(d,(const uint8_t*)"abc",3);
        CHECK(operation==2?rc!=0:rc==0);CHECK(chos_digest_finish(d,result,&n)&&!n&&digest_fault_hits()==1);chos_digest_destroy(d);
    }}
    digest_arm_fault(10);CHECK(!chos_digest_create(CHOS_DIGEST_SHA256)&&digest_fault_hits()==1);digest_arm_fault(0);
    struct chos_digest*d=chos_digest_create(CHOS_DIGEST_SHA256);CHECK(d&&chos_digest_add(d,NULL,1));uint8_t result[64];size_t n=1;CHECK(chos_digest_finish(d,result,&n)&&!n);chos_digest_destroy(d);
    puts("[ OK ] real MbedTLS five digests,80 reference cases,fragmentation and failures passed");return 0;
}

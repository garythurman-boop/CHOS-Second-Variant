#include <stddef.h>
#include <stdint.h>
#include "faults.h"
#include "DigestBackend.h"
#include "cases.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
static void*worker(void*arg){unsigned algorithm=(unsigned)(uintptr_t)arg;const struct digest_case*c=NULL;for(size_t i=0;i<DIGEST_CASE_COUNT;++i)if(digest_cases[i].algorithm==algorithm&&digest_cases[i].kind==1)c=&digest_cases[i];if(!c)return(void*)1;for(unsigned i=0;i<100;++i){struct chos_digest*d=chos_digest_create((enum chos_digest_algorithm)algorithm);uint8_t out[64];size_t n;if(!d||chos_digest_add(d,(const uint8_t*)"abc",3)||chos_digest_finish(d,out,&n)||n!=c->digest_bytes||memcmp(out,c->expected,n)){chos_digest_destroy(d);return(void*)1;}chos_digest_destroy(d);}return NULL;}
int main(void){if(digest_backend_tests())return 1;pthread_t t[3];for(unsigned i=0;i<3;++i)if(pthread_create(t+i,NULL,worker,(void*)(uintptr_t)(i+1)))return 1;for(unsigned i=0;i<3;++i){void*r;if(pthread_join(t[i],&r)||r)return 1;}puts("[ OK ] real digest backend threaded cleanup passed");return 0;}

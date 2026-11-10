#define _GNU_SOURCE 1
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include <alloca.h>
#ifdef CHOS_STDLIB_HOST
#include <stdio.h>
#define SAY(x) puts(x)
#else
#include <chos/user.h>
#define SAY(x) chos_write(x,sizeof(x)-1)
#endif
#define CHECK(x) do{if(!(x)){SAY("[FAIL] native string/random/recursive-lock runtime\n");return 1;}}while(0)
static int sequential[6000],parallel[6000];
static void *worker(void *argument){
    unsigned index=(uintptr_t)argument;
    for(unsigned i=0;i<2000;++i)parallel[index*2000+i]=rand();
    return NULL;
}
static int compare(const void *a,const void *b){int x=*(const int*)a,y=*(const int*)b;return (x>y)-(x<y);}
int main(void){
    const unsigned seeds[]={0,1,42,UINT32_MAX};
    for(unsigned s=0;s<4;++s){
        srand(seeds[s]);for(unsigned i=0;i<6000;++i){sequential[i]=rand();CHECK(sequential[i]>=0&&sequential[i]<=RAND_MAX);}
        srand(seeds[s]);for(unsigned i=0;i<6000;++i)CHECK(rand()==sequential[i]);
        srand(seeds[s]);pthread_t threads[3];
        for(unsigned i=0;i<3;++i)CHECK(!pthread_create(&threads[i],NULL,worker,(void*)(uintptr_t)i));
        for(unsigned i=0;i<3;++i)CHECK(!pthread_join(threads[i],NULL));
        qsort(sequential,6000,sizeof(int),compare);qsort(parallel,6000,sizeof(int),compare);
        CHECK(!memcmp(sequential,parallel,sizeof(sequential)));
    }
    pthread_mutex_t recursive=PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
    CHECK(!pthread_mutex_lock(&recursive)&&!pthread_mutex_lock(&recursive)&&!pthread_mutex_trylock(&recursive));
    CHECK(!pthread_mutex_unlock(&recursive)&&!pthread_mutex_unlock(&recursive)&&!pthread_mutex_unlock(&recursive));
    CHECK(!pthread_mutex_destroy(&recursive));
    void *pages=mmap(NULL,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);CHECK(pages!=MAP_FAILED);
    CHECK(!mprotect((unsigned char*)pages+4096,4096,PROT_NONE));char *tail=(char*)pages+4096-32;
    memset(tail,'z',32);char *bounded=strndup(tail,32);CHECK(bounded&&strlen(bounded)==32&&!memcmp(bounded,tail,32));
    memset(tail,'q',32);CHECK(bounded[0]=='z');free(bounded);
    bounded=strndup((char*)pages+4096,0);CHECK(bounded&&!*bounded);free(bounded);
    tail[31]=0;char *full=strdup(tail);CHECK(full&&strlen(full)==31&&!memcmp(full,tail,32));free(full);
    CHECK(!munmap(pages,8192));
    volatile size_t stack_length=67;
    unsigned char *stack_bytes=alloca(stack_length);
    CHECK(((uintptr_t)stack_bytes&15u)==0);
    memset(stack_bytes,0x6b,stack_length);
    for(size_t i=0;i<stack_length;++i)CHECK(stack_bytes[i]==0x6b);
    SAY("[ OK ] native bounded string copies,atomic noncrypto rand and static recursive mutex passed\n");return 0;
}

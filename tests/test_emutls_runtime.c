/* Exercise unmodified upstream storage/indexing with real host pthreads under
 * sanitizers. CHOS adapters/compiler ABI are separately exercised by the guest. */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
typedef struct {uintptr_t size,align,index;void *value;} control_t;
extern void *chos_test_emutls_get_address(control_t *);
static control_t controls[64];
static uint64_t initial[64];
static void *worker(void *p){
    uintptr_t id=(uintptr_t)p;
    for(unsigned i=0;i<64;i++){
        uint64_t *value=chos_test_emutls_get_address(&controls[i]);
        assert(!((uintptr_t)value&(controls[i].align-1))&&*value==initial[i]);
        *value=initial[i]+id;
    }
    for(unsigned n=0;n<100;n++)for(unsigned i=0;i<64;i++){
        uint64_t *value=chos_test_emutls_get_address(&controls[i]);assert(*value==initial[i]+id);
    }
    return 0;
}
int main(void){
    for(unsigned i=0;i<64;i++){initial[i]=100u*i;controls[i]=(control_t){sizeof(uint64_t),i&1u?4096:16,0,&initial[i]};}
    for(unsigned run=0;run<4;run++){
        pthread_t threads[4];
        for(unsigned i=0;i<4;i++)assert(!pthread_create(&threads[i],0,worker,(void *)(uintptr_t)(i+1)));
        for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],0));
    }
    for(unsigned i=0;i<64;i++)assert(controls[i].index&&controls[i].index<=64);
    puts("compiler-rt emutls: actual host pthread isolation,64 controls,alignment,array growth and destructor reclamation passed");
    return 0;
}

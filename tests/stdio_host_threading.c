/* Host thread adapter compiled against host headers, never CHOS pthread ABI. */
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
struct args { int (*function)(void *); void *argument; };
static void *entry(void *p) { struct args *a=p; return (void *)(intptr_t)a->function(a->argument); }
void chos_host_yield(void) { sched_yield(); }
int chos_host_readers(int (*function)(void *),void *argument) {
    pthread_t threads[3]; struct args args={function,argument}; unsigned n=0; int failed=0;
    for (;n<3;n++) if (pthread_create(&threads[n],NULL,entry,&args)) { failed=1; break; }
    for (unsigned i=0;i<n;i++) { void *result=NULL; if (pthread_join(threads[i],&result) || result) failed=1; }
    return failed;
}

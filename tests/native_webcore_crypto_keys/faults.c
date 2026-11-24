#include "faults.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Sweeps are serial and all tracked blocks are released before pthread workers
 * begin. A lazy emulated-TLS malloc ledger would recurse while creating itself. */
typedef struct { void *pointer; size_t bytes; } Block;
static struct { int active,injected;size_t attempts,fail_at,live;Block blocks[1024]; } ledger;
extern void *__real_malloc(size_t);
extern void *__real_calloc(size_t,size_t);
extern void *__real_realloc(void *,size_t);
extern void __real_free(void *);
static int reject(void) {
    if(!ledger.active)return 0;
    if(++ledger.attempts==ledger.fail_at){ledger.injected=1;return 1;}
    return 0;
}
static void remember(void *p,size_t n) {
    if(!ledger.active||!p)return;
    for(unsigned i=0;i<1024;++i)if(!ledger.blocks[i].pointer){ledger.blocks[i]=(Block){p,n};ledger.live+=n;return;}
    abort();
}
static void forget(void *p) {
    if(!p)return;
    for(unsigned i=0;i<1024;++i)if(ledger.blocks[i].pointer==p){ledger.live-=ledger.blocks[i].bytes;ledger.blocks[i]=(Block){0};return;}
}
void *__wrap_malloc(size_t n) { if(reject())return NULL;void *p=__real_malloc(n);remember(p,n);return p; }
void *__wrap_calloc(size_t n,size_t s) { if(reject()||(s&&n>SIZE_MAX/s))return NULL;void *p=__real_calloc(n,s);remember(p,n*s);return p; }
void *__wrap_realloc(void *old,size_t n) {
    if(!ledger.active)return __real_realloc(old,n);
    if(reject())return NULL;
    void *p=__real_realloc(old,n);
    if(p||!n){forget(old);remember(p,n);}
    return p;
}
void __wrap_free(void *p) { if(ledger.active)forget(p);__real_free(p); }
void key_fault_begin(size_t n) { if(ledger.active||ledger.live)abort();memset(&ledger,0,sizeof(ledger));ledger.active=1;ledger.fail_at=n; }
size_t key_fault_end(void) { if(!ledger.active)abort();ledger.active=0;return ledger.attempts; }
int key_fault_injected(void) { return ledger.injected; }
size_t key_fault_live(void) { return ledger.live; }

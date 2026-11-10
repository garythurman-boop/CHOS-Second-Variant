#include <stdlib.h>
#include <stdint.h>
/* musl 1.2.5's deterministic rand sequence, with atomic state publication for
 * CHOS threads. Original musl MIT notices: third_party/musl/COPYRIGHT.
 * This is not entropy and must never seed TLS, credentials or security tokens.
 */
static uint64_t state;
void srand(unsigned seed){__atomic_store_n(&state,(uint64_t)seed-1,__ATOMIC_RELAXED);}
int rand(void){
    uint64_t old=__atomic_load_n(&state,__ATOMIC_RELAXED),next;
    do {next=old*UINT64_C(6364136223846793005)+1;}
    while(!__atomic_compare_exchange_n(&state,&old,next,1,__ATOMIC_RELAXED,__ATOMIC_RELAXED));
    return (int)(next>>33);
}

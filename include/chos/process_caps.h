#ifndef CHOS_PROCESS_CAPS_H
#define CHOS_PROCESS_CAPS_H
#include <stdint.h>
#define CHOS_PROCESS_SYSCALL_LIMIT 62u
_Static_assert(CHOS_PROCESS_SYSCALL_LIMIT<63u,"syscall permissions must fit a positive64-bit mask");
#define CHOS_PROCESS_CAP(n) (UINT64_C(1)<<(n))
#define CHOS_PROCESS_CAP_ALL ((UINT64_C(1)<<(CHOS_PROCESS_SYSCALL_LIMIT+1u))-2u)
/* Process/thread EXIT and capability inspection remain available to every process. */
#define CHOS_PROCESS_CAP_REQUIRED (CHOS_PROCESS_CAP(2)|CHOS_PROCESS_CAP(55)|CHOS_PROCESS_CAP(59))
static inline int chos_process_cap_reduction(uint64_t parent,uint64_t requested,uint64_t *effective){
 if(!effective || (requested&~CHOS_PROCESS_CAP_ALL) || ((requested|CHOS_PROCESS_CAP_REQUIRED)&~parent))return 0;
 *effective=requested|CHOS_PROCESS_CAP_REQUIRED;return 1;
}
#endif

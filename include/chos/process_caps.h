#ifndef CHOS_PROCESS_CAPS_H
#define CHOS_PROCESS_CAPS_H
#include <stdint.h>
#include "chos/syscall.h"
/* Permission bits1..62 keep the positive signed-result ABI. SEEK63 uses
 * the existing READ9 permission class rather than consuming the sign bit. */
#define CHOS_PROCESS_SYSCALL_LIMIT 62u
_Static_assert(CHOS_PROCESS_SYSCALL_LIMIT<63u,"syscall permissions must fit a positive64-bit mask");
#define CHOS_PROCESS_CAP(n) (UINT64_C(1)<<(n))
#define CHOS_PROCESS_CAP_SEEK CHOS_PROCESS_CAP(CHOS_SYS_READ)
static inline uint64_t chos_process_syscall_capability(uint32_t number){
 if(number==CHOS_SYS_SEEK)return CHOS_PROCESS_CAP_SEEK;
 return number&&number<=CHOS_PROCESS_SYSCALL_LIMIT?CHOS_PROCESS_CAP(number):0;
}
#define CHOS_PROCESS_CAP_ALL ((UINT64_C(1)<<(CHOS_PROCESS_SYSCALL_LIMIT+1u))-2u)
/* Process/thread EXIT and capability inspection remain available to every process. */
#define CHOS_PROCESS_CAP_REQUIRED (CHOS_PROCESS_CAP(2)|CHOS_PROCESS_CAP(55)|CHOS_PROCESS_CAP(59))
static inline int chos_process_cap_reduction(uint64_t parent,uint64_t requested,uint64_t *effective){
 if(!effective || (requested&~CHOS_PROCESS_CAP_ALL) || ((requested|CHOS_PROCESS_CAP_REQUIRED)&~parent))return 0;
 *effective=requested|CHOS_PROCESS_CAP_REQUIRED;return 1;
}
#endif

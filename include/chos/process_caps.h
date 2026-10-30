#ifndef CHOS_PROCESS_CAPS_H
#define CHOS_PROCESS_CAPS_H
#include <stdint.h>
#include "chos/syscall.h"
/* Permission bits1..62 keep the positive signed-result ABI. SEEK63 uses
 * the existing READ9 permission class rather than consuming the sign bit.
 * MEMORY_CONTROL64/UNMAP_RANGE65 share MAP35/UNMAP36; current-thread context66
 * and self-resource snapshot67 share the read-only THREAD_INFO57 class. */
#define CHOS_PROCESS_SYSCALL_LIMIT 62u
_Static_assert(CHOS_PROCESS_SYSCALL_LIMIT<63u,"syscall permissions must fit a positive64-bit mask");
#define CHOS_PROCESS_CAP(n) (UINT64_C(1)<<(n))
#define CHOS_PROCESS_CAP_SEEK CHOS_PROCESS_CAP(CHOS_SYS_READ)
static inline uint64_t chos_process_syscall_capability(uint32_t number){
 if(number==CHOS_SYS_DIRECTORY_PAGE)return CHOS_PROCESS_CAP(CHOS_SYS_LIST_DIR);
 if(number==CHOS_SYS_FILE_METADATA||number==CHOS_SYS_PATH_METADATA)return CHOS_PROCESS_CAP(CHOS_SYS_READ);
 if(number>=CHOS_SYS_FILE_OPEN_WRITE&&number<=CHOS_SYS_FILE_FLUSH)return CHOS_PROCESS_CAP(CHOS_SYS_WRITE_FILE);
 if(number==CHOS_SYS_FILE_ABORT)return CHOS_PROCESS_CAP(CHOS_SYS_CLOSE);
 if(number==CHOS_SYS_FILE_INFO||number==CHOS_SYS_READ_AT)return CHOS_PROCESS_CAP(CHOS_SYS_READ);
 if(number==CHOS_SYS_RESOURCE_INFO||number==CHOS_SYS_THREAD_CONTEXT)return CHOS_PROCESS_CAP(CHOS_SYS_THREAD_INFO);
 if(number==CHOS_SYS_SEEK)return CHOS_PROCESS_CAP_SEEK;
 if(number==CHOS_SYS_MEMORY_CONTROL)return CHOS_PROCESS_CAP(CHOS_SYS_MAP_MEMORY);
 if(number==CHOS_SYS_UNMAP_RANGE)return CHOS_PROCESS_CAP(CHOS_SYS_UNMAP_MEMORY);
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

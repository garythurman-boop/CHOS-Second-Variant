#ifndef CHOS_THREAD_PAUSE_H
#define CHOS_THREAD_PAUSE_H
#include "chos/syscall.h"
#include <stdint.h>
#define CHOS_THREAD_PAUSE_DEPTH_MAX 32u
/* Kernel-only ownership policy; caller serializes the state. One controller per
 * process prevents a second controller freezing the owner of other pauses. */
typedef struct {uint32_t controller,references;} chos_pause_owner_t;
static inline int chos_pause_can_acquire(const chos_pause_owner_t *o,uint32_t caller,uint32_t depth){
 if(!caller)return -CHOS_ERRNO_EINVAL;
 if(o->controller && o->controller!=caller)return -CHOS_ERRNO_EBUSY;
 if(depth>=CHOS_THREAD_PAUSE_DEPTH_MAX || o->references==UINT32_MAX)return -CHOS_ERRNO_EOVERFLOW;
 return 0;
}
static inline int chos_pause_acquire(chos_pause_owner_t *o,uint32_t caller,uint32_t *depth){
 int rc=chos_pause_can_acquire(o,caller,*depth);if(rc)return rc;
 o->controller=caller;++o->references;++*depth;return 0;
}
static inline int chos_pause_release(chos_pause_owner_t *o,uint32_t caller,uint32_t *depth){
 if(!caller || o->controller!=caller)return -CHOS_ERRNO_EPERM;
 if(!*depth || !o->references)return -CHOS_ERRNO_EINVAL;
 --*depth;if(!--o->references)o->controller=0;return 0;
}
#endif

#ifndef CHOS_THREAD_API_H
#define CHOS_THREAD_API_H
#include <stdint.h>
#include <stddef.h>
#define CHOS_THREAD_API_VERSION 1u
/* Version1 retains its original exit-time stack-unpin behavior. Version2 is
 * opt-in managed ownership: reserved[0] holds these flags; reserved[1] is zero.
 * Managed stacks remain pinned until collection or detached disposal. */
#define CHOS_THREAD_MANAGED_VERSION 2u
#define CHOS_THREAD_MANAGED_STACK UINT64_C(1)
#define CHOS_THREAD_CREATE_DETACHED UINT64_C(2)
#define CHOS_THREAD_MAX_PER_PROCESS 3u
#define CHOS_THREAD_STACK_MAX (128u*1024u)
#define CHOS_THREAD_QUEUED 1u
#define CHOS_THREAD_RUNNING 2u
#define CHOS_THREAD_EXITED 3u
#define CHOS_THREAD_FAILED 4u
typedef struct {
 uint32_t version,size_bytes;
 uint64_t entry,stack_base,stack_bytes,argument,tls;
 uint64_t reserved[2];
} chos_thread_request_t;
typedef struct {
 uint32_t version,size_bytes,tid,state;
 int64_t exit_status;
 uint64_t reserved;
} chos_thread_info_t;
_Static_assert(sizeof(chos_thread_request_t)==64u,"thread request ABI");
_Static_assert(sizeof(chos_thread_info_t)==32u,"thread status ABI");
static inline int chos_thread_request_valid(const chos_thread_request_t *r){
 return r&&r->size_bytes==sizeof(*r)&&!r->reserved[1]&&
  ((r->version==CHOS_THREAD_API_VERSION&&!r->reserved[0])||
   (r->version==CHOS_THREAD_MANAGED_VERSION&&(r->reserved[0]==CHOS_THREAD_MANAGED_STACK||
    r->reserved[0]==(CHOS_THREAD_MANAGED_STACK|CHOS_THREAD_CREATE_DETACHED))))&&
  r->entry&&!(r->entry&3u)&&r->stack_base&&!(r->stack_base&4095u)&&r->stack_bytes>=4096u&&
  r->stack_bytes<=CHOS_THREAD_STACK_MAX&&!(r->stack_bytes&4095u)&&r->stack_base<=UINT64_MAX-r->stack_bytes&&
  r->tls&&!(r->tls&15u)&&r->tls<=UINT64_MAX-32u;
}
static inline int chos_thread_request_managed(const chos_thread_request_t *r){return r->version==CHOS_THREAD_MANAGED_VERSION;}
#endif

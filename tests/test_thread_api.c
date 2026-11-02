#include <assert.h>
#include "chos/thread_api.h"
#include "chos/thread_pause.h"
static void test_pause_ownership(void){
 chos_pause_owner_t owner={0};uint32_t a=0,b=0;
 assert(chos_pause_acquire(&owner,0,&a)==-CHOS_ERRNO_EINVAL&&!owner.controller&&!a);
 for(unsigned i=0;i<CHOS_THREAD_PAUSE_DEPTH_MAX;i++)assert(!chos_pause_acquire(&owner,77,&a));
 assert(chos_pause_acquire(&owner,77,&a)==-CHOS_ERRNO_EOVERFLOW);
 assert(chos_pause_acquire(&owner,88,&b)==-CHOS_ERRNO_EBUSY&&!b);
 assert(chos_pause_release(&owner,88,&a)==-CHOS_ERRNO_EPERM);
 assert(!chos_pause_acquire(&owner,77,&b));
 for(unsigned i=0;i<CHOS_THREAD_PAUSE_DEPTH_MAX;i++)assert(!chos_pause_release(&owner,77,&a));
 assert(owner.controller==77&&owner.references==1);
 assert(chos_pause_release(&owner,77,&a)==-CHOS_ERRNO_EINVAL);
 assert(!chos_pause_release(&owner,77,&b)&&!owner.controller&&!owner.references);
 assert(!chos_pause_acquire(&owner,88,&a));assert(!chos_pause_release(&owner,88,&a));
}
int main(void){
 test_pause_ownership();
 chos_thread_request_t good={.version=1,.size_bytes=64,.entry=0x20000000,.stack_base=0x21001000,.stack_bytes=32768,.tls=0x20080000};
 assert(chos_thread_request_valid(&good));assert(!chos_thread_request_valid(0));
 chos_thread_request_t r=good;r.version=2;assert(!chos_thread_request_valid(&r));
 r=good;r.version=CHOS_THREAD_MANAGED_VERSION;r.reserved[0]=CHOS_THREAD_MANAGED_STACK;assert(chos_thread_request_valid(&r)&&chos_thread_request_managed(&r));
 r.reserved[0]|=CHOS_THREAD_CREATE_DETACHED;assert(chos_thread_request_valid(&r));
 r.reserved[0]=CHOS_THREAD_CREATE_DETACHED;assert(!chos_thread_request_valid(&r));
 r.reserved[0]=CHOS_THREAD_MANAGED_STACK|4;assert(!chos_thread_request_valid(&r));
 r.reserved[0]=CHOS_THREAD_MANAGED_STACK;r.reserved[1]=1;assert(!chos_thread_request_valid(&r));
 r=good;r.version=3;assert(!chos_thread_request_valid(&r));
 r=good;r.size_bytes=63;assert(!chos_thread_request_valid(&r));
 r=good;r.reserved[0]=1;assert(!chos_thread_request_valid(&r));
 r=good;r.reserved[1]=1;assert(!chos_thread_request_valid(&r));
 r=good;r.entry=0;assert(!chos_thread_request_valid(&r));r.entry=0x20000001;assert(!chos_thread_request_valid(&r));
 r=good;r.stack_base++;assert(!chos_thread_request_valid(&r));
 r=good;r.stack_base=UINT64_MAX-4095u;assert(!chos_thread_request_valid(&r));
 r=good;r.stack_bytes=0;assert(!chos_thread_request_valid(&r));
 r=good;r.stack_bytes=4095;assert(!chos_thread_request_valid(&r));
 r=good;r.stack_bytes=CHOS_THREAD_STACK_MAX+4096;assert(!chos_thread_request_valid(&r));
 r=good;r.tls=0;assert(!chos_thread_request_valid(&r));r.tls=0x20080001;assert(!chos_thread_request_valid(&r));
 r=good;r.tls=UINT64_MAX-15;assert(!chos_thread_request_valid(&r));
 r=good;r.stack_bytes=CHOS_THREAD_STACK_MAX;assert(chos_thread_request_valid(&r));
 return 0;
}

#include <assert.h>
#include "chos/thread_api.h"
int main(void){
 chos_thread_request_t good={.version=1,.size_bytes=64,.entry=0x20000000,.stack_base=0x21001000,.stack_bytes=32768,.tls=0x20080000};
 assert(chos_thread_request_valid(&good));assert(!chos_thread_request_valid(0));
 chos_thread_request_t r=good;r.version=2;assert(!chos_thread_request_valid(&r));
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

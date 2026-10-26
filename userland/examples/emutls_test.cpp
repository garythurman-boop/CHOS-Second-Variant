#include <pthread.h>
extern "C" void native_record_ctor(unsigned,unsigned);
extern "C" void native_record_dtor(unsigned,unsigned);
static thread_local unsigned identity;
static thread_local unsigned plain=0xabc;
struct Tracker {
 unsigned owner,kind;
 explicit Tracker(unsigned n):owner(identity),kind(n){native_record_ctor(owner,kind);}
 ~Tracker(){native_record_dtor(owner,kind);}
};
static thread_local Tracker first(1),second(2);
extern "C" int native_tls_cpp_initialize(unsigned index){
 identity=index;plain+=index;
 return first.owner!=index||first.kind!=1||second.owner!=index||second.kind!=2;
}
extern "C" int native_tls_cpp_check(unsigned index){return first.owner==index&&second.owner==index&&plain==0xabc+index;}
extern "C" int native_tls_cpp_plain(unsigned index){return plain==0xabc+index;}

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "chos/user.h"
#include "chos/libc.h"
static _Thread_local uint64_t initialized=UINT64_C(0x1122334455667788);
static _Thread_local uint64_t zero;
static _Alignas(4096) _Thread_local unsigned char aligned[37];
#define VARIABLES(X) X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(10) X(11) X(12) X(13) X(14) X(15) X(16) X(17) X(18) X(19) X(20) X(21) X(22) X(23) X(24) X(25) X(26) X(27) X(28) X(29) X(30) X(31) X(32) X(33) X(34) X(35) X(36) X(37) X(38) X(39)
#define DECLARE(n) static _Thread_local unsigned value_##n=n;
VARIABLES(DECLARE)
extern int native_tls_cpp_initialize(unsigned);
extern int native_tls_cpp_check(unsigned);
extern int native_tls_cpp_plain(unsigned);
extern int __cxa_thread_atexit_impl(void (*)(void *),void *,void *);
static unsigned ctors[4],dtors[4],key_calls[4];
static pthread_key_t key;
static void say(const char *s){chos_write(s,strlen(s));}
static int fail(unsigned line){char s[11];s[10]='\n';for(unsigned i=0;i<10;i++){s[9-i]=(char)('0'+line%10);line/=10;}say("[FAIL] compiler TLS line ");chos_write(s,sizeof(s));return 1;}
#define CHECK(x) do{if(!(x))return fail(__LINE__);}while(0)
int native_tls_check(unsigned index){
 if(initialized!=UINT64_C(0x1122334455667788)+index+1||zero!=index+1)return 0;
 for(unsigned i=0;i<sizeof(aligned);i++)if(aligned[i]!=(unsigned char)(index+1))return 0;
#define VERIFY(n) if(value_##n!=(n)*100u+index+1u)return 0;
 VARIABLES(VERIFY)
 return 1;
}
static int seed(unsigned index){
 CHECK(initialized==UINT64_C(0x1122334455667788)&&zero==0&&!((uintptr_t)aligned&4095u));
 for(unsigned i=0;i<sizeof(aligned);i++)CHECK(aligned[i]==0);
#define INITIAL(n) CHECK(value_##n==n);value_##n=(n)*100u+index+1u;
 VARIABLES(INITIAL)
 initialized+=index+1;zero=index+1;memset(aligned,(int)index+1,sizeof(aligned));return 0;
}
void native_record_ctor(unsigned index,unsigned kind){
 if(index>3||kind!=++ctors[index]||!native_tls_check(index)){fail(__LINE__);chos_exit(97);}
}
void native_record_dtor(unsigned index,unsigned kind){
 if(index>3||kind!=2u-dtors[index]||!native_tls_check(index)||!native_tls_cpp_plain(index)){fail(__LINE__);chos_exit(97);}
 ++dtors[index];
 if(index==3&&dtors[index]==2)say("[ OK ] main C++ thread_local destructors retained compiler TLS\n");
}
static void specific_destructor(void *p){
 unsigned index=(unsigned)(uintptr_t)p-1u;
 if(index>3||dtors[index]!=2||!native_tls_check(index)||!native_tls_cpp_plain(index)){fail(__LINE__);chos_exit(97);}
 if(++key_calls[index]<4&&pthread_setspecific(key,p)){fail(__LINE__);chos_exit(97);}
}
static void *worker(void *p){
 unsigned index=(unsigned)(uintptr_t)p;
 if(seed(index)||native_tls_cpp_initialize(index)||pthread_setspecific(key,(void *)(uintptr_t)(index+1)))return (void *)1;
 for(unsigned i=0;i<96;i++){
  if(!native_tls_check(index)||!native_tls_cpp_check(index))return (void *)2;
  if(!(i&7u))chos_yield();
 }
 return (void *)(uintptr_t)(index+0x100u);
}
static void *join_main(void *p){
 void *result=0;
 if(pthread_join((pthread_t)(uintptr_t)p,&result)||result!=(void *)0x1234||dtors[3]!=2||key_calls[3]!=4){fail(__LINE__);chos_exit(97);}
 say("[ OK ] compiler TLS main pthread_exit preserves destructor order and live worker\n");return 0;
}
static void noop(void *p){(void)p;}
static void rearm(void *p){if(__cxa_thread_atexit_impl(rearm,p,0))abort();}
int main(int argc,char **argv){
 if(argc>1&&!strcmp(argv[1],"key-quota")){
  pthread_key_t keys[PTHREAD_KEYS_MAX];for(unsigned i=0;i<PTHREAD_KEYS_MAX;i++)CHECK(!pthread_key_create(&keys[i],0));
  say("[ OK ] compiler TLS key quota refusal begins\n");zero=1;return fail(__LINE__);
 }
 if(argc>1&&!strcmp(argv[1],"dtor-quota")){
  say("[ OK ] compiler TLS destructor quota refusal begins\n");
  for(unsigned i=0;i<65;i++)CHECK(!__cxa_thread_atexit_impl(noop,0,0));return fail(__LINE__);
 }
 if(argc>1&&!strcmp(argv[1],"dtor-loop")){
  CHECK(!__cxa_thread_atexit_impl(rearm,0,0));say("[ OK ] compiler TLS destructor rearm bound begins\n");return 0;
 }
 CHECK(!seed(3)&&!native_tls_cpp_initialize(3));CHECK(!pthread_key_create(&key,specific_destructor));
 if(argc>1&&!strcmp(argv[1],"main-exit")){
  CHECK(!pthread_setspecific(key,(void *)4));pthread_t child;
  CHECK(!pthread_create(&child,0,join_main,(void *)(uintptr_t)pthread_self()));pthread_exit((void *)0x1234);
 }
 chos_heap_stats_t baseline;CHECK(!chos_libc_heap_info(&baseline));
 for(unsigned run=0;run<3;run++){
  pthread_t threads[3];
  for(unsigned i=0;i<3;i++){ctors[i]=dtors[i]=key_calls[i]=0;CHECK(!pthread_create(&threads[i],0,worker,(void *)(uintptr_t)i));}
  for(unsigned i=0;i<3;i++){void *result=0;CHECK(!pthread_join(threads[i],&result)&&result==(void *)(uintptr_t)(i+0x100u));CHECK(ctors[i]==2&&dtors[i]==2&&key_calls[i]==4);}
  chos_heap_stats_t current;CHECK(!chos_libc_heap_info(&current));
  CHECK(current.allocations==baseline.allocations&&current.requested_bytes==baseline.requested_bytes&&current.mapped_bytes==baseline.mapped_bytes);
  CHECK(native_tls_check(3)&&native_tls_cpp_check(3));
 }
 CHECK(!pthread_key_delete(key));say("[ OK ] compiler C/C++ TLS isolation, alignment, constructors and ordered cleanup passed\n");return 0;
}

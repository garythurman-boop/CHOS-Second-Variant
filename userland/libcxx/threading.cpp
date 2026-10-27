/* Native libc++ external ABI over CHOS pthreads, never host pthreads. */
#include <__external_threading>
#include <__chrono/steady_clock.h>
#include <__mutex/once_flag.h>
#include <__system_error/system_error.h>
#include <__verbose_abort>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include "chos/tls_runtime.h"
#include "chos/user.h"
_LIBCPP_BEGIN_NAMESPACE_STD
int __libcpp_recursive_mutex_init(__libcpp_recursive_mutex_t *m){
 pthread_mutexattr_t a;int rc=pthread_mutexattr_init(&a);if(rc)return rc;
 rc=pthread_mutexattr_settype(&a,PTHREAD_MUTEX_RECURSIVE);if(!rc)rc=pthread_mutex_init(m,&a);
 int end=pthread_mutexattr_destroy(&a);return rc?rc:end;
}
int __libcpp_recursive_mutex_lock(__libcpp_recursive_mutex_t *m){return pthread_mutex_lock(m);}
bool __libcpp_recursive_mutex_trylock(__libcpp_recursive_mutex_t *m){return !pthread_mutex_trylock(m);}
int __libcpp_recursive_mutex_unlock(__libcpp_recursive_mutex_t *m){return pthread_mutex_unlock(m);}
int __libcpp_recursive_mutex_destroy(__libcpp_recursive_mutex_t *m){return pthread_mutex_destroy(m);}
int __libcpp_mutex_lock(__libcpp_mutex_t *m){return pthread_mutex_lock(m);}
bool __libcpp_mutex_trylock(__libcpp_mutex_t *m){return !pthread_mutex_trylock(m);}
int __libcpp_mutex_unlock(__libcpp_mutex_t *m){return pthread_mutex_unlock(m);}
int __libcpp_mutex_destroy(__libcpp_mutex_t *m){return pthread_mutex_destroy(m);}
int __libcpp_condvar_signal(__libcpp_condvar_t *c){return pthread_cond_signal(c);}
int __libcpp_condvar_broadcast(__libcpp_condvar_t *c){return pthread_cond_broadcast(c);}
int __libcpp_condvar_wait(__libcpp_condvar_t *c,__libcpp_mutex_t *m){return pthread_cond_wait(c,m);}
int __libcpp_condvar_timedwait(__libcpp_condvar_t *c,__libcpp_mutex_t *m,__libcpp_timespec_t *t){return pthread_cond_timedwait(c,m,t);}
int __libcpp_condvar_destroy(__libcpp_condvar_t *c){return pthread_cond_destroy(c);}
int __libcpp_execute_once(__libcpp_exec_once_flag *f,void(*fn)()){return pthread_once(f,fn);}
bool __libcpp_thread_id_equal(__libcpp_thread_id a,__libcpp_thread_id b){return pthread_equal(a,b);}
bool __libcpp_thread_id_less(__libcpp_thread_id a,__libcpp_thread_id b){return a<b;}
bool __libcpp_thread_isnull(const __libcpp_thread_t *t){return !*t;}
namespace {
struct NativeStart {void *(*function)(void *);void *argument;};
void *native_start(void *p){
 auto value=*static_cast<NativeStart *>(p);::free(p);
 auto *t=__chos_libc_tls();t->cxx_worker_active=1;
 void *result=value.function(value.argument);t->cxx_worker_active=0;return result;
}
}
int __libcpp_thread_create(__libcpp_thread_t *t,void*(*fn)(void*),void *p){
 if(!t||!fn)return EINVAL;
 auto *start=static_cast<NativeStart *>(::malloc(sizeof(NativeStart)));if(!start)return ENOMEM;
 *start={fn,p};int rc=pthread_create(t,nullptr,native_start,start);if(rc)::free(start);return rc;
}
__libcpp_thread_id __libcpp_thread_get_current_id(){return pthread_self();}
__libcpp_thread_id __libcpp_thread_get_id(const __libcpp_thread_t *t){return *t;}
int __libcpp_thread_join(__libcpp_thread_t *t){return pthread_join(*t,nullptr);}
int __libcpp_thread_detach(__libcpp_thread_t*){return ENOTSUP;}
void __libcpp_thread_yield(){if(chos_yield()<0)__throw_system_error(EIO,"native yield failed");}
void __libcpp_thread_sleep_for(const chrono::nanoseconds& ns){
 if(ns.count()<=0)return;
 auto start=chrono::steady_clock::now();auto end=chrono::steady_clock::time_point::max();
 if(ns<end-start)end=start+ns;
 for(;;){
  auto now=chrono::steady_clock::now();if(now>=end)return;
  auto left=(end-now).count();uint64_t ticks=(uint64_t)left/10000000u+((uint64_t)left%10000000u!=0);
  if(ticks>60000)ticks=60000;
  long rc=chos_sleep_ticks(ticks);if(rc<0&&rc!=-EINTR)__throw_system_error((int)-rc,"native thread sleep failed");
 }
}
int __libcpp_tls_create(__libcpp_tls_key *k,void(*f)(void*)){return __chos_pthread_key_create_final(k,f);}
void* __libcpp_tls_get(__libcpp_tls_key k){return pthread_getspecific(k);}
int __libcpp_tls_set(__libcpp_tls_key k,void *p){return pthread_setspecific(k,p);}
// No exceptions: errors terminate, without claiming error_category/unwinding ABI.
void __throw_system_error(int,const char *what){__libcpp_verbose_abort("native system error: %s",what);}
void __call_once(volatile once_flag::_State_type& flag,void *arg,void(*fn)(void*)){
 static_assert(sizeof(once_flag::_State_type)==sizeof(uint64_t));
 static_assert(once_flag::_Complete==UINT64_MAX);
 int rc=__chos_cxx_execute_once(reinterpret_cast<volatile uint64_t *>(&flag),arg,fn);
 if(rc)__throw_system_error(rc,"call_once initialization failed");
}
_LIBCPP_END_NAMESPACE_STD

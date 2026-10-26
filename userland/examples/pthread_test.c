#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "chos/user.h"
#include "chos/libc.h"
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER,guardian=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready=PTHREAD_COND_INITIALIZER,go_condition=PTHREAD_COND_INITIALIZER;
static pthread_once_t once=PTHREAD_ONCE_INIT,restart_once=PTHREAD_ONCE_INIT;
static pthread_key_t key;
static unsigned arrived,go,count,once_count,restart_count;
typedef struct {unsigned index,destructors;} argument_t;
static argument_t arguments[3];
static void say(const char *s){chos_write(s,strlen(s));}
static int fail(unsigned line){char s[12];for(unsigned i=0;i<10;i++){s[9-i]=(char)('0'+line%10);line/=10;}s[10]='\n';say("[FAIL] native pthread line ");chos_write(s,11);return 1;}
#define CHECK(x) do{if(!(x))return fail(__LINE__);}while(0)
static void initialize(void){++once_count;chos_sleep_ticks(3);}
static void destroy(void *p){argument_t *a=p;++a->destructors;if(a->destructors==1)pthread_setspecific(key,p);}
static void *worker(void *p){
 argument_t *a=p;int expected=100+(int)a->index;errno=expected;
 if(pthread_mutex_unlock(&guardian)!=EPERM||pthread_setspecific(key,a)||pthread_once(&once,initialize))return (void *)1;
 if(pthread_mutex_lock(&mutex))return (void *)2;
 ++arrived;pthread_cond_signal(&ready);
 while(!go)if(pthread_cond_wait(&go_condition,&mutex))return (void *)3;
 pthread_mutex_unlock(&mutex);
 for(unsigned i=0;i<120;i++){
  size_t n=37u+(i%23u)*11u;unsigned char *data=calloc(1,n);if(!data)return (void *)4;
  for(size_t j=0;j<n;j++)if(data[j])return (void *)5;
  memset(data,(int)(a->index+1),n);unsigned char *grown=realloc(data,n+117);if(!grown)return (void *)6;
  for(size_t j=0;j<n;j++)if(grown[j]!=(unsigned char)(a->index+1))return (void *)7;
  free(grown);
  if((i&31u)==0){if(malloc(SIZE_MAX)!=0||errno!=ENOMEM)return (void *)8;errno=expected;}
  if(pthread_mutex_lock(&mutex))return (void *)9;
  ++count;if(!(i&15u))chos_yield();pthread_mutex_unlock(&mutex);
  if(!(i&3u))chos_yield();
  if(errno!=expected||pthread_getspecific(key)!=a||!pthread_equal(pthread_self(),pthread_self()))return (void *)10;
 }
 return (void *)(uintptr_t)(0x1000u+a->index);
}
static void initialize_and_exit(void){++restart_count;chos_sleep_ticks(3);if(restart_count==1)pthread_exit((void *)0x77);}
static void *restart_worker(void *p){(void)p;return (void *)(uintptr_t)pthread_once(&restart_once,initialize_and_exit);}
static void *join_main(void *p){
 pthread_t main=(pthread_t)(uintptr_t)p;void *result=0;
 if(pthread_join(main,&result)||result!=(void *)0x1234)return (void *)11;
 chos_sleep_ticks(3);say("[ OK ] pthread main exit preserves live worker and join result\n");return 0;
}
int main(int argc,char **argv){
 pthread_t self=pthread_self();CHECK(self&&pthread_equal(self,pthread_self()));errno=42;
 void *untouched=(void *)0x55;CHECK(pthread_join(self,&untouched)==EDEADLK&&untouched==(void *)0x55);
 if(argc>1&&!strcmp(argv[1],"main-exit")){
  pthread_t t;CHECK(!pthread_create(&t,0,join_main,(void *)(uintptr_t)self));pthread_exit((void *)0x1234);
 }
 if(argc>1&&!strcmp(argv[1],"once-exit")){
  pthread_t t[3];unsigned exited=0;
  for(unsigned i=0;i<3;i++)CHECK(!pthread_create(&t[i],0,restart_worker,0));
  for(unsigned i=0;i<3;i++){void *result=0;CHECK(!pthread_join(t[i],&result));if(result==(void *)0x77)++exited;else CHECK(!result);}
  CHECK(exited==1&&restart_count==2&&restart_once==2);say("[ OK ] pthread once retries initialization after owner exits\n");return 0;
 }
 pthread_attr_t attr;CHECK(!pthread_attr_init(&attr));size_t stack;CHECK(!pthread_attr_getstacksize(&attr,&stack)&&stack==32768);
 CHECK(pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED)==ENOTSUP);
 CHECK(pthread_attr_setstacksize(&attr,4096)==EINVAL);
 CHECK(pthread_attr_setstacksize(&attr,CHOS_THREAD_STACK_MAX+1u)==EINVAL);
 CHECK(!pthread_attr_setstacksize(&attr,32769)&&!pthread_attr_getstacksize(&attr,&stack)&&stack==36864);
 pthread_t unchanged_thread=0x5678;
 CHECK(pthread_create(&unchanged_thread,0,0,0)==EINVAL&&unchanged_thread==0x5678);
 pthread_mutexattr_t ma;pthread_mutex_t local;
 CHECK(!pthread_mutexattr_init(&ma));CHECK(!pthread_mutexattr_settype(&ma,PTHREAD_MUTEX_RECURSIVE));CHECK(!pthread_mutex_init(&local,&ma));
 CHECK(!pthread_mutex_lock(&local)&&!pthread_mutex_lock(&local)&&!pthread_mutex_trylock(&local));
 CHECK(!pthread_mutex_unlock(&local)&&!pthread_mutex_unlock(&local)&&!pthread_mutex_unlock(&local)&&!pthread_mutex_destroy(&local));
 CHECK(!pthread_mutexattr_settype(&ma,PTHREAD_MUTEX_ERRORCHECK)&&!pthread_mutex_init(&local,&ma));
 CHECK(!pthread_mutex_lock(&local)&&pthread_mutex_lock(&local)==EDEADLK&&pthread_mutex_trylock(&local)==EBUSY);
 CHECK(pthread_mutex_destroy(&local)==EBUSY);CHECK(!pthread_mutex_unlock(&local)&&!pthread_mutex_destroy(&local));
 CHECK(pthread_mutex_lock(&local)==EINVAL&&pthread_mutexattr_setpshared(&ma,PTHREAD_PROCESS_SHARED)==ENOTSUP);
 CHECK(!pthread_mutexattr_destroy(&ma));
 pthread_condattr_t ca;pthread_cond_t timed;
 CHECK(!pthread_condattr_init(&ca)&&!pthread_condattr_setclock(&ca,CLOCK_MONOTONIC)&&!pthread_cond_init(&timed,&ca));
 CHECK(pthread_condattr_setpshared(&ca,PTHREAD_PROCESS_SHARED)==ENOTSUP);
 struct timespec end;CHECK(!clock_gettime(CLOCK_MONOTONIC,&end));end.tv_nsec+=30000000;
 if(end.tv_nsec>=1000000000){end.tv_nsec-=1000000000;++end.tv_sec;}
 CHECK(!pthread_mutex_lock(&mutex));CHECK(pthread_cond_timedwait(&timed,&mutex,&end)==ETIMEDOUT);
 CHECK(pthread_mutex_trylock(&mutex)==EBUSY);end.tv_nsec=1000000000;
 CHECK(pthread_cond_timedwait(&timed,&mutex,&end)==EINVAL);CHECK(!pthread_mutex_unlock(&mutex));
 CHECK(!pthread_cond_destroy(&timed)&&!pthread_condattr_destroy(&ca));
 CHECK(!pthread_key_create(&key,destroy));
 pthread_key_t old,fresh;CHECK(!pthread_key_create(&old,0)&&!pthread_setspecific(old,(void *)0x12)&&!pthread_key_delete(old));
 CHECK(!pthread_key_create(&fresh,0)&&fresh!=old&&pthread_getspecific(fresh)==0&&pthread_getspecific(old)==0);
 CHECK(pthread_setspecific(old,(void *)1)==EINVAL);CHECK(!pthread_key_delete(fresh));
 pthread_key_t many[PTHREAD_KEYS_MAX];unsigned n=0;
 for(;n<PTHREAD_KEYS_MAX-1;n++)CHECK(!pthread_key_create(&many[n],0));
 pthread_key_t unchanged=0x12345678;CHECK(pthread_key_create(&unchanged,0)==EAGAIN&&unchanged==0x12345678);
 for(unsigned i=0;i<n;i++)CHECK(!pthread_key_delete(many[i]));
 for(unsigned lifecycle=0;lifecycle<3;lifecycle++){
  arrived=go=count=once_count=0;once=PTHREAD_ONCE_INIT;
  pthread_t t[3];CHECK(!pthread_mutex_lock(&guardian)&&!pthread_mutex_lock(&mutex));
  for(unsigned i=0;i<3;i++){arguments[i]=(argument_t){i,0};CHECK(!pthread_create(&t[i],&attr,worker,&arguments[i]));}
  CHECK(pthread_create(&unchanged_thread,&attr,worker,&arguments[0])==EAGAIN&&unchanged_thread==0x5678);
  while(arrived<3)CHECK(!pthread_cond_wait(&ready,&mutex));
  go=1;CHECK(!pthread_cond_broadcast(&go_condition)&&!pthread_mutex_unlock(&mutex));
  for(unsigned i=0;i<3;i++){void *result=0;CHECK(!pthread_join(t[i],&result));if(result!=(void *)(uintptr_t)(0x1000u+i)){say("bad worker return\n");return fail(__LINE__);}CHECK(arguments[i].destructors==2);}
  CHECK(count==360&&once_count==1&&errno==42);CHECK(!pthread_mutex_unlock(&guardian));
 }
 CHECK(!pthread_attr_destroy(&attr)&&!pthread_key_delete(key));
 chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&stats.allocations==0&&stats.regions==0);
 say("[ OK ] native pthread mutex/condition/once, TLS keys/errno and concurrent allocator passed\n");return 0;
}

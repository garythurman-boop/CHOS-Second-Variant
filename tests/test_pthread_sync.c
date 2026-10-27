/* Deterministic native synchronization tests. These mocks exercise error and
 * wake/timeout paths; actual scheduling and contention are tested in QEMU. */
#include <assert.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include "chos/tls_runtime.h"

static chos_libc_tcb_t tcbs[2];
static unsigned current, waits, wakes, clock_calls;
static uint64_t last_ticks;
static int native_errno, clock_failure;
static clockid_t last_clock;
static long wait_result;
static struct timespec now;
static void (*on_wait)(void);
static pthread_mutex_t *observed_mutex;
static pthread_cond_t *observed_condition;
chos_libc_tcb_t *__chos_libc_tls(void){return &tcbs[current];}
int *__chos_errno_location(void){return &native_errno;}
int clock_gettime(clockid_t id,struct timespec *out){
    assert(id==CLOCK_MONOTONIC||id==CLOCK_REALTIME);last_clock=id;++clock_calls;
    if(clock_failure){errno=EIO;return -1;}*out=now;return 0;
}
long chos_wait_word(volatile uint32_t *word,uint32_t expected,uint64_t ticks){
    assert(*word==expected);++waits;last_ticks=ticks;
    if(observed_condition){assert(observed_condition->waiters==1);assert(observed_mutex->state==0);}
    void (*callback)(void)=on_wait;on_wait=0;if(callback)callback();
    return wait_result;
}
long chos_wake_word(volatile uint32_t *word,unsigned count){
    (void)word;assert(count==1||count==8);++wakes;return 0;
}
static void unlock_other(void){current=0;assert(!pthread_mutex_unlock(observed_mutex));current=1;}
static void signal_condition(void){assert(!pthread_cond_signal(observed_condition));}
static void expire(void){now=(struct timespec){1000,0};}
static uint64_t cxx_flag;
static void cxx_initializer(void *p){assert(p==(void *)0x55);}
static void publish_cxx(void){__atomic_store_n(&cxx_flag,UINT64_MAX,__ATOMIC_RELEASE);}
static unsigned initialized;
static void initializer(void){++initialized;}
static void abandon_initializer(void){
    assert(tcbs[current].once_depth==1);__chos_once_abandon(&tcbs[current]);
    /* Simulate a nonreturning pthread_exit: host test uses abandon directly,
     * then validates the flag separately rather than returning into once. */
}
int main(void){
    pthread_mutexattr_t ma;pthread_mutex_t m=PTHREAD_MUTEX_INITIALIZER;
    assert(pthread_mutex_lock(0)==EINVAL);assert(!pthread_mutexattr_init(&ma));
    assert(pthread_mutexattr_settype(&ma,3)==EINVAL);
    assert(pthread_mutexattr_setpshared(&ma,PTHREAD_PROCESS_SHARED)==ENOTSUP);
    assert(!pthread_mutex_lock(&m));assert(pthread_mutex_trylock(&m)==EBUSY);
    assert(pthread_mutex_destroy(&m)==EBUSY);current=1;
    assert(pthread_mutex_unlock(&m)==EPERM);assert(pthread_mutex_trylock(&m)==EBUSY);
    observed_mutex=&m;on_wait=unlock_other;wait_result=-EAGAIN;
    assert(!pthread_mutex_lock(&m));assert(waits==1&&last_ticks==60000);
    assert(!pthread_mutex_unlock(&m)&&wakes==2);
    current=0;assert(!pthread_mutex_lock(&m));current=1;wait_result=-EINTR;
    assert(pthread_mutex_lock(&m)==EINTR);current=0;assert(!pthread_mutex_unlock(&m));
    assert(!pthread_mutexattr_settype(&ma,PTHREAD_MUTEX_RECURSIVE));assert(!pthread_mutex_init(&m,&ma));
    assert(!pthread_mutex_lock(&m)&&!pthread_mutex_lock(&m));
    assert(m.depth==2);m.depth=UINT32_MAX;assert(pthread_mutex_trylock(&m)==EAGAIN);
    m.depth=2;assert(!pthread_mutex_unlock(&m)&&!pthread_mutex_unlock(&m));
    assert(!pthread_mutexattr_settype(&ma,PTHREAD_MUTEX_ERRORCHECK));assert(!pthread_mutex_init(&m,&ma));
    assert(!pthread_mutex_lock(&m)&&pthread_mutex_lock(&m)==EDEADLK);
    assert(!pthread_mutex_unlock(&m)&&!pthread_mutex_destroy(&m));assert(pthread_mutex_lock(&m)==EINVAL);
    assert(!pthread_mutexattr_destroy(&ma));assert(pthread_mutexattr_gettype(&ma,0)==EINVAL);

    pthread_condattr_t ca;pthread_cond_t c=PTHREAD_COND_INITIALIZER;
    assert(!pthread_condattr_init(&ca));assert(!pthread_condattr_setclock(&ca,CLOCK_MONOTONIC));
    assert(pthread_condattr_setclock(&ca,123)==EINVAL);assert(!pthread_cond_init(&c,&ca));
    assert(!pthread_mutex_init(&m,0));assert(pthread_cond_wait(&c,&m)==EPERM);
    assert(!pthread_mutex_lock(&m));
    struct timespec end={10,0};now=end;
    unsigned before=waits;assert(pthread_cond_timedwait(&c,&m,&end)==ETIMEDOUT&&waits==before);
    assert(m.state&&m.owner==(uintptr_t)&tcbs[0]);
    end.tv_nsec=1000000000L;assert(pthread_cond_timedwait(&c,&m,&end)==EINVAL);
    end=(struct timespec){11,1};now=(struct timespec){10,0};wait_result=-ETIMEDOUT;on_wait=expire;
    observed_condition=&c;assert(pthread_cond_timedwait(&c,&m,&end)==ETIMEDOUT);
    assert(last_ticks==101&&c.waiters==0&&m.state); /* round upward, never expire early */
    now=(struct timespec){10,0};end=(struct timespec){10000,0};on_wait=signal_condition;wait_result=-EAGAIN;
    assert(!pthread_cond_timedwait(&c,&m,&end)&&last_ticks==60000&&m.state);
    wait_result=-EINTR;assert(pthread_cond_wait(&c,&m)==EINTR&&m.state&&c.waiters==0);
    clock_failure=1;before=waits;assert(pthread_cond_timedwait(&c,&m,&end)==EIO&&waits==before&&m.state);clock_failure=0;
    now=(struct timespec){10,0};end=now;
    assert(pthread_cond_clockwait(&c,&m,CLOCK_REALTIME,&end)==ETIMEDOUT&&last_clock==CLOCK_REALTIME);
    assert(pthread_cond_clockwait(&c,&m,CLOCK_MONOTONIC,&end)==ETIMEDOUT&&last_clock==CLOCK_MONOTONIC);
    assert(c.clock==CLOCK_MONOTONIC&&m.state);
    assert(pthread_cond_clockwait(&c,&m,99,&end)==EINVAL);
    assert(pthread_cond_clockwait(&c,&m,CLOCK_MONOTONIC,0)==EINVAL);
    c.waiters=1;assert(pthread_cond_destroy(&c)==EBUSY);c.waiters=0;
    assert(!pthread_cond_broadcast(&c));assert(!pthread_cond_destroy(&c));assert(pthread_cond_signal(&c)==EINVAL);
    assert(!pthread_condattr_destroy(&ca));assert(!pthread_mutex_unlock(&m));observed_condition=0;

    pthread_once_t once=PTHREAD_ONCE_INIT;
    assert(!pthread_once(&once,initializer)&&!pthread_once(&once,initializer)&&initialized==1);
    once=3;assert(pthread_once(&once,initializer)==EINVAL);
    once=0;tcbs[0].once_depth=8;assert(pthread_once(&once,initializer)==EAGAIN);tcbs[0].once_depth=0;
    once=1;tcbs[0].active_once[0]=&once;tcbs[0].once_depth=1;
    abandon_initializer();assert(once==0&&tcbs[0].once_depth==0);
    assert(!pthread_once(&once,initializer)&&initialized==2&&once==2);
    assert(__chos_cxx_execute_once(0,0,cxx_initializer)==EINVAL);
    assert(__chos_cxx_execute_once((uint64_t *)((char *)&cxx_flag+1),0,cxx_initializer)==EINVAL);
    assert(__chos_cxx_execute_once(&cxx_flag,0,0)==EINVAL);
    cxx_flag=3;assert(__chos_cxx_execute_once(&cxx_flag,0,cxx_initializer)==EINVAL);
    cxx_flag=0;tcbs[0].cxx_once_depth=CHOS_CXX_ONCE_DEPTH;
    assert(__chos_cxx_execute_once(&cxx_flag,0,cxx_initializer)==EAGAIN);tcbs[0].cxx_once_depth=0;
    assert(!__chos_cxx_execute_once(&cxx_flag,(void *)0x55,cxx_initializer)&&cxx_flag==UINT64_MAX);
    assert(!__chos_cxx_execute_once(&cxx_flag,0,cxx_initializer));
    cxx_flag=1;tcbs[0].active_cxx_once[0]=&cxx_flag;tcbs[0].cxx_once_depth=1;
    assert(__chos_cxx_execute_once(&cxx_flag,0,cxx_initializer)==EDEADLK);
    __chos_once_abandon(&tcbs[0]);assert(!cxx_flag&&!tcbs[0].cxx_once_depth);
    cxx_flag=1;on_wait=publish_cxx;wait_result=-EAGAIN;
    assert(!__chos_cxx_execute_once(&cxx_flag,0,cxx_initializer)&&cxx_flag==UINT64_MAX);
    cxx_flag=1;wait_result=-EIO;assert(__chos_cxx_execute_once(&cxx_flag,0,cxx_initializer)==EIO);
    assert(clock_calls&&waits&&wakes);puts("native pthread synchronization error/wake/timeout tests passed");
    return 0;
}

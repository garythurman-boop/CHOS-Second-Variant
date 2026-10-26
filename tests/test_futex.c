#include <assert.h>
#include <string.h>
#include <stdio.h>
#define CHOS_FUTEX_HOST_TEST
#include "../kernel/core/futex.c"
static uint64_t now=100;
static int masked=1,sleeping,stopping,prepared,running=1,prepare_ok=1,bad_mapping,mode;
static uint32_t word_value=7;
static const address_space_handle_t a={0,1,1},b={1,1,2};
#define WORD UINT64_C(0x21001000)
uint64_t chos_spin_lock_irqsave(chos_spinlock_t *p){assert(!p->held);uint64_t old=masked;p->held=1;masked=1;return old;}
void chos_spin_unlock_irqrestore(chos_spinlock_t *p,uint64_t flags){assert(p->held);p->held=0;masked=(int)flags;}
int address_space_user_range_valid(uint64_t va,size_t n){return va>=0x20000000&&va<0x30000000&&n<=0x30000000-va;}
int address_space_query(address_space_handle_t h,uint64_t va,vmm_mapping_info_t *out){
 (void)h;if(va!=WORD)return 0;*out=(vmm_mapping_info_t){.accessible_at_el0=1,.writable=!bad_mapping,.memory_type=VMM_MEMORY_NORMAL};return 1;
}
int address_space_copy_from_user(address_space_handle_t h,void *out,uint64_t va,size_t n){(void)h;assert(masked&&n==4&&va==WORD);memcpy(out,&word_value,4);return 1;}
int scheduler_is_running(void){return running;}
int scheduler_preemption_enabled(void){return running;}
int timer_is_ready(void){return running;}
uint64_t timer_ticks(void){return now;}
size_t scheduler_current_task_index(void){return 2;}
int scheduler_get_task_info(size_t index,scheduler_task_info_t *out){assert(index==2);*out=(scheduler_task_info_t){.id=5};return 1;}
int scheduler_prepare_sleep_ticks(uint64_t ticks){
 assert(masked&&futex_lock.held&&futex_queued_waiters==1&&ticks==3);++prepared;
 if(!prepare_ok)return 0;sleeping=1;return 1;
}
int scheduler_wake_task(uint32_t task){assert(masked&&task==5);int was=sleeping;sleeping=0;return was;}
void scheduler_await_dispatch(void){assert(!masked&&!sleeping);}
int chos_threads_stopping(address_space_handle_t h){assert(same(h,a));return stopping;}
void chos_futex_irq_enable(void){
 assert(masked&&!futex_lock.held&&sleeping&&futex_queued_waiters==1);masked=0;
 if(mode==0){
  /* The competing store/wake runs only after the waiter has published both
   * its key and sleep state, including when it runs before await_dispatch. */
  word_value=8;assert(chos_futex_wake(b,WORD,1)==0);assert(chos_futex_wake(a,WORD,1)==1);
  assert(chos_futex_wake(a,WORD,1)==0);
 }else if(mode==1){now+=3;sleeping=0;}
 else {stopping=1;sleeping=0;}
}
void chos_futex_irq_restore(uint64_t flags){masked=(int)flags;}
static void reset(void){memset(waits,0,sizeof(waits));futex_queued_waiters=0;masked=1;sleeping=stopping=prepared=bad_mapping=mode=0;prepare_ok=running=1;now=100;word_value=7;}
int main(void){
 reset();assert(chos_futex_wait(a,WORD,6,3)==-CHOS_ERRNO_EAGAIN&&!prepared);
 assert(chos_futex_wait(a,WORD,7,0)==-CHOS_ERRNO_ETIMEDOUT&&!prepared);
 assert(chos_futex_wait(a,WORD+1,7,3)==-CHOS_ERRNO_EINVAL);
 assert(chos_futex_wait(a,0,7,3)==-CHOS_ERRNO_EFAULT);
 assert(chos_futex_wait(a,WORD,7,CHOS_SYS_MAX_SLEEP_TICKS+1u)==-CHOS_ERRNO_EINVAL);
 bad_mapping=1;assert(chos_futex_wait(a,WORD,7,3)==-CHOS_ERRNO_EFAULT);bad_mapping=0;
 assert(chos_futex_wake(a,WORD,CHOS_SCHED_MAX_TASKS+1u)==-CHOS_ERRNO_EINVAL);
 assert(chos_futex_wake(a,WORD,0)==0);
 running=0;assert(chos_futex_wait(a,WORD,7,3)==-CHOS_ERRNO_ENOSYS);running=1;
 prepare_ok=0;assert(chos_futex_wait(a,WORD,7,3)==-CHOS_ERRNO_EAGAIN&&!futex_queued_waiters&&!sleeping);
 reset();assert(chos_futex_wait(a,WORD,7,3)==0&&prepared==1&&!futex_queued_waiters&&masked);
 reset();mode=1;assert(chos_futex_wait(a,WORD,7,3)==-CHOS_ERRNO_ETIMEDOUT&&!futex_queued_waiters&&masked);
 reset();mode=2;assert(chos_futex_wait(a,WORD,7,3)==-CHOS_ERRNO_EINTR&&!futex_queued_waiters&&masked);
 reset();for(unsigned i=0;i<CHOS_SCHED_MAX_TASKS;i++)waits[i]=(wait_t){.owner=a,.live=1,.task=i+20};
 futex_queued_waiters=CHOS_SCHED_MAX_TASKS;assert(chos_futex_wait(a,WORD,7,3)==-CHOS_ERRNO_EAGAIN);
 puts("private futex: atomic compare/queue/sleep ordering, early wake, owner keys, timeout, cancellation and bounds passed");return 0;
}

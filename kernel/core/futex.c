#include "chos/futex.h"
#include "chos/scheduler.h"
#include "chos/spinlock.h"
#include "chos/syscall.h"
#include "chos/threads.h"
#include "chos/timer.h"
typedef struct {
 address_space_handle_t owner;
 uint64_t word,deadline;
 uint32_t task;
 int live,notified;
} wait_t;
static wait_t waits[CHOS_SCHED_MAX_TASKS];
/* Keyless counters aid diagnostics; no user-memory pointer is retained. */
static volatile uint32_t futex_queued_waiters;
static volatile uint64_t futex_wait_calls,futex_wake_events,futex_timeouts,futex_cancellations;
static chos_spinlock_t futex_lock=CHOS_SPINLOCK_INITIALIZER;
#ifdef CHOS_FUTEX_HOST_TEST
extern void chos_futex_irq_enable(void);
extern void chos_futex_irq_restore(uint64_t);
#else
static void chos_futex_irq_enable(void){__asm__ volatile("msr daifclr,#2\n\tisb":::"memory");}
static void chos_futex_irq_restore(uint64_t flags){__asm__ volatile("msr daif,%0\n\tisb"::"r"(flags):"memory");}
#endif
static int same(address_space_handle_t a,address_space_handle_t b){return a.slot==b.slot&&a.id==b.id&&a.generation==b.generation;}
static int read_word(address_space_handle_t owner,uint64_t word,uint32_t *value){
 vmm_mapping_info_t info;
 if(!word||(word&3u)||!address_space_user_range_valid(word,4)||!address_space_query(owner,word,&info)||
  !info.accessible_at_el0||!info.writable||info.executable_at_el0||info.executable_at_el1||info.memory_type!=VMM_MEMORY_NORMAL)return 0;
 return address_space_copy_from_user(owner,value,word,4);
}
int chos_futex_wait(address_space_handle_t owner,uint64_t word,uint32_t expected,uint64_t ticks){
 if(word&3u||ticks>CHOS_SYS_MAX_SLEEP_TICKS)return -CHOS_ERRNO_EINVAL;
 if(!scheduler_is_running()||!scheduler_preemption_enabled()||!timer_is_ready())return -CHOS_ERRNO_ENOSYS;
 uint64_t f=chos_spin_lock_irqsave(&futex_lock);uint32_t value;
 if(!read_word(owner,word,&value)){chos_spin_unlock_irqrestore(&futex_lock,f);return -CHOS_ERRNO_EFAULT;}
 if(value!=expected){chos_spin_unlock_irqrestore(&futex_lock,f);return -CHOS_ERRNO_EAGAIN;}
 if(!ticks){chos_spin_unlock_irqrestore(&futex_lock,f);return -CHOS_ERRNO_ETIMEDOUT;}
 scheduler_task_info_t info;size_t index=scheduler_current_task_index();
 if(index==CHOS_SCHED_NO_INDEX||!scheduler_get_task_info(index,&info)){
  chos_spin_unlock_irqrestore(&futex_lock,f);return -CHOS_ERRNO_ENOSYS;
 }
 unsigned slot=CHOS_SCHED_MAX_TASKS;
 for(unsigned i=0;i<CHOS_SCHED_MAX_TASKS;i++){
  if(waits[i].live&&waits[i].task==info.id){chos_spin_unlock_irqrestore(&futex_lock,f);return -CHOS_ERRNO_EDEADLK;}
  if(!waits[i].live)slot=i;
 }
 if(slot==CHOS_SCHED_MAX_TASKS){chos_spin_unlock_irqrestore(&futex_lock,f);return -CHOS_ERRNO_EAGAIN;}
 uint64_t now=timer_ticks();
 wait_t *w=&waits[slot];*w=(wait_t){.owner=owner,.word=word,.task=info.id,.live=1,.deadline=ticks>UINT64_MAX-now?UINT64_MAX:now+ticks};
 ++futex_queued_waiters;++futex_wait_calls;
 /* Compare, queue publication and marking the task SLEEPING are one IRQ-masked
  * transaction. A waker cannot run between the value check and sleep state. */
 if(!scheduler_prepare_sleep_ticks(ticks)){
  *w=(wait_t){0};--futex_queued_waiters;chos_spin_unlock_irqrestore(&futex_lock,f);return -CHOS_ERRNO_EAGAIN;
 }
 chos_spin_unlock_irqrestore(&futex_lock,f);
 chos_futex_irq_enable();scheduler_await_dispatch();chos_futex_irq_restore(f);
 int stopping=chos_threads_stopping(owner);
 uint64_t cleanup=chos_spin_lock_irqsave(&futex_lock);int notified=w->notified;
 *w=(wait_t){0};--futex_queued_waiters;if(stopping)++futex_cancellations;else if(!notified)++futex_timeouts;
 chos_spin_unlock_irqrestore(&futex_lock,cleanup);
 if(stopping)return -CHOS_ERRNO_EINTR;
 return notified?0:-CHOS_ERRNO_ETIMEDOUT;
}
int chos_futex_wake(address_space_handle_t owner,uint64_t word,unsigned count){
 if((word&3u)||count>CHOS_SCHED_MAX_TASKS)return -CHOS_ERRNO_EINVAL;
 uint64_t f=chos_spin_lock_irqsave(&futex_lock);uint32_t value;
 if(!read_word(owner,word,&value)){chos_spin_unlock_irqrestore(&futex_lock,f);return -CHOS_ERRNO_EFAULT;}
 unsigned n=0;uint64_t now=timer_ticks();
 for(unsigned i=0;i<CHOS_SCHED_MAX_TASKS&&n<count;i++){
  wait_t *w=&waits[i];
  if(w->live&&!w->notified&&w->word==word&&same(w->owner,owner)&&now<w->deadline&&scheduler_wake_task(w->task)){
   w->notified=1;++n;++futex_wake_events;
  }
 }
 chos_spin_unlock_irqrestore(&futex_lock,f);return (int)n;
}

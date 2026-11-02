#include "chos/threads.h"
#include "chos/process.h"
#include "chos/scheduler.h"
#include "chos/spinlock.h"
#include "chos/syscall.h"
#include "chos/user_memory.h"
#include "chos/user_mode.h"
#include "chos/thread_context.h"
#include "chos/thread_pause.h"
#include "chos/vmm.h"
#define THREAD_SLOTS (CHOS_AS_MAX_SPACES*CHOS_THREAD_MAX_PER_PROCESS)
enum { FREE=0,PENDING,READY,RUNNING,DONE,FAILED };
typedef struct {uint32_t depth;chos_thread_context_t context;} paused_t;
typedef struct {
 address_space_handle_t space;
 uint32_t tid,task_id,state,error;
 chos_thread_request_t request;
 int64_t status;
 int pinned,detached,reported;
 paused_t paused;
} thread_t;
typedef struct {
 address_space_handle_t space;
 uint32_t main_tid,main_task;
 chos_pause_owner_t pause_owner;
 paused_t paused;
 int stopping,faulted,main_exited;
 int64_t status;
} owner_t;
static thread_t threads[THREAD_SLOTS];
static owner_t owners[CHOS_AS_MAX_SPACES];
static uint32_t next_tid=1;
static chos_spinlock_t lock=CHOS_SPINLOCK_INITIALIZER;
static int equal(address_space_handle_t a,address_space_handle_t b){return a.id==b.id&&a.generation==b.generation&&a.slot==b.slot;}
static uint32_t current_task(void){
 scheduler_task_info_t info;size_t index=scheduler_current_task_index();
 return index!=CHOS_SCHED_NO_INDEX&&scheduler_get_task_info(index,&info)?info.id:0;
}
static owner_t *owner(address_space_handle_t h,int create,uint32_t task){
 if(h.slot>=CHOS_AS_MAX_SPACES)return 0;
 owner_t *o=&owners[h.slot];
 if(equal(o->space,h))return o;
 if(!create||!next_tid||!task)return 0;
 address_space_info_t info;if(!address_space_get_info(h,&info))return 0;
 *o=(owner_t){.space=h,.main_tid=next_tid++,.main_task=task};return o;
}
static thread_t *find(address_space_handle_t h,uint32_t tid){
 for(unsigned i=0;i<THREAD_SLOTS;i++)if(threads[i].state&&threads[i].tid==tid&&equal(threads[i].space,h))return &threads[i];
 return 0;
}
static int live(const thread_t *t){return t->state==PENDING||t->state==READY||t->state==RUNNING;}
static void unpin(thread_t *t){
 if(t->pinned&&chos_user_memory_unpin(t->space,t->request.stack_base,t->request.stack_bytes))t->pinned=0;
}
/* Called with the thread lock/IRQs held, only after the EL0 task has returned
 * to its separate kernel stack. Creation acknowledgement keeps a fast exiting
 * detached worker observable until its creator receives the actual TID. */
static int dispose_detached(thread_t *t){
 if(!t->detached||!t->reported||live(t))return 0;
 int rc=chos_user_memory_retire_stack(t->space,t->request.stack_base,t->request.stack_bytes);
 if(!rc){*t=(thread_t){0};return 0;}
 if(rc==-CHOS_ERRNO_EIO)t->pinned=0;
 t->state=FAILED;t->status=rc;return rc;
}
int64_t chos_thread_self(address_space_handle_t h){
 uint32_t task=current_task();if(!scheduler_is_running()||!task)return -CHOS_ERRNO_ENOSYS;
 uint64_t f=chos_spin_lock_irqsave(&lock);owner_t *o=owner(h,1,task);int64_t id=-CHOS_ERRNO_EAGAIN;
 if(o){id=o->main_tid;for(unsigned i=0;i<THREAD_SLOTS;i++)if(threads[i].state&&equal(threads[i].space,h)&&threads[i].task_id==task){id=threads[i].tid;break;}}
 chos_spin_unlock_irqrestore(&lock,f);return id;
}
int chos_thread_current_stack(address_space_handle_t h,uint32_t *id,uint64_t *base,uint64_t *bytes){
 if(!id||!base||!bytes)return -CHOS_ERRNO_EINVAL;
 int64_t current=chos_thread_self(h);if(current<0)return (int)current;
 uint32_t task=current_task();int main_thread=0,rc=-CHOS_ERRNO_ESRCH;
 uint64_t f=chos_spin_lock_irqsave(&lock);owner_t *o=owner(h,0,0);thread_t *t=find(h,(uint32_t)current);
 if(o&&!o->stopping){
  if(t&&t->state==RUNNING&&t->pinned&&t->task_id==task){*base=t->request.stack_base;*bytes=t->request.stack_bytes;rc=0;}
  else if(o->main_task==task&&o->main_tid==(uint32_t)current)main_thread=1;
 }
 chos_spin_unlock_irqrestore(&lock,f);
 /* Keep thread and process locks separate; no borrowed record survives unlock. */
 if(main_thread)rc=process_main_stack_bounds(h,base,bytes);
 if(!rc)*id=(uint32_t)current;return rc;
}

/* Called with thread lock/IRQs held. The scheduler only masks IRQs and never
 * takes AS/process/heap locks. Resume preserves real sleep/wake state. */
static void release_pauses_locked(owner_t *o){
 if(o->paused.depth){(void)scheduler_resume_user_task(o->space,o->main_task);o->paused=(paused_t){0};}
 for(unsigned i=0;i<THREAD_SLOTS;i++)if(equal(threads[i].space,o->space)&&threads[i].paused.depth){
  (void)scheduler_resume_user_task(o->space,threads[i].task_id);threads[i].paused=(paused_t){0};
 }
 o->pause_owner=(chos_pause_owner_t){0};
}
void chos_thread_pause_owner_exit(address_space_handle_t h){
 int64_t self=chos_thread_self(h);if(self<0)return;
 uint64_t f=chos_spin_lock_irqsave(&lock);owner_t *o=owner(h,0,0);
 if(o&&o->pause_owner.controller==(uint32_t)self)release_pauses_locked(o);
 chos_spin_unlock_irqrestore(&lock,f);
}
/* op0 acquires/nests,op1 returns the immutable owned snapshot,op2 resumes.
 * Main-stack lookup happens before thread lock to preserve process lock order. */
int chos_thread_pause(address_space_handle_t h,uint32_t id,unsigned op,chos_thread_context_t *out){
 int64_t self=chos_thread_self(h);if(self<0)return (int)self;
 if(!id || op>2 || (op==1&&!out))return -CHOS_ERRNO_EINVAL;
 if(id==(uint32_t)self)return -CHOS_ERRNO_EDEADLK;
 uint64_t main_base=0,main_bytes=0;
 (void)process_main_stack_bounds(h,&main_base,&main_bytes);
 uint64_t f=chos_spin_lock_irqsave(&lock);owner_t *o=owner(h,0,0);thread_t *t=find(h,id);
 int rc=-CHOS_ERRNO_ESRCH;paused_t *p=NULL;uint32_t task=0;uint64_t base=0,bytes=0;
 if(o&&!o->stopping){
  if(t&&t->state==RUNNING&&t->pinned){p=&t->paused;task=t->task_id;base=t->request.stack_base;bytes=t->request.stack_bytes;}
  else if(id==o->main_tid&&!o->main_exited){p=&o->paused;task=o->main_task;base=main_base;bytes=main_bytes;}
 }
 if(!p){if(t&&live(t)&&t->state!=RUNNING)rc=-CHOS_ERRNO_EAGAIN;goto done;}
 if(op==0){
  rc=chos_pause_can_acquire(&o->pause_owner,(uint32_t)self,p->depth);if(rc)goto done;
  if(!p->depth){
   chos_irq_context_t frame;
   rc=scheduler_pause_user_task(h,task,&frame);
   /* A selected RUNNING thread owns a live scheduler task. An inconsistent
    * missing scheduler record is not permission to skip a live GC stack. */
   if(rc==-CHOS_ERRNO_ESRCH)rc=-CHOS_ERRNO_EIO;
   if(rc)goto done;
   if(!chos_thread_context_capture(&frame,id,base,bytes,&p->context)){
    (void)scheduler_resume_user_task(h,task);rc=-CHOS_ERRNO_EFAULT;goto done;
   }
   p->context.flags=CHOS_THREAD_CONTEXT_SUSPENDED|CHOS_THREAD_CONTEXT_REGISTERS_VALID|CHOS_THREAD_CONTEXT_STACK_VALID;
  }
  rc=chos_pause_acquire(&o->pause_owner,(uint32_t)self,&p->depth);
 }else{
  if(o->pause_owner.controller!=(uint32_t)self){rc=-CHOS_ERRNO_EPERM;goto done;}
  if(!p->depth){rc=-CHOS_ERRNO_EINVAL;goto done;}
  if(op==1){*out=p->context;rc=0;}
  else{
   rc=p->depth==1?scheduler_resume_user_task(h,task):0;if(rc)goto done;
   rc=chos_pause_release(&o->pause_owner,(uint32_t)self,&p->depth);
   if(!p->depth)p->context=(chos_thread_context_t){0};
  }
 }
done:chos_spin_unlock_irqrestore(&lock,f);return rc;
}

int chos_threads_main_exit(address_space_handle_t h){
 uint32_t task=current_task();if(!scheduler_is_running()||!task)return -CHOS_ERRNO_ENOSYS;
 uint64_t f=chos_spin_lock_irqsave(&lock);owner_t *o=owner(h,1,task);int rc=0;
 if(!o)rc=-CHOS_ERRNO_EAGAIN;else if(o->main_task!=task)rc=-CHOS_ERRNO_EINVAL;else o->main_exited=1;
 chos_spin_unlock_irqrestore(&lock,f);return rc;
}
int chos_thread_is_secondary(address_space_handle_t h){
 uint32_t task=current_task();uint64_t f=chos_spin_lock_irqsave(&lock);int found=0;
 for(unsigned i=0;i<THREAD_SLOTS;i++)if(live(&threads[i])&&equal(threads[i].space,h)&&threads[i].task_id==task){found=1;break;}
 chos_spin_unlock_irqrestore(&lock,f);return found;
}
static int writable(address_space_handle_t h,uint64_t base,size_t bytes){
 if(!address_space_user_range_valid(base,bytes))return 0;
 for(size_t off=0;off<bytes;){
  vmm_mapping_info_t m;uint64_t va=base+off;
  if(!address_space_query(h,va,&m)||!m.accessible_at_el0||!m.writable||m.executable_at_el0||m.executable_at_el1||m.memory_type!=VMM_MEMORY_NORMAL)return 0;
  size_t n=4096u-(size_t)(va&4095u);if(n>bytes-off)n=bytes-off;off+=n;
 }
 return 1;
}
int64_t chos_thread_enqueue(address_space_handle_t h,const chos_thread_request_t *r){
 if(!scheduler_is_running()||!scheduler_preemption_enabled())return -CHOS_ERRNO_ENOSYS;
 if(!chos_thread_request_valid(r))return -CHOS_ERRNO_EINVAL;
 vmm_mapping_info_t code;
 if(!address_space_query(h,r->entry,&code)||!code.accessible_at_el0||!code.executable_at_el0||code.executable_at_el1||code.writable||code.memory_type!=VMM_MEMORY_NORMAL||
    !writable(h,r->stack_base,r->stack_bytes)||!writable(h,r->tls,32))return -CHOS_ERRNO_EFAULT;
 uint32_t task=current_task();uint64_t f=chos_spin_lock_irqsave(&lock);owner_t *o=owner(h,1,task);
 if(!o||!next_tid){chos_spin_unlock_irqrestore(&lock,f);return -CHOS_ERRNO_EAGAIN;}
 if(o->stopping){chos_spin_unlock_irqrestore(&lock,f);return -CHOS_ERRNO_EINTR;}
 unsigned slot=THREAD_SLOTS,count=0;
 for(unsigned i=0;i<THREAD_SLOTS;i++){if(threads[i].state&&equal(threads[i].space,h))++count;if(!threads[i].state)slot=i;}
 if(count>=CHOS_THREAD_MAX_PER_PROCESS||slot==THREAD_SLOTS){chos_spin_unlock_irqrestore(&lock,f);return -CHOS_ERRNO_EAGAIN;}
 if(!chos_user_memory_pin(h,r->stack_base,r->stack_bytes)){chos_spin_unlock_irqrestore(&lock,f);return -CHOS_ERRNO_EBUSY;}
 thread_t *t=&threads[slot];*t=(thread_t){.space=h,.tid=next_tid++,.state=PENDING,.request=*r,.pinned=1,
  .detached=chos_thread_request_managed(r)&&!!(r->reserved[0]&CHOS_THREAD_CREATE_DETACHED)};
 int64_t tid=t->tid;chos_spin_unlock_irqrestore(&lock,f);return tid;
}
void chos_thread_cancel_pending(address_space_handle_t h,uint32_t tid){
 uint64_t f=chos_spin_lock_irqsave(&lock);thread_t *t=find(h,tid);
 if(t&&t->state==PENDING){t->state=FAILED;t->error=CHOS_ERRNO_EAGAIN;unpin(t);}
 chos_spin_unlock_irqrestore(&lock,f);
}
int chos_thread_create_result(address_space_handle_t h,uint32_t id){
 uint64_t f=chos_spin_lock_irqsave(&lock);thread_t *t=find(h,id);int result,dispose_error=0;
 if(!t)result=-CHOS_ERRNO_ESRCH;
 else if(t->task_id){t->reported=1;result=1;dispose_error=dispose_detached(t);if(dispose_error)result=dispose_error;}
 else if(t->state==FAILED){result=-(int)t->error;unpin(t);if(!t->pinned)*t=(thread_t){0};}
 else result=0;
 chos_spin_unlock_irqrestore(&lock,f);if(dispose_error)chos_threads_stop(h,-CHOS_ERRNO_EIO,1);return result;
}
int chos_thread_info(address_space_handle_t h,uint32_t id,chos_thread_info_t *out){
 if(!out||!id)return -CHOS_ERRNO_EINVAL;
 uint64_t f=chos_spin_lock_irqsave(&lock);thread_t *t=find(h,id);
 if(!t){chos_spin_unlock_irqrestore(&lock,f);return -CHOS_ERRNO_ESRCH;}
 *out=(chos_thread_info_t){.version=CHOS_THREAD_API_VERSION,.size_bytes=sizeof(*out),.tid=id,
  .state=t->state==RUNNING?CHOS_THREAD_RUNNING:t->state==DONE?CHOS_THREAD_EXITED:t->state==FAILED?CHOS_THREAD_FAILED:CHOS_THREAD_QUEUED,
  .exit_status=t->status};
 chos_spin_unlock_irqrestore(&lock,f);return 0;
}
int chos_thread_reap(address_space_handle_t h,uint32_t id){
 uint32_t task=current_task();uint64_t f=chos_spin_lock_irqsave(&lock);thread_t *t=find(h,id);int rc=0;
 if(!t)rc=-CHOS_ERRNO_ESRCH;
 else if(t->detached)rc=-CHOS_ERRNO_EINVAL;
 else if(t->task_id==task)rc=-CHOS_ERRNO_EDEADLK;
 else if(live(t))rc=-CHOS_ERRNO_EAGAIN;
 else {unpin(t);if(t->pinned)rc=-CHOS_ERRNO_EIO;else *t=(thread_t){0};}
 chos_spin_unlock_irqrestore(&lock,f);return rc;
}
int chos_thread_detach(address_space_handle_t h,uint32_t id){
 if(!id)return -CHOS_ERRNO_EINVAL;
 uint64_t f=chos_spin_lock_irqsave(&lock);thread_t *t=find(h,id);int rc=0,dispose_error=0;
 if(!t)rc=-CHOS_ERRNO_ESRCH;
 else if(!chos_thread_request_managed(&t->request))rc=-CHOS_ERRNO_ENOTSUP;
 else if(t->detached)rc=-CHOS_ERRNO_EINVAL;
 else {t->detached=1;rc=dispose_error=dispose_detached(t);}
 chos_spin_unlock_irqrestore(&lock,f);if(dispose_error)chos_threads_stop(h,-CHOS_ERRNO_EIO,1);return rc;
}
void chos_threads_stop(address_space_handle_t h,int64_t status,int faulted){
 uint64_t f=chos_spin_lock_irqsave(&lock);owner_t *o=owner(h,0,0);
 if(o){
  if(!o->stopping||(faulted&&!o->faulted)){o->status=status;o->faulted=faulted;}
  o->stopping=1;release_pauses_locked(o);
  for(unsigned i=0;i<THREAD_SLOTS;i++){
   thread_t *t=&threads[i];if(!equal(t->space,h))continue;
   if(t->state==PENDING){t->state=FAILED;t->error=CHOS_ERRNO_EINTR;unpin(t);}
   if(t->task_id)(void)scheduler_wake_task(t->task_id);
  }
  (void)scheduler_wake_task(o->main_task);
 }
 chos_spin_unlock_irqrestore(&lock,f);
}
int chos_threads_stopping(address_space_handle_t h){
 uint64_t f=chos_spin_lock_irqsave(&lock);owner_t *o=owner(h,0,0);int stopping=o&&o->stopping;
 chos_spin_unlock_irqrestore(&lock,f);return stopping;
}
chos_irq_context_t *chos_threads_cancel_return(chos_irq_context_t *frame){
 if(!frame||(frame->spsr&15u))return frame;
 address_space_handle_t h;if(!user_mode_get_address_space(&h))return frame;
 uint64_t f=chos_spin_lock_irqsave(&lock);owner_t *o=owner(h,0,0);int stop=o&&o->stopping,fault=o?o->faulted:0;int64_t status=o?o->status:0;
 chos_spin_unlock_irqrestore(&lock,f);
 if(stop)user_mode_request_exit(frame,status,fault);
 return frame;
}
static void thread_entry(void *argument){
 unsigned index=(unsigned)(uintptr_t)argument;if(index>=THREAD_SLOTS)scheduler_exit_current();
 uint64_t f=chos_spin_lock_irqsave(&lock);thread_t *t=&threads[index];
 address_space_handle_t h=t->space;chos_thread_request_t r=t->request;t->state=RUNNING;
 int stop=owner(h,0,0)&&owner(h,0,0)->stopping;
 chos_spin_unlock_irqrestore(&lock,f);
 int64_t status=0;int faulted=0,entered=1;
 if(!stop)entered=user_mode_execute_with_tls(h,r.entry,r.stack_base+r.stack_bytes,r.argument,0,&status,&faulted,r.tls);
 chos_thread_pause_owner_exit(h);
 if(!entered||faulted)chos_threads_stop(h,status,1);
 /* The EL0 return trampoline keeps IRQs masked here. No other user task can
  * observe DONE until this task has committed to scheduler_exit_current(). */
 f=chos_spin_lock_irqsave(&lock);t=&threads[index];
 if(!chos_thread_request_managed(&t->request))unpin(t);
 t->status=status;t->state=entered&&!faulted?DONE:FAILED;
 int dispose_error=dispose_detached(t);
 chos_spin_unlock_irqrestore(&lock,f);if(dispose_error)chos_threads_stop(h,-CHOS_ERRNO_EIO,1);scheduler_exit_current();
}
void chos_threads_service(void){
 if(!scheduler_current_is_kernel_task()||vmm_current_root_table_address()!=vmm_kernel_root_table_address())return;
 /* Joined user records and scheduler stacks have separate lifetimes. Reap
  * committed DEAD stacks before a new request needs their bounded slots. */
 scheduler_reap_completed();
 uint64_t f=chos_spin_lock_irqsave(&lock);
 for(unsigned i=0;i<THREAD_SLOTS;i++)if(threads[i].state==PENDING){
  thread_t *t=&threads[i];
  if(scheduler_create_task_in_address_space("native-thread",thread_entry,(void *)(uintptr_t)i,t->space,&t->task_id))t->state=READY;
  else {t->state=FAILED;t->error=CHOS_ERRNO_EAGAIN;unpin(t);}break;
 }
 chos_spin_unlock_irqrestore(&lock,f);
}
void chos_threads_finish(address_space_handle_t h,int64_t *status,int *faulted){
 uint64_t begin=chos_spin_lock_irqsave(&lock);owner_t *initial=owner(h,0,0);
 int main_exited=initial&&initial->main_exited;chos_spin_unlock_irqrestore(&lock,begin);
 if(!main_exited||*faulted)chos_threads_stop(h,*status,*faulted);
 uint64_t daif;__asm__ volatile("mrs %0,daif":"=r"(daif));
 for(;;){
  uint64_t f=chos_spin_lock_irqsave(&lock);int busy=0;owner_t *o=owner(h,0,0);
  for(unsigned i=0;i<THREAD_SLOTS;i++)if(equal(threads[i].space,h)&&live(&threads[i]))busy=1;
  if(!busy&&o){*status=o->stopping?o->status:0;*faulted=o->stopping?o->faulted:0;}
  chos_spin_unlock_irqrestore(&lock,f);if(!busy)break;
  __asm__ volatile("msr daifclr,#2\n\tisb":::"memory");scheduler_sleep_ticks(1);
 }
 __asm__ volatile("msr daif,%0\n\tisb"::"r"(daif):"memory");
}
int chos_threads_drop(address_space_handle_t h){
 uint64_t f=chos_spin_lock_irqsave(&lock);
 for(unsigned i=0;i<THREAD_SLOTS;i++)if(equal(threads[i].space,h)&&live(&threads[i])){chos_spin_unlock_irqrestore(&lock,f);return 0;}
 int ok=1;
 for(unsigned i=0;i<THREAD_SLOTS;i++)if(equal(threads[i].space,h)){unpin(&threads[i]);if(threads[i].pinned)ok=0;else threads[i]=(thread_t){0};}
 owner_t *o=owner(h,0,0);if(ok&&o)*o=(owner_t){0};
 chos_spin_unlock_irqrestore(&lock,f);return ok;
}

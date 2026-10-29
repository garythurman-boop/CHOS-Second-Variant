#include "chos/resource_accounting.h"
#include <stddef.h>
void chos_cpu_account_init(chos_cpu_account_t *a){
 if(a)*a=(chos_cpu_account_t){.valid=1};
}
void chos_cpu_account_transition(chos_cpu_account_t *t,chos_cpu_account_t *p,uint64_t now,unsigned mode){
 if(!t||!p)return;
 if(!t->valid||!p->valid||mode>CHOS_CPU_KERNEL||t->mode>CHOS_CPU_KERNEL)goto invalid;
 if(t->mode!=CHOS_CPU_STOPPED){
  if(now<t->counter)goto invalid;
  uint64_t elapsed=now-t->counter;
  uint64_t *thread=t->mode==CHOS_CPU_USER?&t->user_ticks:&t->system_ticks;
  uint64_t *process=t->mode==CHOS_CPU_USER?&p->user_ticks:&p->system_ticks;
  if(elapsed>UINT64_MAX-*thread||elapsed>UINT64_MAX-*process)goto invalid;
  *thread+=elapsed;*process+=elapsed;
 }
 t->mode=mode;t->counter=now;return;
invalid:
 t->valid=p->valid=0;t->mode=CHOS_CPU_STOPPED;
}

#include "chos/resource_accounting.h"
#include "chos/process_caps.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void step(chos_cpu_account_t *t,chos_cpu_account_t *p,uint64_t now,unsigned m){
 chos_cpu_account_transition(t,p,now,m);assert(t->valid&&p->valid);
}
int main(void){
 chos_cpu_account_t a,b,p;chos_cpu_account_init(&a);chos_cpu_account_init(&b);chos_cpu_account_init(&p);
 step(&a,&p,100,CHOS_CPU_USER);step(&a,&p,110,CHOS_CPU_KERNEL);step(&a,&p,115,CHOS_CPU_STOPPED);
 step(&b,&p,120,CHOS_CPU_USER);step(&b,&p,220,CHOS_CPU_KERNEL);step(&b,&p,225,CHOS_CPU_STOPPED);
 step(&a,&p,1000000,CHOS_CPU_KERNEL);step(&a,&p,1000020,CHOS_CPU_USER);step(&a,&p,1000030,CHOS_CPU_STOPPED);
 assert(a.user_ticks==20&&a.system_ticks==25&&b.user_ticks==100&&b.system_ticks==5);
 assert(p.user_ticks==120&&p.system_ticks==30); /* blocked gap never charged */
 /* Independent event oracle includes repeated timestamps, mode-only boundaries,
  * arbitrary pause lengths and alternating owners, retaining exited records. */
 chos_cpu_account_init(&a);chos_cpu_account_init(&b);chos_cpu_account_init(&p);
 uint64_t seed=0x5e11927,now=0,users[2]={0},systems[2]={0};
 for(unsigned i=0;i<200000;i++){
  chos_cpu_account_t *t=i%2?&a:&b;unsigned id=i%2;uint64_t delta;
  seed=seed*6364136223846793005ull+1;now+=seed>>55;
  step(t,&p,now,CHOS_CPU_USER);
  delta=(seed>>40)&0x3ff;now+=delta;users[id]+=delta;step(t,&p,now,CHOS_CPU_KERNEL);
  delta=(seed>>30)&0xff;now+=delta;systems[id]+=delta;step(t,&p,now,CHOS_CPU_KERNEL);
  step(t,&p,now,CHOS_CPU_STOPPED);assert(t->user_ticks==users[id]&&t->system_ticks==systems[id]);
 }
 assert(p.user_ticks==users[0]+users[1]&&p.system_ticks==systems[0]+systems[1]);
 /* Backward/wrapped active counters and either overflow permanently invalidate
  * the process and current thread without partially committing the interval. */
 for(unsigned which=0;which<5;which++){
  chos_cpu_account_init(&a);chos_cpu_account_init(&p);step(&a,&p,100,CHOS_CPU_USER);
  if(which==1)a.user_ticks=UINT64_MAX-2;
  if(which==2)p.user_ticks=UINT64_MAX-2;
  if(which==3)a.mode=99;
  chos_cpu_account_t olda=a,oldp=p;
  chos_cpu_account_transition(&a,&p,which==0?99:110,which==4?99:CHOS_CPU_KERNEL);
  assert(!a.valid&&!p.valid&&a.mode==CHOS_CPU_STOPPED);
  assert(a.user_ticks==olda.user_ticks&&p.user_ticks==oldp.user_ticks);
  chos_cpu_account_transition(&a,&p,1000,CHOS_CPU_USER);assert(!a.valid&&!p.valid);
 }
 chos_cpu_account_init(&a);chos_cpu_account_init(&p);step(&a,&p,UINT64_MAX-2,CHOS_CPU_KERNEL);
 chos_cpu_account_transition(&a,&p,1,CHOS_CPU_STOPPED);assert(!a.valid&&!p.valid);
 chos_cpu_account_init(&a);chos_cpu_account_init(&p);step(&a,&p,1,CHOS_CPU_KERNEL);
 a.system_ticks=UINT64_MAX;chos_cpu_account_transition(&a,&p,2,CHOS_CPU_STOPPED);assert(!p.valid);
 chos_cpu_account_transition(NULL,&p,0,0);chos_cpu_account_transition(&a,NULL,0,0);
 assert(chos_process_syscall_capability(CHOS_SYS_RESOURCE_INFO)==CHOS_PROCESS_CAP(CHOS_SYS_THREAD_INFO));
 assert(CHOS_PROCESS_CAP_ALL<INT64_MAX&&CHOS_SYSCALL_LIMIT==CHOS_SYS_RESOURCE_INFO);
 puts("resource accounting: active user/kernel intervals, blocked gaps, retained owners, overflow and capability policy passed");
}

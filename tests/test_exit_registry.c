#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "internal/exit_registry.h"
static pthread_mutex_t locks[2];
static int lock_failure=-1,unlock_failure=-1;
static unsigned log_values[2048],log_count,loop_count,recursion_error;
static chos_exit_registry_t registry;
static int lock_host(void *p,unsigned which){(void)p;if(lock_failure==(int)which)return EIO;return pthread_mutex_lock(&locks[which]);}
static int unlock_host(void *p,unsigned which){(void)p;int rc=pthread_mutex_unlock(&locks[which]);return unlock_failure==(int)which?EIO:rc;}
static void reset(void){
 registry=(chos_exit_registry_t){.lock=lock_host,.unlock=unlock_host};
 log_count=loop_count=recursion_error=0;lock_failure=unlock_failure=-1;
}
static void record(void *p){assert(log_count<2048);log_values[log_count++]=(unsigned)(uintptr_t)p;}
static void c_record(void){record((void *)19);}
static void add_and_finalize(void *p){
 record(p);assert(!chos_exit_register_cxx(&registry,record,(void *)5,(void *)1));
 assert(!chos_exit_finalize(&registry,(void *)2,0));
}
static void rearm(void *p){++loop_count;assert(!chos_exit_register_cxx(&registry,rearm,p,0));}
static void recurse(void *p){
 (void)p;if(recursion_error)return;
 assert(!chos_exit_register_cxx(&registry,recurse,0,0));
 int rc=chos_exit_finalize(&registry,0,0);if(rc)recursion_error=(unsigned)-rc;
}
static void *register_worker(void *p){
 unsigned index=(unsigned)(uintptr_t)p;
 for(unsigned i=0;i<25;i++)assert(!chos_exit_register_cxx(&registry,record,(void *)(uintptr_t)(index*25+i),0));
 return 0;
}
static void *finalize_worker(void *p){(void)p;assert(!chos_exit_finalize(&registry,0,0));return 0;}
int main(void){
 pthread_mutexattr_t a;assert(!pthread_mutexattr_init(&a)&&!pthread_mutexattr_settype(&a,PTHREAD_MUTEX_RECURSIVE));
 assert(!pthread_mutex_init(&locks[0],0)&&!pthread_mutex_init(&locks[1],&a)&&!pthread_mutexattr_destroy(&a));reset();
 assert(chos_exit_register_c(0,c_record)==CHOS_EXIT_INVALID);
 assert(chos_exit_register_c(&registry,0)==CHOS_EXIT_INVALID&&chos_exit_register_cxx(&registry,0,0,0)==CHOS_EXIT_INVALID);
 assert(!chos_exit_register_c(&registry,c_record));
 assert(!chos_exit_register_cxx(&registry,record,(void *)1,(void *)1));
 assert(!chos_exit_register_cxx(&registry,record,(void *)2,(void *)1));
 assert(!chos_exit_register_cxx(&registry,record,(void *)4,(void *)2));
 assert(!chos_exit_register_cxx(&registry,add_and_finalize,(void *)3,(void *)1));
 assert(!chos_exit_finalize(&registry,(void *)1,0));
 unsigned expected[]={3,4,5,2,1};assert(log_count==5&&!memcmp(log_values,expected,sizeof(expected)));
 assert(!chos_exit_finalize(&registry,(void *)1,0)&&log_count==5);
 assert(!chos_exit_finalize(&registry,0,1)&&log_count==6&&log_values[5]==19);
 assert(chos_exit_register_c(&registry,c_record)==CHOS_EXIT_CLOSED);
 assert(!chos_exit_finalize(&registry,0,1)&&log_count==6);reset();
 for(unsigned i=0;i<CHOS_EXIT_CALLBACKS;i++)assert(!chos_exit_register_cxx(&registry,record,(void *)(uintptr_t)i,0));
 assert(chos_exit_register_c(&registry,c_record)==CHOS_EXIT_FULL);
 assert(!chos_exit_finalize(&registry,0,0)&&log_count==CHOS_EXIT_CALLBACKS);
 for(unsigned i=0;i<CHOS_EXIT_CALLBACKS;i++)assert(log_values[i]==CHOS_EXIT_CALLBACKS-1-i);
 assert(!chos_exit_register_c(&registry,c_record)&&!chos_exit_finalize(&registry,0,0));reset();
 assert(!chos_exit_register_cxx(&registry,rearm,0,0));
 assert(chos_exit_finalize(&registry,0,0)==CHOS_EXIT_LIMIT&&loop_count==CHOS_EXIT_CALLBACK_BUDGET&&!registry.depth);reset();
 assert(!chos_exit_register_cxx(&registry,recurse,0,0));
 assert(!chos_exit_finalize(&registry,0,0)&&recursion_error==(unsigned)-CHOS_EXIT_LIMIT&&!registry.depth);reset();
 registry.sequence=UINT64_MAX;assert(chos_exit_register_c(&registry,c_record)==CHOS_EXIT_LIMIT);reset();
 assert(chos_exit_finalize(&registry,(void *)1,1)==CHOS_EXIT_INVALID);
 lock_failure=0;assert(chos_exit_register_c(&registry,c_record)==CHOS_EXIT_LOCK_ERROR);reset();
 lock_failure=1;assert(chos_exit_finalize(&registry,0,0)==CHOS_EXIT_LOCK_ERROR);reset();
 assert(!chos_exit_register_c(&registry,c_record));lock_failure=0;
 assert(chos_exit_finalize(&registry,0,0)==CHOS_EXIT_LOCK_ERROR&&!registry.depth);reset();
 unlock_failure=0;assert(chos_exit_register_c(&registry,c_record)==CHOS_EXIT_LOCK_ERROR);reset();
 for(unsigned cycle=0;cycle<50;cycle++){
  pthread_t threads[4];for(unsigned i=0;i<4;i++)assert(!pthread_create(&threads[i],0,register_worker,(void *)(uintptr_t)i));
  for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],0));
  unsigned wanted[100];for(unsigned i=0;i<100;i++)wanted[i]=0;
  for(unsigned i=0;i<CHOS_EXIT_CALLBACKS;i++)if(registry.entries[i].active){
   assert(registry.entries[i].sequence>=1&&registry.entries[i].sequence<=100);
   wanted[100-registry.entries[i].sequence]=(unsigned)(uintptr_t)registry.entries[i].object;
  }
  for(unsigned i=0;i<4;i++)assert(!pthread_create(&threads[i],0,finalize_worker,0));
  for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],0));
  assert(log_count==100&&!memcmp(log_values,wanted,sizeof(wanted)));reset();
 }
 assert(!pthread_mutex_destroy(&locks[0])&&!pthread_mutex_destroy(&locks[1]));
 puts("exit registry: typed LIFO/DSO callbacks,reentrancy,quotas,closure,errors and real host concurrency passed");
 return 0;
}

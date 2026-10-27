#include "internal/exit_registry.h"
static int valid(const chos_exit_registry_t *r){return r&&r->lock&&r->unlock;}
static int register_callback(chos_exit_registry_t *r,chos_exit_callback_t callback){
 if(!valid(r))return CHOS_EXIT_INVALID;
 if(r->lock(r->context,CHOS_EXIT_TABLE_LOCK))return CHOS_EXIT_LOCK_ERROR;
 int result=0;unsigned slot;
 if(r->closed){result=CHOS_EXIT_CLOSED;goto done;}
 if(r->sequence==UINT64_MAX){result=CHOS_EXIT_LIMIT;goto done;}
 for(slot=0;slot<CHOS_EXIT_CALLBACKS;slot++)if(!r->entries[slot].active)break;
 if(slot==CHOS_EXIT_CALLBACKS){result=CHOS_EXIT_FULL;goto done;}
 callback.sequence=++r->sequence;callback.active=1;r->entries[slot]=callback;
done:
 if(r->unlock(r->context,CHOS_EXIT_TABLE_LOCK))result=CHOS_EXIT_LOCK_ERROR;
 return result;
}
int chos_exit_register_c(chos_exit_registry_t *r,void (*fn)(void)){
 if(!fn)return CHOS_EXIT_INVALID;
 return register_callback(r,(chos_exit_callback_t){.function.c=fn});
}
int chos_exit_register_cxx(chos_exit_registry_t *r,void (*fn)(void *),void *object,void *dso){
 if(!fn)return CHOS_EXIT_INVALID;
 return register_callback(r,(chos_exit_callback_t){.function.cxx=fn,.object=object,.dso=dso,.cxx=1});
}
int chos_exit_finalize(chos_exit_registry_t *r,void *dso,int close_registry){
 if(!valid(r)||(close_registry&&dso))return CHOS_EXIT_INVALID;
 if(r->lock(r->context,CHOS_EXIT_FINAL_LOCK))return CHOS_EXIT_LOCK_ERROR;
 int result=0;
 if(r->depth>=CHOS_EXIT_RECURSION){result=CHOS_EXIT_LIMIT;goto unlocked;}
 if(!r->depth)r->remaining=CHOS_EXIT_CALLBACK_BUDGET;
 ++r->depth;
 for(;;){
  if(r->lock(r->context,CHOS_EXIT_TABLE_LOCK)){result=CHOS_EXIT_LOCK_ERROR;break;}
  unsigned selected=CHOS_EXIT_CALLBACKS;uint64_t newest=0;
  for(unsigned i=0;i<CHOS_EXIT_CALLBACKS;i++){
   const chos_exit_callback_t *e=&r->entries[i];
   if(e->active&&(!dso||e->dso==dso)&&e->sequence>newest){newest=e->sequence;selected=i;}
  }
  if(selected==CHOS_EXIT_CALLBACKS){
   if(close_registry)r->closed=1;
   if(r->unlock(r->context,CHOS_EXIT_TABLE_LOCK))result=CHOS_EXIT_LOCK_ERROR;
   break;
  }
  if(!r->remaining){
   result=CHOS_EXIT_LIMIT;
   if(r->unlock(r->context,CHOS_EXIT_TABLE_LOCK))result=CHOS_EXIT_LOCK_ERROR;
   break;
  }
  chos_exit_callback_t callback=r->entries[selected];r->entries[selected].active=0;--r->remaining;
  if(r->unlock(r->context,CHOS_EXIT_TABLE_LOCK)){result=CHOS_EXIT_LOCK_ERROR;break;}
  if(callback.cxx)callback.function.cxx(callback.object);else callback.function.c();
 }
 --r->depth;
unlocked:
 if(r->unlock(r->context,CHOS_EXIT_FINAL_LOCK))result=CHOS_EXIT_LOCK_ERROR;
 return result;
}

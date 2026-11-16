/* CHOS SQLite in-memory platform profile, 2026-11-12.
 * Real pthread synchronization, OS entropy and wall clock. Persistent file
 * operations are explicitly unavailable; there is no successful fake disk VFS.
 */
#include <sqlite3.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <stdatomic.h>
#ifdef __CHOS__
#include <chos/user.h>
#else
#include <sys/random.h>
#include <unistd.h>
#endif
struct sqlite3_mutex {pthread_mutex_t lock;int kind;_Atomic(uintptr_t) owner;_Atomic(unsigned) depth;};
#define STATIC_MUTEX(id) {.lock=PTHREAD_MUTEX_INITIALIZER,.kind=(id)}
static sqlite3_mutex statics[]={STATIC_MUTEX(2),STATIC_MUTEX(3),STATIC_MUTEX(4),STATIC_MUTEX(5),STATIC_MUTEX(6),STATIC_MUTEX(7),STATIC_MUTEX(8),STATIC_MUTEX(9),STATIC_MUTEX(10),STATIC_MUTEX(11),STATIC_MUTEX(12),STATIC_MUTEX(13)};
static int mutex_init(void){return SQLITE_OK;}
static int mutex_end(void){
 for(unsigned i=0;i<sizeof(statics)/sizeof(*statics);++i)if(atomic_load(&statics[i].depth))abort();
 return SQLITE_OK;
}
static sqlite3_mutex *mutex_alloc(int id){
 if(id>=2)return id<=13?&statics[id-2]:NULL;
 if(id!=SQLITE_MUTEX_FAST&&id!=SQLITE_MUTEX_RECURSIVE)return NULL;
 sqlite3_mutex *m=calloc(1,sizeof(*m));if(!m)return NULL;m->kind=id;
 pthread_mutexattr_t attr;if(pthread_mutexattr_init(&attr)){free(m);return NULL;}
 int rc=pthread_mutexattr_settype(&attr,id==SQLITE_MUTEX_RECURSIVE?PTHREAD_MUTEX_RECURSIVE:PTHREAD_MUTEX_NORMAL);
 if(!rc)rc=pthread_mutex_init(&m->lock,&attr);pthread_mutexattr_destroy(&attr);
 if(rc){free(m);return NULL;}return m;
}
static void mutex_free(sqlite3_mutex *m){
 if(!m)return;if(m->kind>=2||atomic_load(&m->depth)||pthread_mutex_destroy(&m->lock))abort();free(m);
}
static void entered(sqlite3_mutex *m){atomic_store(&m->owner,(uintptr_t)pthread_self());atomic_fetch_add(&m->depth,1);}
static void mutex_enter(sqlite3_mutex *m){if(pthread_mutex_lock(&m->lock))abort();entered(m);}
static int mutex_try(sqlite3_mutex *m){int rc=pthread_mutex_trylock(&m->lock);if(rc==EBUSY)return SQLITE_BUSY;if(rc)abort();entered(m);return SQLITE_OK;}
static void mutex_leave(sqlite3_mutex *m){
 if(atomic_load(&m->owner)!=(uintptr_t)pthread_self()||!atomic_load(&m->depth))abort();
 if(atomic_fetch_sub(&m->depth,1)==1)atomic_store(&m->owner,0);
 if(pthread_mutex_unlock(&m->lock))abort();
}
static int mutex_held(sqlite3_mutex *m){return !m||(atomic_load(&m->owner)==(uintptr_t)pthread_self()&&atomic_load(&m->depth));}
static int mutex_notheld(sqlite3_mutex *m){return !m||!mutex_held(m);}
const sqlite3_mutex_methods *chos_sqlite_mutex_methods(void){
 static const sqlite3_mutex_methods methods={mutex_init,mutex_end,mutex_alloc,mutex_free,mutex_enter,mutex_try,mutex_leave,mutex_held,mutex_notheld};return &methods;
}
static int entropy(unsigned char *out,size_t bytes){
 size_t offset=0;
 while(offset<bytes){
#ifdef __CHOS__
  long n=chos_random_bytes(out+offset,bytes-offset);
#else
  long n=getrandom(out+offset,bytes-offset,0);
  if(n<0&&errno==EINTR)continue;
#endif
  if(n<=0||(size_t)n>bytes-offset)return -1;offset+=(size_t)n;
 }
 return 0;
}
static int unavailable_open(sqlite3_vfs *v,const char *n,sqlite3_file *f,int flags,int *out){(void)v;(void)n;(void)flags;if(out)*out=0;f->pMethods=NULL;return SQLITE_CANTOPEN;}
static int unavailable_delete(sqlite3_vfs *v,const char *n,int sync){(void)v;(void)n;(void)sync;return SQLITE_IOERR_DELETE;}
static int unavailable_access(sqlite3_vfs *v,const char *n,int flags,int *out){(void)v;(void)n;(void)flags;*out=0;return SQLITE_IOERR_ACCESS;}
static int fullpath(sqlite3_vfs *v,const char *name,int size,char *out){(void)v;if(!name||size<=0||strlen(name)>=(size_t)size)return SQLITE_CANTOPEN;memcpy(out,name,strlen(name)+1);return SQLITE_OK;}
static int randomness(sqlite3_vfs *v,int size,char *out){(void)v;if(size<0||entropy((unsigned char*)out,(size_t)size))abort();return size;}
static int sleep_us(sqlite3_vfs *v,int us){(void)v;if(us<=0)return 0;
#ifdef __CHOS__
 uint64_t ticks=((uint64_t)us+9999)/10000;if(chos_sleep_ticks(ticks)<0)return 0;return (int)(ticks*10000);
#else
 struct timespec t={us/1000000,(us%1000000)*1000L};while(nanosleep(&t,&t)&&errno==EINTR){}return us;
#endif
}
static int wall_ms(sqlite3_vfs *v,sqlite3_int64 *out){(void)v;struct timespec t;if(clock_gettime(CLOCK_REALTIME,&t))return SQLITE_IOERR;
 if(t.tv_sec<0||t.tv_sec>INT64_MAX/1000-INT64_C(210866760000))return SQLITE_IOERR;
 *out=INT64_C(210866760000000)+(sqlite3_int64)t.tv_sec*1000+t.tv_nsec/1000000;return SQLITE_OK;
}
static int wall_jd(sqlite3_vfs *v,double *out){sqlite3_int64 ms;int rc=wall_ms(v,&ms);if(!rc)*out=(double)ms/86400000.0;return rc;}
static sqlite3_vfs memory_vfs={.iVersion=2,.szOsFile=sizeof(sqlite3_file),.mxPathname=1024,.zName="chos-memory-only",.xOpen=unavailable_open,.xDelete=unavailable_delete,.xAccess=unavailable_access,.xFullPathname=fullpath,.xRandomness=randomness,.xSleep=sleep_us,.xCurrentTime=wall_jd,.xCurrentTimeInt64=wall_ms};
int sqlite3_os_init(void){unsigned char probe[32];if(entropy(probe,sizeof(probe)))return SQLITE_IOERR;memset(probe,0,sizeof(probe));return sqlite3_vfs_register(&memory_vfs,1);}
int sqlite3_os_end(void){return sqlite3_vfs_unregister(&memory_vfs);}

#include <sqlite3.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef __CHOS__
#include <chos/libc.h>
#endif
#define CHECK(x) do{if(!(x)){printf("[FAIL] native SQLite line %u\n",__LINE__);fflush(stdout);return 1;}}while(0)
static int execute(sqlite3 *db,const char *sql){char *message=NULL;int r=sqlite3_exec(db,sql,NULL,NULL,&message);if(r)printf("[SQL ERROR] %s\n",message?message:"unknown");sqlite3_free(message);return r;}
static int scalar(sqlite3 *db,const char *sql,sqlite3_int64 expected){sqlite3_stmt *s=NULL;CHECK(sqlite3_prepare_v2(db,sql,-1,&s,NULL)==SQLITE_OK);CHECK(sqlite3_step(s)==SQLITE_ROW&&sqlite3_column_int64(s,0)==expected);CHECK(sqlite3_step(s)==SQLITE_DONE);CHECK(sqlite3_finalize(s)==SQLITE_OK);return 0;}
static int statements(void){
 sqlite3 *db=NULL;CHECK(sqlite3_open_v2(":memory:",&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,NULL)==SQLITE_OK);
 CHECK(!execute(db,"CREATE TABLE t(id INTEGER PRIMARY KEY,label TEXT UNIQUE,bytes BLOB,value REAL);BEGIN"));
 sqlite3_stmt *s=NULL;CHECK(sqlite3_prepare_v2(db,"INSERT INTO t VALUES(?,?,?,?)",-1,&s,NULL)==SQLITE_OK);
 unsigned char bytes[]={0,1,2,127,128,254,255};
 for(int i=0;i<200;++i){char label[64];snprintf(label,sizeof(label),"row-%d-\xce\xa9-\xf0\x9f\x8c\x8d",i);
  CHECK(sqlite3_bind_int(s,1,i)==SQLITE_OK&&sqlite3_bind_text(s,2,label,-1,SQLITE_TRANSIENT)==SQLITE_OK);
  CHECK(sqlite3_bind_blob(s,3,bytes,sizeof(bytes),SQLITE_TRANSIENT)==SQLITE_OK&&sqlite3_bind_double(s,4,i+0.25)==SQLITE_OK);
  CHECK(sqlite3_step(s)==SQLITE_DONE&&sqlite3_reset(s)==SQLITE_OK&&sqlite3_clear_bindings(s)==SQLITE_OK);
 }
 CHECK(sqlite3_finalize(s)==SQLITE_OK&&!execute(db,"COMMIT"));CHECK(!scalar(db,"SELECT count(*) FROM t",200));
 CHECK(!execute(db,"BEGIN;DELETE FROM t WHERE id<100;ROLLBACK"));CHECK(!scalar(db,"SELECT count(*) FROM t",200));
 CHECK(sqlite3_prepare_v2(db,"SELECT label,bytes,value FROM t WHERE id=17",-1,&s,NULL)==SQLITE_OK&&sqlite3_step(s)==SQLITE_ROW);
 CHECK(!strcmp((const char*)sqlite3_column_text(s,0),"row-17-\xce\xa9-\xf0\x9f\x8c\x8d"));
 CHECK(sqlite3_column_bytes(s,1)==sizeof(bytes)&&!memcmp(sqlite3_column_blob(s,1),bytes,sizeof(bytes)));
 CHECK(sqlite3_column_double(s,2)==17.25&&sqlite3_column_origin_name(s,0)&&!strcmp(sqlite3_column_origin_name(s,0),"label"));
 CHECK(sqlite3_finalize(s)==SQLITE_OK);
 CHECK(sqlite3_exec(db,"INSERT INTO t(id,label) VALUES(17,'duplicate')",NULL,NULL,NULL)==SQLITE_CONSTRAINT);
 CHECK(sqlite3_prepare_v2(db,"THIS IS INVALID SQL",-1,&s,NULL)==SQLITE_ERROR&&s==NULL);
 CHECK(!scalar(db,"SELECT length(randomblob(32))",32));
 CHECK(!scalar(db,"SELECT json_extract('{\"a\":17}', '$.a')",17));
 CHECK(!scalar(db,"SELECT unixepoch('2024-02-29 00:00:00')",1709164800));
 CHECK(sqlite3_close(db)==SQLITE_OK);return 0;
}
static int rejection(void){
 sqlite3 *db=NULL;int rc=sqlite3_open_v2("/DOCUMENTS/not-persistent.db",&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,NULL);
 CHECK(rc==SQLITE_CANTOPEN&&db);CHECK(sqlite3_close(db)==SQLITE_OK);
 CHECK(sqlite3_open(":memory:",&db)==SQLITE_OK);
 CHECK(sqlite3_exec(db,"ATTACH '/DOCUMENTS/not-persistent.db' AS disk",NULL,NULL,NULL)==SQLITE_CANTOPEN);
 CHECK(sqlite3_exec(db,"SELECT load_extension('untrusted')",NULL,NULL,NULL)==SQLITE_ERROR);
 CHECK(sqlite3_close(db)==SQLITE_OK);return 0;
}
static sqlite3 *shared;
static void *worker(void *arg){
 (void)arg;
 if(statements())return (void*)1;
 for(unsigned i=0;i<300;++i)if(execute(shared,"UPDATE counter SET n=n+1"))return (void*)2;
 return NULL;
}
static int threads(void){
 CHECK(sqlite3_open_v2(":memory:",&shared,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,NULL)==SQLITE_OK);
 CHECK(!execute(shared,"CREATE TABLE counter(n INTEGER);INSERT INTO counter VALUES(0)"));
 pthread_t t[3];pthread_attr_t attr;CHECK(!pthread_attr_init(&attr)&&!pthread_attr_setstacksize(&attr,128*1024));
 for(unsigned i=0;i<3;++i)CHECK(!pthread_create(&t[i],&attr,worker,(void*)(uintptr_t)i));
 for(unsigned i=0;i<3;++i){void *result=NULL;CHECK(!pthread_join(t[i],&result)&&!result);}
 CHECK(!pthread_attr_destroy(&attr));CHECK(!scalar(shared,"SELECT n FROM counter",900));CHECK(sqlite3_close(shared)==SQLITE_OK);shared=NULL;return 0;
}
static void *try_locked(void *arg){return (void*)(uintptr_t)sqlite3_mutex_try(arg);}
static int mutexes(void){
 sqlite3_mutex *m=sqlite3_mutex_alloc(SQLITE_MUTEX_RECURSIVE);CHECK(m);
 sqlite3_mutex_enter(m);sqlite3_mutex_enter(m);CHECK(sqlite3_mutex_try(m)==SQLITE_OK);
 pthread_t t;pthread_attr_t attr;CHECK(!pthread_attr_init(&attr)&&!pthread_attr_setstacksize(&attr,128*1024));
 CHECK(!pthread_create(&t,&attr,try_locked,m));void *result=NULL;CHECK(!pthread_join(t,&result));
 CHECK((uintptr_t)result==SQLITE_BUSY);CHECK(!pthread_attr_destroy(&attr));
 sqlite3_mutex_leave(m);sqlite3_mutex_leave(m);sqlite3_mutex_leave(m);sqlite3_mutex_free(m);return 0;
}
static int allocation_failures(void){
 /* Real SQLite hard heap quota reaches allocation failure without fabricated
  * SQL success. Restoring the quota permits the same query afterwards. */
 sqlite3 *db=NULL;CHECK(sqlite3_open(":memory:",&db)==SQLITE_OK);
 sqlite3_stmt *s=NULL;CHECK(sqlite3_prepare_v2(db,"SELECT zeroblob(1048576)",-1,&s,NULL)==SQLITE_OK&&s);
 sqlite3_int64 current=sqlite3_memory_used(),old=sqlite3_hard_heap_limit64(current+65536);
 int rc=sqlite3_step(s),refused=rc==SQLITE_NOMEM;
 if(rc==SQLITE_ROW){const void *blob=sqlite3_column_blob(s,0);refused=!blob&&sqlite3_errcode(db)==SQLITE_NOMEM;}
 sqlite3_finalize(s);sqlite3_hard_heap_limit64(old);
 if(!refused){sqlite3_close(db);CHECK(refused);}
 CHECK(!scalar(db,"SELECT length(zeroblob(1048576))",1048576));CHECK(sqlite3_close(db)==SQLITE_OK);return 0;
}
int main(int argc,char **argv){
 CHECK(!strcmp(sqlite3_libversion(),"3.54.1"));
 if(argc==2&&!strcmp(argv[1],"entropy-failure")){
  CHECK(sqlite3_initialize()==SQLITE_IOERR);CHECK(sqlite3_shutdown()==SQLITE_OK&&sqlite3_memory_used()==0);
#ifdef __CHOS__
  chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&!stats.allocations&&!stats.mapped_bytes);
#endif
  puts("[ OK ] SQLite refuses initialization without entropy");return 0;
 }
 CHECK(sqlite3_threadsafe()==1&&sqlite3_initialize()==SQLITE_OK);
 CHECK(!statements()&&!rejection()&&!allocation_failures()&&!mutexes());
 puts("[ OK ] actual SQLite statements,transactions,bindings,errors and allocation bounds passed");
 CHECK(!threads());puts("[ OK ] actual SQLite pthread serialized and independent connections passed");
 CHECK(sqlite3_shutdown()==SQLITE_OK&&sqlite3_memory_used()==0);
#ifdef __CHOS__
 chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&!stats.allocations&&!stats.mapped_bytes);
#endif
 puts("[ OK ] native SQLite in-memory resources cleaned up");return 0;
}

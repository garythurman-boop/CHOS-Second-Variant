/* Actual CHFS/storage wrappers over an owned memory block device. Only the
 * unavailable hardware discovery entry is modeled; filesystem code is real. */
#include "chos/storage.h"
#include "chos/syscall.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#define SECTORS 1024u
static unsigned char disk[SECTORS*512u];
static int backend_context;
static int reads,writes,flushes,fail_read,fail_write,fail_flush,tear_write;
static int rd(chos_block_device_t *d,uint64_t lba,uint32_t count,void *out){
 assert(d->context==&backend_context);++reads;if(fail_read&&reads==fail_read)return 0;
 if(lba>=SECTORS||count>SECTORS-lba)return 0;
 memcpy(out,disk+lba*512u,count*512u);return 1;
}
static int wr(chos_block_device_t *d,uint64_t lba,uint32_t count,const void *in){
 assert(d->context==&backend_context);++writes;
 if(lba>=SECTORS||count>SECTORS-lba)return 0;
 if(fail_write&&writes==fail_write){
  /* A failed callback need not mean zero bytes reached the medium. */
  if(tear_write)memcpy(disk+lba*512u,in,32);
  return 0;
 }
 memcpy(disk+lba*512u,in,count*512u);return 1;
}
static int fl(chos_block_device_t *d){assert(d->context==&backend_context);++flushes;return !(fail_flush&&flushes==fail_flush);}
int virtio_blk_init(chos_block_device_t *d){(void)d;return 0;}
const char *virtio_blk_last_error(void){return "host fixture has no hardware discovery";}
static chos_block_device_t device={"owned-host-fs",SECTORS,&backend_context,rd,wr,fl,1};
static void reset(void){
 storage_detach();fail_read=fail_write=fail_flush=tear_write=0;reads=writes=flushes=0;
 memset(disk,0,sizeof disk);assert(chfs_format(&device));
 assert(storage_attach_block_device(&device)&&storage_mounted()&&storage_write_ready());
}
static chos_storage_lease_t snapshot(const char *path,const char *expected){
 unsigned char buffer[128];size_t n=999;chos_storage_lease_t lease={0};memset(buffer,0xa5,sizeof buffer);
 assert(!storage_read_snapshot(path,buffer,sizeof buffer,&n,&lease)&&n==strlen(expected)&&!memcmp(buffer,expected,n)&&buffer[n]==0xa5);
 assert(lease.mount&&lease.object&&lease.revision&&!lease.reserved);return lease;
}
static void unchanged_output(const chos_storage_lease_t *value){
 chos_storage_lease_t out,old;memset(&out,0xa5,sizeof out);old=out;
 assert(storage_commit_snapshot("/A",value,"bad",3,&out)==-CHOS_ERRNO_ESTALE&&!memcmp(&out,&old,sizeof out));
}
static pthread_barrier_t gate;static chos_storage_lease_t shared;static int answers[2];
static void *writer(void *p){
 size_t id=(size_t)p;chos_storage_lease_t out;
 pthread_barrier_wait(&gate);answers[id]=storage_commit_snapshot("/A",&shared,id?"second":"first",id?6:5,&out);return NULL;
}
int main(void){
 reset();assert(storage_write_file("/A","old",3)&&storage_write_file("/B","other",5));
 chos_storage_lease_t a=snapshot("/a","old"),b=snapshot("/B","other"),next;
 assert(storage_write_file("/B","updated",7));
 assert(!storage_commit_snapshot("/A",&a,"mine",4,&next)&&next.object==a.object&&next.revision!=a.revision);
 unchanged_output(&a);a=next;snapshot("/A","mine");
 assert(storage_commit_snapshot("/B",&b,"stale",5,&next)==-CHOS_ERRNO_ESTALE);
 /* Identical content and metadata still invalidates the writable lease. */
 assert(storage_write_file("/A","mine",4));unchanged_output(&a);a=snapshot("/A","mine");
 assert(storage_rename_file("/A","/RENAMED"));unchanged_output(&a);
 next=snapshot("/RENAMED","mine");assert(next.object==a.object);
 assert(storage_remove_file("/RENAMED")&&storage_write_file("/A","mine",4));unchanged_output(&a);
 next=snapshot("/A","mine");assert(next.object!=a.object);a=next;
 assert(storage_mount()&&storage_write_ready());unchanged_output(&a);a=snapshot("/A","mine");
 /* Independently executing writers cannot both commit the same revision. */
 shared=a;assert(!pthread_barrier_init(&gate,NULL,3));pthread_t threads[2];
 for(size_t i=0;i<2;i++)assert(!pthread_create(&threads[i],NULL,writer,(void *)i));
 pthread_barrier_wait(&gate);for(size_t i=0;i<2;i++)assert(!pthread_join(threads[i],NULL));pthread_barrier_destroy(&gate);
 assert((!answers[0]&&answers[1]==-CHOS_ERRNO_ESTALE)||(!answers[1]&&answers[0]==-CHOS_ERRNO_ESTALE));
 a=snapshot("/A",!answers[0]?"first":"second");
 /* A failed allocator backup read cannot be followed by a data write. */
 int old_writes=writes;fail_read=reads+1;
 assert(storage_commit_snapshot("/A",&a,"not-written",11,&next)==-CHOS_ERRNO_EIO&&writes==old_writes);
 fail_read=0;assert(storage_write_ready());
 assert(!storage_commit_snapshot("/A",&a,"retry",5,&next));a=next;snapshot("/A","retry");
 /* Failure of a data write or either flush quarantines all subsequent writes
  * until a verified remount; ordinary read-only access remains possible. */
 for(unsigned mode=0;mode<8;mode++){
  unsigned point=mode%4;
  reset();assert(storage_write_file("/A","original",8));a=snapshot("/A","original");
  tear_write=mode>=4;
  if(point==0)fail_write=writes+1;
  if(point==1)fail_flush=flushes+1;
  if(point==2)fail_write=writes+2; /* alternate catalogue write */
  if(point==3)fail_flush=flushes+2; /* catalogue durability acknowledgement */
  memset(&next,0xa5,sizeof next);chos_storage_lease_t old=next;
  assert(storage_commit_snapshot("/A",&a,"new-value",9,&next)==-CHOS_ERRNO_EIO&&!memcmp(&old,&next,sizeof old));
  assert(!storage_write_ready()&&strstr(storage_error(),"uncertain"));
  old_writes=writes;assert(!storage_write_file("/OTHER","blocked",7)&&writes==old_writes);
  fail_write=0;fail_flush=flushes+1;
  assert(storage_mount()&&!storage_write_ready()); /* reads cannot clear failed durability */
  fail_flush=0;int prior_flushes=flushes;
  assert(storage_mount()&&storage_write_ready()&&flushes==prior_flushes+1);unchanged_output(&a);
  /* A failed final flush is an uncertain acknowledgement: recovery may select
   * the complete new catalogue; earlier failures retain the old file. */
  a=snapshot("/A",point==3?"new-value":"original");
  assert(!storage_commit_snapshot("/A",&a,"recovered",9,&next));snapshot("/A","recovered");
 }
 /* Invalid inputs, directories, missing/undersized snapshots and missing flush
  * must not fabricate writable storage or mutate caller-owned metadata. */
 reset();assert(storage_write_file("/A","data",4)&&storage_make_directory("/D"));
 unsigned char buffer[16];size_t size=999;chos_storage_lease_t old;memset(&next,0xa5,sizeof next);old=next;
 assert(storage_read_snapshot("/MISSING",buffer,sizeof buffer,&size,&next)==-CHOS_ERRNO_ENOENT&&size==999&&!memcmp(&next,&old,sizeof next));
 assert(storage_read_snapshot("/D",buffer,sizeof buffer,&size,&next)==-CHOS_ERRNO_EISDIR);
 assert(storage_read_snapshot("/A",buffer,3,&size,&next)==-CHOS_ERRNO_E2BIG&&size==999&&!memcmp(&next,&old,sizeof next));
 assert(storage_read_snapshot("/A",NULL,4,&size,&next)==-CHOS_ERRNO_EINVAL);
 assert(storage_commit_snapshot(NULL,&old,"",0,&next)==-CHOS_ERRNO_EINVAL);
 a=snapshot("/A","data");
 assert(!storage_write_file("/MISSING/FILE","x",1)&&storage_write_ready());
 assert(!storage_commit_snapshot("/A",&a,NULL,0,&next));snapshot("/A","");
 assert(storage_make_directory("/D/SUB")&&storage_write_file("/D/SUB/F","child",5));
 a=snapshot("/D/SUB/F","child");assert(storage_rename_directory("/D","/MOVED"));
 assert(storage_commit_snapshot("/D/SUB/F",&a,"x",1,&next)==-CHOS_ERRNO_ESTALE);
 next=snapshot("/MOVED/SUB/F","child");assert(next.object==a.object&&next.revision!=a.revision);
 /* A failed snapshot read must not publish an ownership token or byte count. */
 size=999;memset(&next,0xa5,sizeof next);old=next;fail_read=reads+1;
 assert(storage_read_snapshot("/MOVED/SUB/F",buffer,sizeof buffer,&size,&next)==-CHOS_ERRNO_EIO&&size==999&&!memcmp(&next,&old,sizeof next));
 fail_read=0;assert(storage_write_ready());
 assert(storage_write_file("/A","data",4));
 a=snapshot("/A","data");chos_block_device_t noflush=device;noflush.flush=NULL;
 storage_detach();assert(storage_attach_block_device(&noflush)&&storage_mounted()&&!storage_write_ready());
 next=snapshot("/A","data");assert(storage_commit_snapshot("/A",&next,"x",1,&a)==-CHOS_ERRNO_EROFS);
 old_writes=writes;assert(!storage_write_file("/A","x",1)&&writes==old_writes&&!storage_sync());
 puts("actual storage/CHFS: independent leases,stale/ABA/remount/concurrent writers,backup-read guard,data/catalogue/flush failures,read-only and recovery passed");
}

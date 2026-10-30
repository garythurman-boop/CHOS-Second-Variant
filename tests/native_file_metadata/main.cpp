#include <chos/user.h>
#include <chos/libc.h>
#include <atomic>
#include <thread>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
static const char *path="/DOCUMENTS/IDENTITY.BIN";
static bool fail(unsigned line){printf("[FAIL] native metadata line %u\n",line);return false;}
#define CHECK(v) do{if(!(v))return fail(__LINE__);}while(0)
static bool valid(const chos_file_metadata_t& m){
 CHECK(m.version==1&&m.size_bytes==64&&!m.reserved0&&!m.reserved1&&!m.identity.reserved&&!(m.flags&~127u));
 unsigned type=m.flags&(CHOS_FILE_META_REGULAR|CHOS_FILE_META_DIRECTORY);
 CHECK(type==CHOS_FILE_META_REGULAR||type==CHOS_FILE_META_DIRECTORY);
 if(type==CHOS_FILE_META_DIRECTORY)CHECK(!(m.flags&CHOS_FILE_META_SIZE_VALID)&&!m.length);
 else CHECK(m.flags&CHOS_FILE_META_SIZE_VALID);
 CHECK(m.identity.source>=1&&m.identity.source<=3);
 if(m.flags&CHOS_FILE_META_ID_VALID){
  CHECK(m.identity.object);
  if(m.identity.source==CHOS_FILE_SOURCE_CHFS)CHECK(m.identity.mount&&m.source_revision);
  else CHECK(m.identity.source==CHOS_FILE_SOURCE_ROMFS&&!m.identity.mount&&!m.source_revision);
 }else CHECK(!m.identity.object&&!m.identity.mount&&!m.source_revision);
 CHECK(!(m.flags&CHOS_FILE_META_DIRTY)||(m.flags&CHOS_FILE_META_WORKING_COPY));return true;
}
static bool path_info(const char *p,chos_file_metadata_t &out){memset(&out,0xa5,sizeof out);CHECK(!chos_path_metadata(p,&out)&&valid(out));return true;}
static bool file_info(int fd,chos_file_metadata_t &out){memset(&out,0xa5,sizeof out);CHECK(!chos_file_metadata(fd,&out)&&valid(out));return true;}
static bool refusals(int fd){
 alignas(16) unsigned char output[96],old[96];memset(output,0xa5,sizeof output);memcpy(old,output,sizeof output);
 for(long size:{0L,63L,65L})for(unsigned operation:{CHOS_SYS_FILE_METADATA,CHOS_SYS_PATH_METADATA})
  CHECK(chos_syscall3(operation,operation==CHOS_SYS_FILE_METADATA?fd:(long)(uintptr_t)path,(long)(uintptr_t)(output+16),size)==-CHOS_ERRNO_EINVAL&&!memcmp(output,old,sizeof output));
 for(uintptr_t address:{uintptr_t(0),uintptr_t(-32),uintptr_t(0x40000000)})CHECK(chos_syscall3(CHOS_SYS_FILE_METADATA,fd,(long)address,64)==-CHOS_ERRNO_EFAULT);
 CHECK(chos_syscall3(CHOS_SYS_FILE_METADATA,(long)((UINT64_C(1)<<32)|(unsigned)fd),(long)(uintptr_t)output,64)==-CHOS_ERRNO_EBADF&&!memcmp(output,old,sizeof output));
 CHECK(chos_path_metadata("/../INVALID",(chos_file_metadata_t *)output)==-CHOS_ERRNO_EINVAL&&!memcmp(output,old,sizeof output));
 CHECK(chos_path_metadata("/ABSENT",(chos_file_metadata_t *)output)==-CHOS_ERRNO_ENOENT&&!memcmp(output,old,sizeof output));
 CHECK(chos_syscall3(CHOS_SYS_PATH_METADATA,0,(long)(uintptr_t)output,64)==-CHOS_ERRNO_EFAULT&&!memcmp(output,old,sizeof output));
 auto *map=(unsigned char *)mmap(nullptr,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);CHECK(map!=MAP_FAILED);memset(map,0xa5,8192);
 CHECK(!mprotect(map+4096,4096,PROT_READ));
 for(unsigned operation:{CHOS_SYS_FILE_METADATA,CHOS_SYS_PATH_METADATA})CHECK(chos_syscall3(operation,operation==CHOS_SYS_FILE_METADATA?fd:(long)(uintptr_t)path,(long)(uintptr_t)(map+4096-32),64)==-CHOS_ERRNO_EFAULT);
 for(unsigned i=0;i<8192;i++)CHECK(map[i]==0xa5);
 CHECK(!munmap(map,8192));return true;
}
static std::atomic<uint64_t> revisions[51];static std::atomic<unsigned> errors;static std::atomic<bool> writer_done;
static chos_file_identity_t stable;
static bool writer_body(){
 int fd=(int)chos_open_write(path,0,65536);CHECK(fd>=3);bool correct=true;
 for(uint32_t i=1;i<=50;i++){
  chos_file_metadata_t m;
  if(chos_seek(fd,0,0)||chos_file_write(fd,&i,4)!=4||chos_file_flush(fd)||chos_file_metadata(fd,&m)||m.identity!=stable){correct=false;break;}
  revisions[i].store(m.source_revision,std::memory_order_release);std::this_thread::yield();
 }
 if(chos_file_abort(fd))correct=false;return correct;
}
static void writer(){if(!writer_body())++errors;writer_done=true;}
static bool reader_body(){
 for(unsigned i=0;i<100;i++){
  int fd=(int)chos_open(path);CHECK(fd>=3);uint32_t value=999;chos_file_metadata_t m;
  long read=chos_read(fd,&value,4),info=chos_file_metadata(fd,&m),closed=chos_close(fd);
  CHECK(read==4&&!info&&!closed&&value<=50&&valid(m)&&m.identity==stable&&m.length==4&&(m.flags&CHOS_FILE_META_READ_ONLY));
  uint64_t expected=revisions[value].load(std::memory_order_acquire);
  for(unsigned wait=0;!expected&&wait<5000;wait++){
   if(writer_done){expected=revisions[value].load(std::memory_order_acquire);break;}
   std::this_thread::yield();expected=revisions[value].load(std::memory_order_acquire);
  }
  CHECK(expected&&m.source_revision==expected);
 }
 return true;
}
static void reader(){if(!reader_body())++errors;}
static bool concurrent_views(){
 uint32_t zero=0;CHECK(chos_write_file(path,&zero,4)==4);
 chos_file_metadata_t m;CHECK(path_info(path,m));stable=m.identity;
 for(auto &r:revisions)r=0;revisions[0]=m.source_revision;errors=0;writer_done=false;
 std::thread w(writer),a(reader),b(reader);bool correct=reader_body();w.join();a.join();b.join();
 CHECK(correct&&!errors&&writer_done&&revisions[50]);return true;
}
static int child(const char *mode){
 chos_file_metadata_t m,old;memset(&m,0xa5,sizeof m);old=m;
 if(!strcmp(mode,"foreign")){
  if(chos_file_metadata(3,&m)!=-CHOS_ERRNO_EBADF||memcmp(&m,&old,sizeof m))return 1;
 }else if(!strcmp(mode,"no-read")){
  if(chos_path_metadata(path,&m)!=-CHOS_ERRNO_EPERM||chos_file_metadata(3,&m)!=-CHOS_ERRNO_EPERM||memcmp(&m,&old,sizeof m))return 1;
 }else if(!strcmp(mode,"no-open")){
  if(chos_path_metadata(path,&m)!=-CHOS_ERRNO_EPERM||memcmp(&m,&old,sizeof m))return 1;
 }else{
  int fd=(int)chos_open(path);if(fd<3||chos_file_metadata(fd,&m)||!(m.flags&CHOS_FILE_META_READ_ONLY)||chos_close(fd)||chos_path_metadata(path,&m)||!valid(m)||chos_open_write(path,0,16)!=-CHOS_ERRNO_EPERM)return 1;
 }
 printf("[ OK ] native metadata permission/owner refusal %s\n",mode);return 0;
}
static bool run(){
 chos_file_metadata_t m,original,other,view;
 CHECK(path_info("/",m)&&(m.flags&CHOS_FILE_META_DIRECTORY)&&!(m.flags&CHOS_FILE_META_ID_VALID));
 CHECK(path_info("/BIN",m)&&m.identity.source==CHOS_FILE_SOURCE_NAMESPACE&&m.flags==(CHOS_FILE_META_DIRECTORY|CHOS_FILE_META_READ_ONLY));
 int rom=(int)chos_open("/BIN/HELLO.CHX");CHECK(rom>=3&&file_info(rom,m)&&path_info("/bin/hello.chx",other)&&m.identity==other.identity&&m.identity.source==2&&!m.source_revision&&!chos_close(rom));
 CHECK(chos_write_file(path,"old-data",8)==8&&path_info(path,original));
 int a=(int)chos_open(path),b=(int)chos_open(path);CHECK(a>=3&&b>=3&&file_info(a,m)&&file_info(b,other)&&m.identity==original.identity&&other.identity==m.identity&&m.source_revision==original.source_revision);
 CHECK(refusals(a));int w=(int)chos_open_write(path,0,65536);CHECK(w>=3&&file_info(w,view)&&view.identity==original.identity&&(view.flags&CHOS_FILE_META_WORKING_COPY)&&!(view.flags&CHOS_FILE_META_READ_ONLY));
 CHECK(chos_file_write(w,"new-data",8)==8&&file_info(w,m)&&(m.flags&CHOS_FILE_META_DIRTY)&&m.source_revision==original.source_revision&&path_info(path,other)&&other.source_revision==original.source_revision);
 CHECK(!chos_file_flush(w)&&file_info(w,m)&&!(m.flags&CHOS_FILE_META_DIRTY)&&m.source_revision!=original.source_revision&&m.identity==original.identity);
 CHECK(file_info(a,other)&&other.source_revision==original.source_revision&&other.identity==original.identity);
 unsigned char bytes[8];CHECK(chos_read(a,bytes,8)==8&&!memcmp(bytes,"old-data",8));
 for(const char *mode:{"foreign","no-read","no-open","no-write"}){
  uint64_t mask=CHOS_PROCESS_CAP_ALL;if(!strcmp(mode,"no-read"))mask&=~CHOS_PROCESS_CAP(CHOS_SYS_READ);if(!strcmp(mode,"no-open"))mask&=~CHOS_PROCESS_CAP(CHOS_SYS_OPEN);if(!strcmp(mode,"no-write"))mask&=~CHOS_PROCESS_CAP(CHOS_SYS_WRITE_FILE);
  long pid=chos_spawn_limited("/METADATA.ELF",mode,mask);CHECK(pid>0&&!chos_wait((uint32_t)pid));
 }
 CHECK(!chos_rename(path,"/DOCUMENTS/MOVED.BIN")&&path_info("/DOCUMENTS/MOVED.BIN",other)&&other.identity==original.identity&&other.source_revision!=m.source_revision);
 CHECK(!chos_unlink("/DOCUMENTS/MOVED.BIN")&&chos_write_file(path,"replaced",8)==8&&path_info(path,other)&&other.identity!=original.identity);
 CHECK(file_info(a,m)&&m.identity==original.identity&&chos_file_write(w,"x",1)==1&&chos_file_flush(w)==-CHOS_ERRNO_ESTALE&&!chos_file_abort(w)&&!chos_close(a)&&!chos_close(b));
 CHECK(!chos_mkdir("/DOCUMENTS/META_DIR")&&path_info("/DOCUMENTS/META_DIR",m)&&m.flags==(CHOS_FILE_META_DIRECTORY|CHOS_FILE_META_ID_VALID)&&!m.length&&!chos_rmdir("/DOCUMENTS/META_DIR"));
 CHECK(concurrent_views()&&!chos_unlink(path));
 chos_heap_stats_t heap;CHECK(!chos_libc_heap_info(&heap)&&!heap.allocations&&!heap.mapped_bytes);
 puts("[ OK ] native file identities,metadata ownership and atomic snapshot revisions passed");return true;
}
extern "C" int main(int argc,char **argv){return argc>1?child(argv[1]):(run()?0:1);}

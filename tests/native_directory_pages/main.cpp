#include <chos/user.h>
#include <chos/libc.h>
#include <cstdio>
#include <cstring>
#include <time.h>
#include <thread>
#include <atomic>
#include <sys/mman.h>
static std::atomic<unsigned long long> max_us;
static bool fail(unsigned line){printf("[FAIL] directory pages line %u\n",line);return false;}
#define CHECK(v) do{if(!(v))return fail(__LINE__);}while(0)
static chos_directory_page_t initial(){chos_directory_page_t p={};p.version=1;p.size_bytes=sizeof p;p.flags=CHOS_DIRECTORY_START;return p;}
static long page(const char *path,chos_directory_page_t &p){
 timespec start,end;if(clock_gettime(CLOCK_MONOTONIC,&start))return -CHOS_ERRNO_EIO;
 long r=chos_directory_page(path,&p);if(clock_gettime(CLOCK_MONOTONIC,&end))return -CHOS_ERRNO_EIO;
 long long micros=(end.tv_sec-start.tv_sec)*1000000+(end.tv_nsec-start.tv_nsec)/1000;
 unsigned long long previous=max_us.load();
 while(micros>0&&(unsigned long long)micros>previous&&!max_us.compare_exchange_weak(previous,(unsigned long long)micros)){}
 return r;
}
static bool valid(const chos_directory_page_t &p){
 CHECK(p.version==1&&p.size_bytes==640&&p.count<=8&&p.index<=p.total&&!(p.flags&~14u)&&!p.reserved[0]&&!p.reserved[1]);
 CHECK(bool(p.flags&CHOS_DIRECTORY_END)==(p.index==p.total));
 if(p.flags&CHOS_DIRECTORY_CHFS_PRESENT)CHECK(p.mount&&p.revision);
 for(unsigned i=0;i<p.count;i++){
  CHECK(p.entries[i].path[0]=='/'&&!(p.entries[i].flags&~31u));
  size_t n=strnlen(p.entries[i].path,64);CHECK(n<64);
  for(size_t j=n;j<64;j++)CHECK(!p.entries[i].path[j]);
 }
 for(unsigned i=p.count;i<8;i++){chos_file_list_entry_t zero={};CHECK(!memcmp(&p.entries[i],&zero,sizeof zero));}
 return true;
}
static bool refusals(){
 chos_directory_page_t p=initial(),old;
 for(unsigned n=0;n<8;n++){
  p=initial();if(n==0)p.version=2;if(n==1)p.size_bytes=639;if(n==2)p.flags=16;if(n==3)p.reserved[0]=1;
  if(n==4)p.mount=1;if(n==5)p.revision=1;if(n==6)p.index=1;if(n==7)p.flags|=CHOS_DIRECTORY_END;
  old=p;CHECK(page("/",p)==-CHOS_ERRNO_EINVAL&&!memcmp(&p,&old,sizeof p));
 }
 p=initial();old=p;
 CHECK(page("/../BAD",p)==-CHOS_ERRNO_EINVAL&&!memcmp(&p,&old,sizeof p));
 CHECK(page("/MISSING",p)==-CHOS_ERRNO_ENOENT&&!memcmp(&p,&old,sizeof p));
 CHECK(page("/DIRS.ELF",p)==-CHOS_ERRNO_ENOTDIR&&!memcmp(&p,&old,sizeof p));
 for(long n:{0L,639L,641L})CHECK(chos_syscall3(CHOS_SYS_DIRECTORY_PAGE,(long)(uintptr_t)"/",(long)(uintptr_t)&p,n)==-CHOS_ERRNO_EINVAL&&!memcmp(&p,&old,sizeof p));
 auto *mem=(unsigned char *)mmap(nullptr,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);CHECK(mem!=MAP_FAILED);memset(mem,0xa5,8192);
 memcpy(mem+4096-128,&p,sizeof p);CHECK(!mprotect(mem+4096,4096,PROT_READ));
 CHECK(chos_syscall3(CHOS_SYS_DIRECTORY_PAGE,(long)(uintptr_t)"/",(long)(uintptr_t)(mem+4096-128),640)==-CHOS_ERRNO_EFAULT);
 CHECK(!memcmp(mem+4096-128,&p,sizeof p));CHECK(!munmap(mem,8192));return true;
}
static bool many(const char *directory,unsigned files,bool extra_dirs){
 auto p=initial();unsigned count=0;
 do{
  CHECK(!page(directory,p)&&valid(p)&&p.total==files+(extra_dirs?10:0)&&p.count&&p.index==count+p.count);
  for(unsigned i=0;i<p.count;i++,count++){
   char expected[64];unsigned offset=extra_dirs?10:0;
   if(count<offset){snprintf(expected,sizeof expected,"%s/DIR%02u",directory,count);CHECK(p.entries[i].flags&CHOS_FILE_FLAG_DIRECTORY);}
   else{snprintf(expected,sizeof expected,"%s/F%03u.BIN",directory,count-offset);CHECK(!(p.entries[i].flags&CHOS_FILE_FLAG_DIRECTORY)&&p.entries[i].size_bytes==4);}
   CHECK(!strcmp(expected,p.entries[i].path)&&(p.entries[i].flags&CHOS_FILE_FLAG_PERSISTENT));
  }
 }while(!(p.flags&CHOS_DIRECTORY_END));
 CHECK(count==files+(extra_dirs?10:0));CHECK(!page(directory,p)&&valid(p)&&p.count==0&&p.flags&CHOS_DIRECTORY_END);return true;
}
static bool stale(){
 auto p=initial();CHECK(!page("/ZZZ",p)&&p.count==8);auto saved=p;
 unsigned value=1;CHECK(chos_write_file("/ZZZ/F000.BIN",&value,4)==4);
 CHECK(page("/ZZZ",p)==-CHOS_ERRNO_ESTALE&&!memcmp(&p,&saved,sizeof p));
 p=initial();CHECK(!page("/ZZZ",p));saved=p;
 CHECK(!chos_rename("/ZZZ/F174.BIN","/ZZZ/MOVED.BIN"));
 CHECK(page("/ZZZ",p)==-CHOS_ERRNO_ESTALE&&!memcmp(&p,&saved,sizeof p));
 CHECK(!chos_rename("/ZZZ/MOVED.BIN","/ZZZ/F174.BIN"));
 p=initial();CHECK(!page("/ZZZ",p));p.index=UINT64_MAX;saved=p;
 CHECK(page("/ZZZ",p)==-CHOS_ERRNO_EINVAL&&!memcmp(&p,&saved,sizeof p));
 p=initial();CHECK(!page("/ZZZ",p));p.flags^=CHOS_DIRECTORY_READ_ONLY;saved=p;
 CHECK(page("/ZZZ",p)==-CHOS_ERRNO_ESTALE&&!memcmp(&p,&saved,sizeof p));return true;
}
static std::atomic<unsigned> errors;
static bool reader(){for(unsigned i=0;i<4;i++)CHECK(many("/ZZZ",175,true));return true;}
static int child(){auto p=initial(),old=p;if(chos_directory_page("/",&p)!=-CHOS_ERRNO_EPERM||memcmp(&p,&old,sizeof p))return 1;puts("[ OK ] directory page capability refusal");return 0;}
static bool run(){
 CHECK(refusals());CHECK(!chos_mkdir("/AAA")&&!chos_mkdir("/ZZZ")&&!chos_mkdir("/EMPTY"));
 // Place hundreds of unrelated records before ZZZ. The old global64-entry
 // catalogue cannot represent this directory even though its files exist.
 for(const char *dir:{"/AAA","/ZZZ"})for(unsigned i=0;i<175;i++){char p[48];snprintf(p,sizeof p,"%s/F%03u.BIN",dir,i);CHECK(chos_write_file(p,&i,4)==4);}
 for(unsigned i=0;i<10;i++){char p[48];snprintf(p,sizeof p,"/ZZZ/DIR%02u",i);CHECK(!chos_mkdir(p));snprintf(p,sizeof p,"/ZZZ/DIR%02u/CHILD",i);CHECK(chos_write_file(p,&i,4)==4);}
 CHECK(many("/AAA",175,false)&&many("/ZZZ",175,true)&&stale());
 auto empty=initial();CHECK(!page("/EMPTY",empty)&&valid(empty)&&!empty.count&&!empty.total&&empty.flags&CHOS_DIRECTORY_END);
 auto rom=initial();unsigned rom_count=0;do{CHECK(!page("/bin",rom)&&valid(rom));for(unsigned i=0;i<rom.count;i++)CHECK(rom.entries[i].flags&CHOS_FILE_FLAG_READ_ONLY);rom_count+=rom.count;}while(!(rom.flags&CHOS_DIRECTORY_END));CHECK(rom_count==11);
 long pid=chos_spawn_limited("/DIRS.ELF","no-list",CHOS_PROCESS_CAP_ALL&~CHOS_PROCESS_CAP(CHOS_SYS_LIST_DIR));CHECK(pid>0&&!chos_wait((uint32_t)pid));
 {std::thread a([]{if(!reader())++errors;}),b([]{if(!reader())++errors;}),c([]{if(!reader())++errors;});a.join();b.join();c.join();}CHECK(!errors);
 for(unsigned i=0;i<10;i++){char p[48];snprintf(p,sizeof p,"/ZZZ/DIR%02u/CHILD",i);CHECK(!chos_unlink(p));snprintf(p,sizeof p,"/ZZZ/DIR%02u",i);CHECK(!chos_rmdir(p));}
 for(const char *dir:{"/AAA","/ZZZ"}){for(unsigned i=0;i<175;i++){char p[48];snprintf(p,sizeof p,"%s/F%03u.BIN",dir,i);CHECK(!chos_unlink(p));}CHECK(!chos_rmdir(dir));}CHECK(!chos_rmdir("/EMPTY"));
 chos_heap_stats_t heap;CHECK(!chos_libc_heap_info(&heap)&&!heap.allocations&&!heap.mapped_bytes);
 printf("directory maximum observed page call us=%llu\n",max_us.load());
 puts("[ OK ] complete native directory pages,stable restart tokens and guarded outputs passed");return true;
}
int main(int argc,char **){return argc>1?child():(run()?0:1);}

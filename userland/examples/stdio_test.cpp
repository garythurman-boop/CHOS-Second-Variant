#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <thread>
#include <atomic>
#include "chos/user.h"
#include "chos/libc.h"
static const char *path="/DOCUMENTS/CHOS-IO-TEST.TMP";
static void say(const char *s){chos_write(s,strlen(s));}
static int fail(unsigned line){char n[11];n[10]='\n';for(unsigned i=0;i<10;i++){n[9-i]=char('0'+line%10);line/=10;}say("[FAIL] native stdio line ");chos_write(n,11);return 1;}
#define CHECK(x) do {if(!(x))return fail(__LINE__);}while(0)
static unsigned char data[2001];
static FILE *shared;
static std::atomic<unsigned> histogram[256];
static void reader(){int c;unsigned count=0;while((c=fgetc(shared))!=EOF){++histogram[c];if((++count&127u)==0)std::this_thread::yield();}}
extern "C" int main(int argc,char **argv){
 if(argc>1){
  if(!strcmp(argv[1],"denied")){CHECK(chos_seek(3,0,SEEK_SET)==-CHOS_ERRNO_EPERM);say("[ OK ] seek obeys READ permission\n");return 0;}
  CHECK(chos_seek(3,0,SEEK_SET)==-CHOS_ERRNO_EBADF);say("[ OK ] seek rejects foreign process descriptor\n");return 0;
 }
 // Refuse to overwrite a pre-existing file, including a failed prior test.
 CHECK(chos_open(path)==-CHOS_ERRNO_ENOENT);
 for(unsigned i=0;i<sizeof(data);i++)data[i]=(unsigned char)i;
 CHECK(chos_write_file(path,data,sizeof(data))==(long)sizeof(data));
 long fd=chos_open(path);CHECK(fd>=3);unsigned char out[2200];
 CHECK(chos_read((int)fd,out,5)==5&&!memcmp(out,data,5));
 CHECK(chos_seek((int)fd,17,SEEK_CUR)==22&&chos_read((int)fd,out,3)==3&&!memcmp(out,data+22,3));
 CHECK(chos_seek((int)fd,-2,SEEK_END)==1999&&chos_read((int)fd,out,8)==2&&!memcmp(out,data+1999,2));
 CHECK(chos_seek((int)fd,INT64_MAX,SEEK_SET)==INT64_MAX&&chos_read((int)fd,out,1)==0);
 CHECK(chos_seek((int)fd,1,SEEK_CUR)==-CHOS_ERRNO_EOVERFLOW&&chos_seek((int)fd,0,SEEK_CUR)==INT64_MAX);
 CHECK(chos_seek((int)fd,INT64_MIN,SEEK_CUR)==-CHOS_ERRNO_EINVAL);
 CHECK(chos_seek((int)fd,-1,SEEK_SET)==-CHOS_ERRNO_EINVAL&&chos_seek((int)fd,0,3)==-CHOS_ERRNO_EINVAL);
 CHECK(chos_seek((int)fd,0,SEEK_SET)==0);
 CHECK(chos_read((int)fd,(void *)(uintptr_t)UINT64_MAX,1)==-CHOS_ERRNO_EFAULT&&chos_seek((int)fd,0,SEEK_CUR)==0);
 CHECK(chos_syscall0(CHOS_SYSCALL_LIMIT+1)==-CHOS_ERRNO_ENOSYS);
 long pid=chos_spawn_args("/BIN/STDIO.ELF","foreign");CHECK(pid>0&&chos_wait((uint32_t)pid)==0);
 uint64_t mask=(uint64_t)chos_capabilities()&~CHOS_PROCESS_CAP(CHOS_SYS_READ);
 CHECK((int64_t)mask>0);pid=chos_spawn_limited("/BIN/STDIO.ELF","denied",mask);CHECK(pid>0&&chos_wait((uint32_t)pid)==0);
 CHECK(chos_close((int)fd)==0&&chos_seek((int)fd,0,SEEK_SET)==-CHOS_ERRNO_EBADF);
 say("[ OK ] native raw seek and child boundaries passed\n");
 FILE *f=fopen(path,"rb");CHECK(f&&fileno(f)>=3);
 CHECK(fread(out,1,1,f)==1&&out[0]==0&&ftell(f)==1);
 CHECK(!fseek(f,100,SEEK_CUR)&&ftell(f)==101&&fgetc(f)==101);
 fpos_t p;CHECK(!fgetpos(f,&p)&&p.__position==102);
 CHECK(ungetc(255,f)==255&&ftell(f)==101&&fgetc(f)==255&&ftell(f)==102);
 CHECK(!fseek(f,-7,SEEK_END)&&fread(out,4,3,f)==1&&ftell(f)==2001&&feof(f)&&!ferror(f));
 CHECK(!memcmp(out,data+1994,7));CHECK(!fsetpos(f,&p)&&fgetc(f)==102&&!feof(f));
 CHECK(!fflush(f));CHECK(chos_seek(fileno(f),0,SEEK_CUR)==103);
 CHECK(fread(out,SIZE_MAX,2,f)==0&&errno==EOVERFLOW&&ferror(f));rewind(f);CHECK(!ftell(f)&&!ferror(f));
 CHECK(!fseek(f,10,SEEK_SET)&&fgets((char *)out,8,f)==(char *)out&&out[0]=='\n'&&!out[1]);
 CHECK(!fclose(f));
 FILE *all[FOPEN_MAX];for(unsigned i=0;i<FOPEN_MAX;i++){all[i]=fopen(path,"r");CHECK(all[i]);}
 CHECK(!fopen(path,"r")&&errno==EMFILE);for(auto stream:all)CHECK(!fclose(stream));
 CHECK(!fopen(path,"w")&&errno==ENOTSUP);
 // An existing CHFS open is an immutable snapshot, independent of later writes.
 f=fopen(path,"r");CHECK(f);CHECK(chos_write_file(path,"new",3)==3);CHECK(fread(out,1,sizeof(out),f)==sizeof(data)&&!memcmp(out,data,sizeof(data)));CHECK(!fclose(f));
 f=fopen(path,"r");CHECK(f&&fread(out,1,8,f)==3&&!memcmp(out,"new",3)&&!fclose(f));
 CHECK(chos_write_file(path,data,sizeof(data))==(long)sizeof(data));
 say("[ OK ] native buffered read and immutable snapshots passed\n");
 for(unsigned cycle=0;cycle<3;cycle++){
  for(auto& count:histogram)count=0;shared=fopen(path,"rb");CHECK(shared);
  std::thread threads[3];for(auto& t:threads)t=std::thread(reader);for(auto& t:threads)t.join();
  unsigned expected[256]={};for(auto c:data)expected[c]++;
  for(unsigned i=0;i<256;i++)CHECK(histogram[i]==expected[i]);
  CHECK(ftell(shared)==sizeof(data)&&feof(shared)&&!ferror(shared)&&!fclose(shared));
  chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&!stats.allocations&&!stats.mapped_bytes);
 }
 CHECK(chos_unlink(path)==0);
 say("[ OK ] native buffered read streams,seek ownership and permission boundaries passed\n");return 0;
}

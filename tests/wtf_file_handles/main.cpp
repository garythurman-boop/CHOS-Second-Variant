#include "config.h"
#include <wtf/FileHandle.h>
#include <wtf/FileSystem.h>
#include <wtf/MappedFileData.h>
#include <chos/user.h>
#include <chos/libc.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <thread>
#include <atomic>
using namespace WTF::FileSystemImpl;
static bool fail(unsigned line){printf("[FAIL] WTF file handles line %u errno %d\n",line,errno);return false;}
#define CHECK(v) do{if(!(v))return fail(__LINE__);}while(0)
static unsigned char payload[32781],output[32782];
static bool read_and_map(){
 const char *p="/DOCUMENTS/WTFVIEW.BIN";
 for(unsigned i=0;i<sizeof(payload);i++)payload[i]=(i*37u+i/17u)^0x5a;
 auto handle=FileHandle::adopt(chos_open_write(p,CHOS_FILE_WRITE_CREATE|CHOS_FILE_WRITE_TRUNCATE,65536));
 CHECK(handle&&handle.size()==0&&handle.id());auto identity=handle.id();
 CHECK(handle.write(payload)==sizeof(payload)&&handle.size()==sizeof(payload));
 CHECK(handle.seek(0,FileSeekOrigin::Beginning)==0&&handle.read(output)==sizeof(payload)&&!memcmp(payload,output,sizeof(payload)));
 CHECK(handle.read(output)==0&&handle.seek(13,FileSeekOrigin::Beginning)==13);
 {
  auto copy=handle.map(MappedFileMode::Private,FileOpenMode::ReadWrite);CHECK(copy&&copy->size()==sizeof(payload)&&!memcmp(copy->span().data(),payload,sizeof(payload)));
  copy->mutableSpan()[0]^=0xff;CHECK(handle.seek(0,FileSeekOrigin::Current)==13);
  unsigned char b=0;CHECK(chos_read_at(handle.platformHandle(),&b,1,0)==1&&b==payload[0]);
 }
 {
  auto copy=handle.map(MappedFileMode::Private,FileOpenMode::Read);CHECK(copy&&copy->size()==sizeof(payload)&&!memcmp(copy->span().data(),payload,sizeof(payload)));
  CHECK(!handle.read(copy->mutableSpan().first(1))&&errno==EFAULT&&handle.seek(0,FileSeekOrigin::Current)==13);
 }
 CHECK(!handle.map(MappedFileMode::Shared,FileOpenMode::ReadWrite)&&errno==ENOTSUP);
 CHECK(!handle.map(MappedFileMode::Private,FileOpenMode::Truncate)&&errno==ENOTSUP);
 CHECK(handle.flush());
 auto old=FileHandle::adopt(chos_open(p));CHECK(old&&old.id()==identity&&old.size()==sizeof(payload));
 CHECK(handle.truncate(17)&&handle.flush()&&handle.size()==17&&old.size()==sizeof(payload)&&old.id()==identity);
 CHECK(!handle.truncate(-1)&&errno==EINVAL&&handle.size()==17);
 CHECK(!old.write(payload)&&errno==EBADF&&!old.truncate(0)&&errno==EBADF);
 CHECK(old.seek(0,FileSeekOrigin::Beginning)==0&&old.read(output)==sizeof(payload)&&!memcmp(payload,output,sizeof(payload)));
 CHECK(chos_write_file(p,"replacement",11)==11&&handle.write(std::span<const uint8_t>(payload,1))==1);
 CHECK(!handle.flush()&&errno==ESTALE);
 // Move assignment destroys the stale dirty handle: diagnostic+explicit abort
 // must free it without overwriting the replacement.
 handle=FileHandle();CHECK(!handle);
 chos_file_metadata_t meta;CHECK(!chos_path_metadata(p,&meta)&&meta.length==11);
 {auto replacement=FileHandle::adopt(chos_open(p));CHECK(replacement&&replacement.read(output)==11&&!memcmp(output,"replacement",11));}
 old=FileHandle();CHECK(!chos_unlink(p));
 {
  auto empty=FileHandle::adopt(chos_open_write(p,CHOS_FILE_WRITE_CREATE,16));CHECK(empty);
  auto mapped=empty.map(MappedFileMode::Private,FileOpenMode::Read);CHECK(mapped&&mapped->size()==0&&mapped->span().empty());
 }
 CHECK(!chos_unlink(p));return true;
}
static bool ownership_and_failure(){
 const char *p="/DOCUMENTS/WTFVIEW.BIN";
 CHECK(chos_write_file(p,"abc",3)==3);
 long fd=chos_open(p);CHECK(fd>=3);
 auto locked=FileHandle::adopt(fd,FileLockMode::Shared);CHECK(!locked&&errno==ENOTSUP);
 chos_file_metadata_t m;CHECK(chos_file_metadata(fd,&m)==-CHOS_ERRNO_EBADF);
 {
  auto a=FileHandle::adopt(chos_open(p));CHECK(a);auto b=std::move(a);CHECK(!a&&b&&b.size()==3);
  auto c=FileHandle::adopt(chos_open(p));CHECK(c);c=std::move(b);CHECK(!b&&c);
  CHECK(!c.seek(-1,FileSeekOrigin::Beginning)&&errno==EINVAL);
  CHECK(!c.seek(0,static_cast<FileSeekOrigin>(99))&&errno==EINVAL);
 }
 FileHandle empty;CHECK(!empty.read(output)&&errno==EBADF&&!empty.write(payload)&&errno==EBADF&&!empty.flush()&&errno==EBADF&&!empty.id()&&!empty.size());
 auto bad=FileHandle::adopt(999);CHECK(!bad.read(output)&&errno==EBADF&&!bad.id()&&errno==EBADF);bad=FileHandle();
 CHECK(!chos_unlink(p));return true;
}
static bool partial_transfers(){
 const char *p="/DOCUMENTS/WTFPART.BIN";
 auto h=FileHandle::adopt(chos_open_write(p,CHOS_FILE_WRITE_CREATE|CHOS_FILE_WRITE_TRUNCATE,300));CHECK(h);
 CHECK(h.write(payload)==256&&errno==ENOSPC&&h.size()==256&&h.seek(0,FileSeekOrigin::Current)==256);
 CHECK(h.flush());
 // A fault in a later native transfer must report exactly the completed prefix.
 auto *region=(unsigned char *)mmap(nullptr,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);CHECK(region!=MAP_FAILED);
 CHECK(!mprotect(region+4096,4096,PROT_NONE));
 CHECK(h.truncate(300)&&h.seek(0,FileSeekOrigin::Beginning)==0);
 CHECK(h.read(std::span<uint8_t>(region+4096-256,300))==256&&errno==EFAULT&&h.seek(0,FileSeekOrigin::Current)==256);
 CHECK(!memcmp(region+4096-256,payload,256));
 CHECK(!mprotect(region+4096,4096,PROT_READ|PROT_WRITE)&&!munmap(region,8192));
 // Exhaust the anonymous aperture. A failed mapping must leave this live handle
 // and its sequential cursor untouched, and cleanup must restore the aperture.
 // Four pages remain reserved for the two mappings' guard pairs.
 void *a=mmap(nullptr,1024*1024,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);CHECK(a!=MAP_FAILED);
 void *b=mmap(nullptr,1024*1024-4*4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);CHECK(b!=MAP_FAILED);
 CHECK(!h.map(MappedFileMode::Private,FileOpenMode::Read)&&errno==ENOMEM&&h.seek(0,FileSeekOrigin::Current)==256);
 CHECK(!munmap(b,1024*1024-4*4096)&&!munmap(a,1024*1024));
 {auto mapped=h.map(MappedFileMode::Private,FileOpenMode::Read);CHECK(mapped&&mapped->size()==300);}
 CHECK(h.flush());h=FileHandle();CHECK(!chos_unlink(p));return true;
}
static std::atomic<unsigned> errors;
static bool worker(unsigned index){
 char p[]="/DOCUMENTS/WTF0.BIN";p[14]='0'+index;
 unsigned char data[769],got[770];for(unsigned i=0;i<sizeof data;i++)data[i]=i+index;
 for(unsigned n=0;n<20;n++){
  auto h=FileHandle::adopt(chos_open_write(p,CHOS_FILE_WRITE_CREATE|CHOS_FILE_WRITE_TRUNCATE,4096));CHECK(h&&h.write(data)==sizeof(data)&&h.flush()&&h.id());
  CHECK(h.seek(0,FileSeekOrigin::Beginning)==0&&h.read(got)==sizeof(data)&&!memcmp(data,got,sizeof data));
 }
 CHECK(!chos_unlink(p));return true;
}
static bool run(){
 CHECK(read_and_map()&&ownership_and_failure()&&partial_transfers());
 {std::thread a([]{if(!worker(0))++errors;}),b([]{if(!worker(1))++errors;}),c([]{if(!worker(2))++errors;});a.join();b.join();c.join();}
 CHECK(!errors);chos_heap_stats_t heap;CHECK(!chos_libc_heap_info(&heap)&&!heap.allocations&&!heap.mapped_bytes);
 puts("[ OK ] actual WTF native handles,private maps and durable conflict handling passed");return true;
}
int main(){return run()?0:1;}

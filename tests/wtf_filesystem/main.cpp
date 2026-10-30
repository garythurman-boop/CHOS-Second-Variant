#include "config.h"
#include <wtf/FileSystem.h>
#include <wtf/FileHandle.h>
#include <wtf/MappedFileData.h>
#include <wtf/Function.h>
#include <wtf/UniStdExtras.h>
#include <wtf/CurrentThread.h>
#include <wtf/Threading.h>
#include <wtf/WTFConfig.h>
#include <sys/mman.h>
#include <wtf/text/AtomString.h>
#include <thread>
#include <atomic>
#include <wtf/text/WTFString.h>
#include <wtf/text/CString.h>
#include <chos/user.h>
#include <chos/libc.h>
#include <cstdio>
#include <cstring>
#include <cerrno>
using namespace WTF;
namespace FS=WTF::FileSystemImpl;
static bool fail(unsigned line){printf("[FAIL] actual WTF filesystem line %u errno %d\n",line,errno);return false;}
#define CHECK(v) do{if(!(v))return fail(__LINE__);}while(0)
static unsigned char payload[32781];
static bool paths(){
 CHECK(FS::lexicallyNormal("/A/./B/../C"_s)=="/A/C"_s&&FS::lexicallyNormal("/../../A"_s)=="/A"_s);
 CHECK(FS::lexicallyNormal("../../A/./B"_s)=="../../A/B"_s);
 CHECK(FS::parentPath("/A/B"_s)=="/A"_s&&FS::parentPath("/"_s)=="/"_s&&FS::pathFileName("/A/B"_s)=="B"_s);
 CHECK(FS::pathByAppendingComponent("/A"_s,"B"_s)=="/A/B"_s&&FS::pathByAppendingComponent("/A"_s,"/B"_s)=="/B"_s);
 CHECK(FS::isAncestor("/DOCUMENTS"_s,"/documents/Child"_s)&&!FS::isAncestor("/A"_s,"/AB/C"_s)&&FS::isAncestor("/"_s,"/A"_s));
 CHECK(FS::isHiddenFile("/A/.CONFIG"_s)&&!FS::isHiddenFile("/A/CONFIG"_s));
 auto decoded=String::fromUTF8("caf\xc3\xa9");CHECK(decoded.length()==4);
 CHECK(FS::decodeFromFilename(FS::encodeForFileName("name with spaces"_s))=="name with spaces"_s);return true;
}
static bool files(){
 CHECK(FS::makeAllDirectories("/DOCUMENTS/WTF/A"_s));
 CHECK(FS::fileType("/DOCUMENTS/WTF"_s)==FS::FileType::Directory&&!FS::fileSize("/DOCUMENTS/WTF"_s));
 CHECK(!FS::makeAllDirectories("/DOCUMENTS/BAD/../OTHER"_s)&&!FS::fileExists("/DOCUMENTS/BAD"_s));
 for(unsigned i=0;i<sizeof payload;i++)payload[i]=i*37+i/17;
 CHECK(FS::overwriteEntireFile("/DOCUMENTS/WTF/A/DATA"_s,payload)==sizeof payload);
 CHECK(FS::fileSize("/DOCUMENTS/WTF/A/DATA"_s)==sizeof payload);
 {
  auto data=FS::readEntireFile("/DOCUMENTS/WTF/A/DATA"_s);CHECK(data&&data->size()==sizeof payload&&!memcmp(data->span().data(),payload,sizeof payload));
  auto h=FS::openFile("/DOCUMENTS/WTF/A/WRITEONLY"_s,FS::FileOpenMode::Truncate);CHECK(h);
  unsigned char byte;CHECK(!h.read(std::span<uint8_t>(&byte,1))&&errno==EBADF&&!h.map(FS::MappedFileMode::Private)&&errno==EBADF);
  auto moved=std::move(h);CHECK(!h&&!moved.read(std::span<uint8_t>(&byte,1))&&errno==EBADF);
  h=std::move(moved);CHECK(h&&!h.read(std::span<uint8_t>(&byte,1))&&errno==EBADF);
  CHECK(h.appendFileContents("/DOCUMENTS/WTF/A/DATA"_s)&&h.flush());
 }
 CHECK(FS::fileSize("/DOCUMENTS/WTF/A/WRITEONLY"_s)==sizeof payload);
 CHECK(FS::copyFile("/DOCUMENTS/WTF/COPY"_s,"/DOCUMENTS/WTF/A/DATA"_s));
 CHECK(!FS::copyFile("/DOCUMENTS/WTF/COPY"_s,"/documents/wtf/copy"_s)&&errno==EINVAL);
 CHECK(!FS::hardLinkOrCopyFile("/DOCUMENTS/WTF/A/DATA"_s,"/DOCUMENTS/WTF/COPY"_s)&&errno==EEXIST);
 CHECK(FS::hardLinkOrCopyFile("/DOCUMENTS/WTF/A/DATA"_s,"/DOCUMENTS/WTF/LINKCOPY"_s));
 CHECK(FS::fileSize("/DOCUMENTS/WTF/LINKCOPY"_s)==sizeof payload&&FS::deleteFile("/DOCUMENTS/WTF/LINKCOPY"_s));
 CHECK(FS::moveFile("/DOCUMENTS/WTF/COPY"_s,"/DOCUMENTS/WTF/MOVED"_s)&&!FS::fileExists("/DOCUMENTS/WTF/COPY"_s));
 auto list=FS::listDirectory("/DOCUMENTS/WTF"_s);CHECK(list.size()==2&&list[0]=="A"_s&&list[1]=="MOVED"_s);
 unsigned seen=0;FS::traverseDirectory("/DOCUMENTS/WTF/A"_s,[&](const String&,FS::FileType type){if(type==FS::FileType::Regular)seen++;});CHECK(seen==2);
 CHECK(FS::directorySize("/DOCUMENTS/WTF"_s)==3*sizeof payload);
 CHECK(FS::volumeFileBlockSize("/DOCUMENTS/WTF/MOVED"_s)==512&&!FS::fileCreationTime("/DOCUMENTS/WTF/MOVED"_s));
 CHECK(!FS::volumeCapacity("/DOCUMENTS/WTF"_s)&&errno==ENOTSUP&&!FS::updateFileModificationTime("/DOCUMENTS/WTF"_s)&&errno==ENOTSUP);
 {
  auto h=FS::openFile("/DOCUMENTS/WTF/MOVED"_s,FS::FileOpenMode::Read);CHECK(h&&WTF::setCloseOnExec(h.platformHandle())&&WTF::setNonBlock(h.platformHandle()));
  CHECK(!WTF::unsetCloseOnExec(h.platformHandle())&&errno==ENOTSUP&&WTF::dupCloseOnExec(h.platformHandle())==-1&&errno==ENOTSUP);
 }
 CHECK(!FS::openFile("/DOCUMENTS/LOCKED"_s,FS::FileOpenMode::Truncate,FS::FileAccessPermission::User,FS::FileLockMode::Exclusive));
 CHECK(errno==ENOTSUP&&!FS::fileExists("/DOCUMENTS/LOCKED"_s));
 CHECK(!FS::createMappedFileData("/DOCUMENTS/SHARED"_s,4096)&&errno==ENOTSUP&&!FS::fileExists("/DOCUMENTS/SHARED"_s));
 CHECK(!chos_mkdir("/DOCUMENTS/WTF/MANY"));
 for(unsigned i=0;i<70;i++){char path[48];snprintf(path,sizeof path,"/DOCUMENTS/WTF/MANY/F%03u",i);CHECK(chos_write_file(path,&i,4)==4);}
 auto large=FS::listDirectory("/DOCUMENTS/WTF/MANY"_s);CHECK(large.size()==70);
 for(unsigned i=0;i<70;i++){char name[16];snprintf(name,sizeof name,"F%03u",i);CHECK(large[i]==String::fromUTF8(name));}
 auto salt=FS::readOrMakeSalt("/DOCUMENTS/WTF/SALT"_s);CHECK(salt&&FS::fileSize("/DOCUMENTS/WTF/SALT"_s)==8&&FS::readOrMakeSalt("/DOCUMENTS/WTF/SALT"_s)==salt);
 CHECK(FS::deleteNonEmptyDirectory("/DOCUMENTS/WTF"_s)&&!FS::fileExists("/DOCUMENTS/WTF"_s));
 CHECK(!FS::deleteNonEmptyDirectory("/"_s));return true;
}
static bool temporary(){
 CHECK(!FS::openTemporaryFile("../ESCAPE"_s).second&&errno==EINVAL);
 auto [path,handle]=FS::openTemporaryFile("TEST"_s,".TMP"_s);CHECK(handle&&path.startsWith("/TMP/TEST"_s)&&path.endsWith(".TMP"_s));
 CHECK(handle.write(std::span<const uint8_t>(payload,512))==512&&handle.flush());handle=FS::FileHandle();CHECK(FS::deleteFile(path));
 auto directory=FS::createTemporaryDirectory();CHECK(!directory.isEmpty()&&FS::fileType(directory)==FS::FileType::Directory&&FS::deleteEmptyDirectory(directory));
 CHECK(FS::deleteEmptyDirectory("/TMP"_s));return true;
}
static std::atomic<unsigned> worker_errors,worker_ready,worker_go;
static std::atomic<uint32_t> worker_ids[3];
static bool adopted_worker(unsigned i){
 auto id=WTF::currentThreadID();CHECK(id&&id==WTF::currentThreadID());worker_ids[i]=id;
 int local=0;CHECK(Thread::currentSingleton().stack().contains(&local));
 ++worker_ready;while(!worker_go.load())std::this_thread::yield();
 for(unsigned n=0;n<16;n++){
  AtomString value(String::fromUTF8("native-wtf-thread-atom"));CHECK(value.string()=="native-wtf-thread-atom"_s);
  auto entries=FS::listDirectory("/BIN"_s);CHECK(entries.size()==11);
 }
 return true;
}
static bool current_threads(){
 auto parent=WTF::currentThreadID();CHECK(parent&&parent==WTF::currentThreadID());
 int local=0;CHECK(Thread::currentSingleton().stack().contains(&local));
 chos_heap_stats_t warm={};
 for(unsigned wave=0;wave<3;wave++){
  worker_ready=0;worker_go=0;bool registered=false;
  {std::thread a([]{if(!adopted_worker(0))++worker_errors;}),b([]{if(!adopted_worker(1))++worker_errors;}),c([]{if(!adopted_worker(2))++worker_errors;});
   while(worker_ready.load()!=3)std::this_thread::yield();
   registered=Thread::allThreads().sizeIncludingEmptyEntriesForTesting()==4;
   worker_go=1;a.join();b.join();c.join();}
  CHECK(registered&&!worker_errors&&Thread::allThreads().sizeIncludingEmptyEntriesForTesting()==1);
  for(unsigned i=0;i<3;i++){CHECK(worker_ids[i]&&worker_ids[i]!=parent);for(unsigned j=0;j<i;j++)CHECK(worker_ids[i]!=worker_ids[j]);}
  chos_heap_stats_t heap;CHECK(!chos_libc_heap_info(&heap));
  if(!wave)warm=heap;else CHECK(heap.allocations==warm.allocations&&heap.mapped_bytes==warm.mapped_bytes);
 }
 puts("[ OK ] actual WTF current-thread adoption,stack bounds and TLS cleanup passed");return true;
}
static bool run(){
 CHECK(paths()&&files()&&temporary()&&current_threads());
 // Exercise the actual library freeze path, not a replacement flag or no-op.
 WTF::Config::finalize();CHECK(g_wtfConfig.isPermanentlyFrozen);
 CHECK(mprotect(WebConfig::g_config,WTF::ConfigSizeToProtect,PROT_READ|PROT_WRITE)==-1&&errno==EINVAL);
 long pid=chos_spawn_limited("/WTFFS.ELF","freeze-write",CHOS_PROCESS_CAP_ALL);CHECK(pid>0);
 CHECK(chos_wait((uint32_t)pid)==(-128-0x24)&&chos_getstatus(pid)==CHOS_STATUS_FAILED);
 CHECK(g_wtfConfig.isPermanentlyFrozen);
 puts("[ OK ] actual WTF config frozen read-only;write restoration and child write refused");
 chos_heap_stats_t heap;CHECK(!chos_libc_heap_info(&heap));
 printf("actual WTF heap before CRT exit: allocations=%llu bytes=%llu\n",(unsigned long long)heap.allocations,(unsigned long long)heap.mapped_bytes);
 puts("[ OK ] linked actual WTF String,Vector and native filesystem operations passed");return true;
}
int main(int argc,char **argv){
 if(argc>1&&!strcmp(argv[1],"freeze-write")){
  WTF::initialize();WTF::Config::finalize();
  if(!g_wtfConfig.isPermanentlyFrozen)return 2;
  auto *p=reinterpret_cast<volatile unsigned char *>(WebConfig::g_config);
  unsigned char value=*p;*p=value;
  puts("[FAIL] frozen WTF configuration remained writable");return 3;
 }
 return run()?0:1;
}

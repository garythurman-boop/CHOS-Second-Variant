#include <array>
#include <vector>
#include <string>
#include <algorithm>
#include <memory>
#include <atomic>
#include <new>
#include <limits>
#include <pthread.h>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cerrno>
#include "chos/libc.h"
#include "chos/user.h"
static void say(const char *s){chos_write(s,std::strlen(s));}
static int fail(unsigned line){char n[11];n[10]='\n';for(unsigned i=0;i<10;i++){n[9-i]=char('0'+line%10);line/=10;}say("[FAIL] native libc++ line ");chos_write(n,11);return 1;}
#define CHECK(x) do{if(!(x))return fail(__LINE__);}while(0)
static std::atomic<unsigned> live{0},completed{0},handler_calls{0};
struct Item {
 int value;
 explicit Item(int v):value(v){++live;}
 Item(const Item& x):value(x.value){++live;}
 Item(Item&& x)noexcept:value(x.value){++live;x.value=-1;}
 Item& operator=(Item&& x)noexcept{value=x.value;x.value=-1;return *this;}
 Item& operator=(const Item& x){value=x.value;return *this;}
 ~Item(){--live;}
};
struct alignas(256) Aligned {char payload[257];};
static void handler(){++handler_calls;std::set_new_handler(nullptr);}
static int containers(unsigned seed){
 std::array<int,4> a{{4,2,3,1}};std::sort(a.begin(),a.end());CHECK(a.front()==1&&a.back()==4);
 std::vector<Item> objects;for(unsigned i=0;i<257;i++)objects.emplace_back(int(i+seed));
 CHECK(objects.size()==257&&objects.front().value==int(seed));
 auto copied=objects;auto moved=std::move(copied);CHECK(moved.size()==257&&moved.back().value==int(256+seed));
 objects.erase(objects.begin()+1,objects.begin()+100);CHECK(objects.size()==158&&objects[1].value==int(100+seed));
 std::vector<int> values;for(int i=511;i>=0;i--)values.push_back(i%127);
 std::sort(values.begin(),values.end());auto end=std::unique(values.begin(),values.end());values.erase(end,values.end());
 CHECK(values.size()==127&&values.front()==0&&values.back()==126);
 CHECK(std::binary_search(values.begin(),values.end(),87));
 std::string text="native ";for(unsigned i=0;i<100;i++)text+="UTF-8: \xf0\x9f\x98\x80 ";
 text.insert(0,"CHOS ");text.replace(0,4,"CHOS");text.erase(5,7);
 CHECK(text.find("UTF-8:")==5&&text.size()==1205);auto moved_text=std::move(text);CHECK(moved_text.substr(5,5)=="UTF-8");
 auto owner=std::make_unique<Item>(72);CHECK(owner->value==72);
 auto array=std::make_unique<Aligned[]>(3);CHECK(reinterpret_cast<uintptr_t>(array.get())%alignof(Aligned)==0);
 array[2].payload[256]=42;CHECK(array[2].payload[256]==42);
 void *p=::operator new(1,std::align_val_t(4096),std::nothrow);CHECK(p&&reinterpret_cast<uintptr_t>(p)%4096==0);::operator delete(p,std::align_val_t(4096));
 return 0;
}
static void *worker(void *p){unsigned seed=unsigned(reinterpret_cast<uintptr_t>(p));
 for(unsigned i=0;i<8;i++){if(containers(seed+i))return reinterpret_cast<void *>(1);chos_yield();chos_sleep_ticks(1);}
 ++completed;return reinterpret_cast<void *>(uintptr_t(0x100+seed));
}
static int files(){
 CHECK(chos_write_file("/DOCUMENTS/CXX-A.TXT","native",6)==6);
 CHECK(!std::rename("/DOCUMENTS/CXX-A.TXT","/DOCUMENTS/CXX-B.TXT"));
 long fd=chos_open("/DOCUMENTS/CXX-B.TXT");char b[6];CHECK(fd>=0&&chos_read(int(fd),b,6)==6&&!std::memcmp(b,"native",6)&&chos_close(int(fd))==0);
 CHECK(!std::remove("/DOCUMENTS/CXX-B.TXT"));
 CHECK(chos_mkdir("/DOCUMENTS/CXX-DIR")==0);
 CHECK(chos_write_file("/DOCUMENTS/CXX-DIR/A.TXT","a",1)==1);
 CHECK(std::remove("/DOCUMENTS/CXX-DIR")==-1&&errno==ENOTEMPTY);
 CHECK(!std::remove("/DOCUMENTS/CXX-DIR/A.TXT")&&!std::remove("/DOCUMENTS/CXX-DIR"));
 CHECK(std::remove("/DOCUMENTS/CXX-MISSING.TXT")==-1&&errno==ENOENT);
 CHECK(std::remove("/CFG/NETWORK.CFG")==-1&&errno==EPERM);
 CHECK(chos_write_file("/DOCUMENTS/CXX-A.TXT","a",1)==1&&chos_write_file("/DOCUMENTS/CXX-B.TXT","b",1)==1);
 CHECK(std::rename("/DOCUMENTS/CXX-A.TXT","/DOCUMENTS/CXX-B.TXT")==-1&&errno==EEXIST);
 CHECK(!std::remove("/DOCUMENTS/CXX-A.TXT")&&!std::remove("/DOCUMENTS/CXX-B.TXT"));
 return 0;
}
extern "C" int main(int argc,char **argv){
 if(argc>1){
  if(!std::strcmp(argv[1],"oom")){volatile size_t huge=std::numeric_limits<size_t>::max();void *p=::operator new(huge);::operator delete(p);return fail(__LINE__);}
  if(!std::strcmp(argv[1],"bounds")){std::vector<int> v{1,2};volatile size_t index=3;return v[index];}
  return fail(__LINE__);
 }
 static_assert(std::atomic<unsigned>::is_always_lock_free);
 static_assert(std::atomic<uint64_t>::is_always_lock_free);
 CHECK(containers(11)==0&&live==0&&files()==0);
 CHECK(std::get_new_handler()==nullptr&&std::set_new_handler(handler)==nullptr);
 volatile size_t huge=std::numeric_limits<size_t>::max();
 void *p=::operator new(huge,std::nothrow);CHECK(!p&&handler_calls==1&&std::get_new_handler()==nullptr);
 p=::operator new(huge,std::align_val_t(256),std::nothrow);CHECK(!p);
 for(unsigned lifecycle=0;lifecycle<3;lifecycle++){
  pthread_t threads[3];for(unsigned i=0;i<3;i++)CHECK(!pthread_create(&threads[i],nullptr,worker,reinterpret_cast<void *>(uintptr_t(i))));
  for(unsigned i=0;i<3;i++){void *value=nullptr;CHECK(!pthread_join(threads[i],&value)&&value==reinterpret_cast<void *>(uintptr_t(0x100+i)));}
  CHECK(live==0&&completed==3*(lifecycle+1));
 }
 chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&stats.allocations==0&&stats.mapped_bytes==0);
 say("[ OK ] native libc++ containers,RAII,allocation,atomics,threads and filenames passed\n");return 0;
}

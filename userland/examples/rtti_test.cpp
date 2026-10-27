#include "rtti_types.h"
#include <thread>
#include <typeindex>
#include <memory>
#include <new>
#include <cctype>
#include <cinttypes>
#include <cstring>
#include <cerrno>
#include <atomic>
#include "chos/libc.h"
#include "chos/user.h"
static void say(const char *s){chos_write(s,std::strlen(s));}
static int fail(unsigned line){char n[11];n[10]='\n';for(unsigned i=0;i<10;i++){n[9-i]=char('0'+line%10);line/=10;}say("[FAIL] RTTI/C byte API line ");chos_write(n,11);return 1;}
#define CHECK(x) do{if(!(x))return fail(__LINE__);}while(0)
__attribute__((noinline)) static Diamond *as_diamond(Root *p){return dynamic_cast<Diamond *>(p);}
static int graph(){
 std::unique_ptr<Root> p(rtti_factory());auto *d=as_diamond(p.get());CHECK(d&&d->leaf==37&&d->root==17&&p->value()==99);
 CHECK(dynamic_cast<Left *>(p.get())==static_cast<Left *>(d));CHECK(dynamic_cast<Right *>(p.get())==static_cast<Right *>(d));
 CHECK(dynamic_cast<Side *>(p.get())==static_cast<Side *>(d));CHECK(dynamic_cast<void *>(static_cast<Right *>(d))==d);
 CHECK(!dynamic_cast<Other *>(p.get())&&!as_diamond(nullptr));CHECK(&dynamic_cast<Diamond&>(*p)==d);
 Root *raw=p.get();CHECK(typeid(*raw)==typeid(Diamond)&&rtti_factory_type()==typeid(Diamond));
 CHECK(std::type_index(typeid(*raw))==std::type_index(typeid(Diamond)));CHECK(typeid(*raw).hash_code()==typeid(Diamond).hash_code());
 CHECK(typeid(int)!=typeid(long)&&typeid(void)!=typeid(int));CHECK(std::strlen(typeid(Diamond).name())>0);
 CHECK(typeid(int[3])!=typeid(int[4])&&typeid(int*)!=typeid(const int*));
 CHECK(typeid(int Root::*)!=typeid(int Side::*));
 Ambiguous a;Side *s=&a;CHECK(!dynamic_cast<X *>(s));CHECK(dynamic_cast<X1 *>(s)==static_cast<X1 *>(&a));
 Hidden h;CHECK(!dynamic_cast<Hidden *>(h.expose())&&dynamic_cast<void *>(h.expose())==&h);
 std::bad_alloc bad;std::exception *base=&bad;CHECK(dynamic_cast<std::bad_alloc *>(base)==&bad&&typeid(*base)==typeid(std::bad_alloc));
 return 0;
}
static std::atomic<unsigned> failures{};
static void worker(){for(unsigned i=0;i<64;i++){if(graph())++failures;std::this_thread::yield();}}
extern "C" int main(int argc,char **argv){
 if(argc>1){
  if(!std::strcmp(argv[1],"bad-cast")){say("failed reference cast refusal begins\n");Other value;Root *p=&value;(void)dynamic_cast<Diamond&>(*p);return 1;}
  if(!std::strcmp(argv[1],"null-typeid")){say("null polymorphic typeid refusal begins\n");Root *p=nullptr;(void)typeid(*p);return 1;}
  return fail(__LINE__);
 }
 for(int c=-1;c<=255;c++){
  bool alpha=(c>='a'&&c<='z')||(c>='A'&&c<='Z'),digit=c>='0'&&c<='9';
  CHECK(!!std::isalpha(c)==alpha&&!!std::isdigit(c)==digit&&!!std::isalnum(c)==(alpha||digit));
  CHECK(std::tolower(c)==((c>='A'&&c<='Z')?c+32:c));CHECK(std::toupper(c)==((c>='a'&&c<='z')?c-32:c));
 }
 char *end=nullptr;errno=0;CHECK(std::strtoimax(" -9223372036854775808x",&end,10)==INTMAX_MIN&&*end=='x'&&!errno);
 CHECK(std::strtoumax("18446744073709551615",&end,10)==UINTMAX_MAX&&!*end);
 errno=0;CHECK(std::strtoimax("9223372036854775808",&end,10)==INTMAX_MAX&&errno==ERANGE);
 auto division=std::imaxdiv(-37,5);CHECK(division.quot==-7&&division.rem==-2&&std::imaxabs(-91)==91);
 for(unsigned cycle=0;cycle<3;cycle++){
  std::thread threads[3];for(auto& t:threads)t=std::thread(worker);for(auto& t:threads)t.join();CHECK(!failures);
  chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&!stats.allocations&&!stats.mapped_bytes);
 }
 say("[ OK ] native RTTI typeid/cross-casts,byte classification and intmax conversions passed\n");return 0;
}

#include <expected>
#include <numbers>
#include <bit>
#include <cstdint>
#include <unordered_map>
#include <memory>
#include <string>
#include <array>
#include <atomic>
#include <thread>
#include <cstring>
#include "chos/user.h"
#include "chos/libc.h"
static void say(const char *s){chos_write(s,std::strlen(s));}
static int fail(unsigned line){char b[80];unsigned at=0;const char *s="[FAIL] native expected/hash line ";while(*s)b[at++]=*s++;char d[16];unsigned n=0;do{d[n++]=(char)('0'+line%10);line/=10;}while(line);while(n)b[at++]=d[--n];b[at++]='\n';chos_write(b,at);return 1;}
#define CHECK(x) do{if(!(x))return fail(__LINE__);}while(0)
static_assert(std::expected<int,int>(17).value()==17);
static bool prime(size_t n){if(n<2)return false;for(size_t d=2;d<=n/d;d++)if(!(n%d))return false;return true;}
static size_t model(size_t n){if(!n)return 0;while(!prime(n))n++;return n;}
static int constants(){
 CHECK(std::bit_cast<uint32_t>(std::numbers::pi_v<float>)==UINT32_C(0x40490fdb));
 CHECK(std::bit_cast<uint64_t>(std::numbers::pi)==UINT64_C(0x400921fb54442d18));
 CHECK(std::bit_cast<uint64_t>(std::numbers::e)==UINT64_C(0x4005bf0a8b145769));
 CHECK(std::bit_cast<uint64_t>(std::numbers::sqrt2)==UINT64_C(0x3ff6a09e667f3bcd));
 // LLVM 19's original numbers header uses unsuffixed double literals even
 // for long double. Record that upstream precision limit, not a quad claim.
 CHECK(std::numbers::pi_v<long double> ==static_cast<long double>(std::numbers::pi));
 CHECK(std::numbers::pi_v<long double> !=0x1.921fb54442d18469898cc51701b8p+1L);return 0;
}
static int prime_checks(){
 for(size_t n=0;n<=4096;n++)CHECK(std::__next_prime(n)==model(n));
 for(size_t n:{size_t(10000),size_t(65536),size_t(99991),size_t(1000000)})CHECK(std::__next_prime(n)==model(n));return 0;
}
struct collisions {size_t operator()(int v)const noexcept{return static_cast<size_t>(v)%7u;}};
static int maps(){
 std::unordered_map<int,int,collisions> values;std::array<int,257> expected{};std::array<bool,257> live{};
 for(unsigned i=0;i<1024;i++){
  unsigned key=(i*17u+3u)%257u;
  if(i%5){int value=static_cast<int>(i*31u+7u);values.insert_or_assign(static_cast<int>(key),value);expected[key]=value;live[key]=true;}
  else {CHECK(values.erase(static_cast<int>(key))==static_cast<size_t>(live[key]));live[key]=false;}
  if(i%64==63)for(unsigned k=0;k<257;k++){auto at=values.find(static_cast<int>(k));CHECK((at!=values.end())==live[k]);if(live[k])CHECK(at->second==expected[k]);}
 }
 values.reserve(512);CHECK(values.bucket_count()>=512&&values.load_factor()<=values.max_load_factor());
 auto copy=values;CHECK(copy==values);std::unordered_map<int,int,collisions> moved=std::move(copy);CHECK(moved==values);
 if(!moved.empty()){int key=moved.begin()->first,value=moved.begin()->second;auto node=moved.extract(key);CHECK(node&&node.mapped()==value&&!moved.contains(key));auto result=moved.insert(std::move(node));CHECK(result.inserted&&result.position->second==value);}
 values.clear();values.rehash(0);CHECK(values.empty()&&moved.size()>0);return 0;
}
static int expected_checks(){
 std::expected<std::string,int> value(std::in_place,"alpha"),error(std::unexpect,7);
 CHECK(value&&*value=="alpha"&&!error&&error.error()==7);
 auto length=value.and_then([](const std::string& s)->std::expected<int,int>{return static_cast<int>(s.size());});CHECK(length&&*length==5);
 unsigned calls=0;auto skipped=error.and_then([&](const std::string&)->std::expected<int,int>{++calls;return 9;});CHECK(!skipped&&skipped.error()==7&&!calls);
 auto changed=error.transform_error([](int code){return std::string(code==7?"seven":"other");});CHECK(!changed&&changed.error()=="seven");
 auto transformed=value.transform([](const std::string& s){return s+" beta";});CHECK(transformed&&*transformed=="alpha beta");
 auto recovered=error.or_else([](int)->std::expected<std::string,int>{return "recovered";});CHECK(recovered&&*recovered=="recovered");
 value.swap(error);CHECK(!value&&value.error()==7&&error&&*error=="alpha");value=error;CHECK(value&&*value=="alpha");value=std::unexpected(19);CHECK(!value&&value.value_or("fallback")=="fallback");
 std::expected<std::unique_ptr<int>,int> pointer(std::in_place,std::make_unique<int>(42));
 auto consumed=std::move(pointer).and_then([](std::unique_ptr<int> p)->std::expected<int,int>{return *p+1;});CHECK(consumed&&*consumed==43);
 std::expected<std::string,int> large(std::in_place,200,'x');auto moved=std::move(large);CHECK(moved&&moved->size()==200&&moved->front()=='x');moved=std::unexpected(5);CHECK(!moved&&moved.error()==5);
 std::expected<void,int> empty;CHECK(empty);std::expected<void,int> bad(std::unexpect,13);CHECK(!bad&&bad.error()==13);
 auto chain=empty.and_then([]()->std::expected<int,int>{return 23;});CHECK(chain&&*chain==23);
 auto fixed=bad.or_else([](int)->std::expected<void,int>{return {};});CHECK(fixed);return 0;
}
static std::atomic<unsigned> failures;
static void worker(){for(unsigned cycle=0;cycle<3;cycle++){if(maps()||expected_checks())++failures;std::this_thread::yield();}}
extern "C" int main(){
 CHECK(!constants()&&!prime_checks()&&!maps()&&!expected_checks());
 for(unsigned cycle=0;cycle<3;cycle++){
  std::thread threads[3];for(auto &t:threads)t=std::thread(worker);for(auto &t:threads)t.join();CHECK(!failures);
  chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&!stats.allocations&&!stats.mapped_bytes);
 }
 say("[ OK ] native original hash helper, unordered containers and C++23 expected passed\n");return 0;
}

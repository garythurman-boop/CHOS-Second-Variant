#include <map>
#include <set>
#include <string>
#include <string_view>
#include <array>
#include <thread>
#include <atomic>
#include <cstdarg>
#include <cstring>
#include "chos/user.h"
#include "chos/libc.h"
static void say(const char *s){chos_write(s,std::strlen(s));}
static int fail(unsigned line){char n[11];n[10]='\n';for(unsigned i=0;i<10;i++){n[9-i]=char('0'+line%10);line/=10;}say("[FAIL] native ordered line ");chos_write(n,11);return 1;}
#define CHECK(x) do{if(!(x))return fail(__LINE__);}while(0)
static int variadic(int marker,...){
 std::va_list first,copy;va_start(first,marker);va_copy(copy,first);
 for(unsigned i=0;i<2;i++){
  std::va_list &p=i?copy:first;
  CHECK(va_arg(p,int)==-73&&va_arg(p,double)==2.5&&va_arg(p,long double)==3.25L);
  CHECK(va_arg(p,unsigned long long)==UINT64_C(0xfedcba9876543210));
  CHECK(!std::strcmp(va_arg(p,const char *),"arguments"));
 }
 va_end(copy);va_end(first);return 0;
}
static int model(unsigned seed){
 std::map<int,int> tree;std::set<int> keys;int values[64]={};bool present[64]={};unsigned random=seed+1;
 for(unsigned iteration=0;iteration<3000;iteration++){
  random=random*1664525u+1013904223u;unsigned k=(random>>16)&63u;unsigned op=random&3u;
  if(op==0){auto r=tree.try_emplace((int)k,(int)random);CHECK(r.second==!present[k]);if(!present[k])values[k]=(int)random;present[k]=true;keys.insert((int)k);}
  else if(op==1){tree.insert_or_assign((int)k,(int)random);values[k]=(int)random;present[k]=true;keys.insert((int)k);}
  else if(op==2){CHECK(tree.erase((int)k)==(unsigned)present[k]);CHECK(keys.erase((int)k)==(unsigned)present[k]);present[k]=false;}
  else if(present[k]){auto node=tree.extract((int)k);CHECK(!node.empty()&&node.key()==(int)k&&node.mapped()==values[k]);auto r=tree.insert(std::move(node));CHECK(r.inserted&&r.node.empty()&&node.empty());}
  if((iteration&15u)==0){
   auto it=tree.begin();auto sit=keys.begin();unsigned count=0;
   for(unsigned j=0;j<64;j++){
    CHECK(tree.contains((int)j)==present[j]&&keys.contains((int)j)==present[j]);
    auto lower=tree.lower_bound((int)j);unsigned expected=j;while(expected<64&&!present[expected])expected++;
    CHECK(expected==64?lower==tree.end():lower!=tree.end()&&lower->first==(int)expected);
    if(present[j]){CHECK(it!=tree.end()&&it->first==(int)j&&it->second==values[j]);CHECK(sit!=keys.end()&&*sit==(int)j);++it;++sit;++count;}
   }
   CHECK(it==tree.end()&&sit==keys.end()&&tree.size()==count&&keys.size()==count);
  }
 }
 auto copy=tree;CHECK(copy==tree);auto moved=std::move(copy);CHECK(moved==tree);moved.clear();CHECK(moved.empty());
 std::map<int,int> collision{{1,10},{2,20}};auto node=collision.extract(1);node.key()=2;auto r=collision.insert(std::move(node));CHECK(!r.inserted&&!r.node.empty()&&r.position->second==20);r.node.key()=3;CHECK(collision.insert(std::move(r.node)).inserted);
 std::map<int,int> other{{2,99},{4,40}};collision.merge(other);CHECK(collision.size()==3&&other.size()==1&&other.at(2)==99);
 std::multimap<int,int> multi{{2,20},{1,10},{2,21}};auto range=multi.equal_range(2);CHECK(multi.count(2)==2&&range.first->second==20);++range.first;CHECK(range.first->second==21&&++range.first==range.second);
 std::multiset<int> duplicates{2,1,2,3};CHECK(duplicates.count(2)==2&&duplicates.erase(2)==2&&duplicates.size()==2);
 std::map<std::string,int,std::less<>> names{{"alpha",1},{"beta",2},{"gamma",3}};
 CHECK(names.find(std::string_view("beta"))->second==2&&names.contains("gamma")&&!names.contains("absent"));
#if __cplusplus>202002L
 std::array<std::pair<int,int>,3> pairs{{{8,80},{6,60},{7,70}}};std::map<int,int> from(std::from_range,pairs);CHECK(from.size()==3&&from.begin()->first==6);from.insert_range(pairs);CHECK(from.size()==3);
 std::array<int,4> numbers{4,2,4,3};std::set<int> unique(std::from_range,numbers);CHECK(unique.size()==3&&*unique.rbegin()==4);
#endif
 CHECK(!variadic(9,-73,2.5,3.25L,UINT64_C(0xfedcba9876543210),"arguments"));return 0;
}
static std::atomic<unsigned> failures;
static void worker(unsigned seed){if(model(seed))++failures;std::this_thread::yield();}
extern "C" int main(){
 CHECK(!model(3));
 for(unsigned cycle=0;cycle<3;cycle++){
  std::thread threads[3];for(unsigned i=0;i<3;i++)threads[i]=std::thread(worker,cycle*3+i);for(auto& thread:threads)thread.join();CHECK(!failures);
  chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&!stats.allocations&&!stats.mapped_bytes);
 }
 say("[ OK ] native ordered containers and mixed variadic ABI passed\n");return 0;
}

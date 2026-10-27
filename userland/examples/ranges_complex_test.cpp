#include <complex>
#include <ranges>
#include <span>
#include <array>
#include <vector>
#include <string>
#include <string_view>
#include <algorithm>
#include <thread>
#include <atomic>
#include <cstring>
#include <cmath>
#include <cstdint>
#include "chos/user.h"
#include "chos/libc.h"
#include "complex_vectors.h"
static void say(const char *s){chos_write(s,std::strlen(s));}
static int fail(unsigned line){char n[11];n[10]='\n';for(unsigned i=0;i<10;i++){n[9-i]=char('0'+line%10);line/=10;}say("[FAIL] C++ ranges/complex line ");chos_write(n,11);return 1;}
#define CHECK(x) do{if(!(x))return fail(__LINE__);}while(0)
static double from64(uint64_t u){union{uint64_t u;double f;}v={u};return v.f;}
static float from32(uint32_t u){union{uint32_t u;float f;}v={u};return v.f;}
static uint64_t bits64(double f){union{double f;uint64_t u;}v={f};return v.u;}
static uint32_t bits32(float f){union{float f;uint32_t u;}v={f};return v.u;}
static void hex(uint64_t n){char out[17];out[16]='\n';for(unsigned i=0;i<16;i++)out[15-i]="0123456789abcdef"[(n>>(4*i))&15];chos_write(out,sizeof(out));}
// Complex elementary functions propagate error between real/imag components.
// Bound the error by the result magnitude, rather than component ULPs near
// cancellation. The reference bits stay those of the high-precision oracle.
static double largest_eps[2]{};
static bool close_complex(uint64_t re,uint64_t im,const complex_vector_t& v,bool record){
 double ar=v.width==32?double(from32(re)):from64(re),ai=v.width==32?double(from32(im)):from64(im);
 double er=v.width==32?double(from32(v.re)):from64(v.re),ei=v.width==32?double(from32(v.im)):from64(v.im);
 double magnitude=std::max(std::abs(er),std::abs(ei));
 double error=std::max(std::abs(ar-er),std::abs(ai-ei));
 double unit=(v.width==32?0x1p-23:0x1p-52)*magnitude;
 if(record&&error/unit>largest_eps[v.width==64])largest_eps[v.width==64]=error/unit;
 return std::isfinite(ar)&&std::isfinite(ai)&&error<=v.error_eps*unit;
}
template<class T>__attribute__((noinline)) static std::complex<T> evaluate(unsigned op,std::complex<T> a,std::complex<T> b){
 switch(op){case 0:return a*b;case 1:return a/b;case 2:return std::sqrt(a);case 3:return std::exp(a);case 4:return std::log(a);case 5:return std::sin(a);case 6:return std::cos(a);case 7:return std::tan(a);case 8:return {std::abs(a),0};case 9:return {std::arg(a),0};default:return std::pow(a,b);}
}
static int complex_cases(unsigned limit){
 for(unsigned i=0;i<limit;i++){
  const auto& v=complex_vectors[i];uint64_t re,im;
  if(v.width==64){auto z=evaluate(v.op,std::complex<double>(from64(v.ar),from64(v.ai)),std::complex<double>(from64(v.br),from64(v.bi)));re=bits64(z.real());im=bits64(z.imag());}
  else {auto z=evaluate(v.op,std::complex<float>(from32(v.ar),from32(v.ai)),std::complex<float>(from32(v.br),from32(v.bi)));re=bits32(z.real());im=bits32(z.imag());}
  if(!close_complex(re,im,v,limit==sizeof(complex_vectors)/sizeof(*complex_vectors))){say("complex vector index: real,expected,imag,expected\n");hex(re);hex(v.re);hex(im);hex(v.im);return fail(i);}
 }
 auto root=evaluate(2,std::complex<double>(-4.,-0.),std::complex<double>{});CHECK(std::abs(root.real())<=16*0x1p-52&&root.imag()==-2&&std::signbit(root.imag()));
 auto zero_root=evaluate(2,std::complex<double>(0.,-0.),std::complex<double>{});CHECK(bits64(zero_root.real())==0&&bits64(zero_root.imag())==UINT64_C(0x8000000000000000));
 auto quotient=evaluate(1,std::complex<double>(1.,2.),std::complex<double>{});CHECK(std::isinf(quotient.real())&&std::isinf(quotient.imag()));
 auto zero=evaluate(1,std::complex<float>{},std::complex<float>{});CHECK(std::isnan(zero.real())&&std::isnan(zero.imag()));
 auto infinite=evaluate(0,std::complex<double>(INFINITY,0.),std::complex<double>(1.,1.));CHECK(std::isinf(infinite.real())&&std::isinf(infinite.imag()));
 return 0;
}
static int range_cases(){
 static_assert(std::ranges::random_access_range<std::vector<int>>&&std::ranges::borrowed_range<std::span<int>>);
 std::vector<int> values{1,2,3,4,5,6,7,8};unsigned calls=0;
 auto lazy=values|std::views::filter([](int x){return x%2;})|std::views::transform([&calls](int x){++calls;return x*x;});
 CHECK(!calls);values[0]=9;int sum=0;for(int x:lazy)sum+=x;CHECK(sum==164&&calls==4);
 auto odd_squares=std::views::iota(0,64)|std::views::filter([](int x){return x%2;})|std::views::transform([](int x){return x*x;})|std::views::take(16);
 sum=0;for(int x:odd_squares)sum+=x;CHECK(sum==5456);
 std::array<int,4> reverse{};std::ranges::copy(values|std::views::drop(4)|std::views::reverse,reverse.begin());CHECK((reverse==std::array<int,4>{8,7,6,5}));
 struct Item{int key,payload;};std::array<Item,4> items{{{3,9},{1,1},{4,16},{2,4}}};std::ranges::sort(items,{},&Item::key);CHECK(items[0].key==1&&items[3].payload==16);
 std::string_view text="one,two,three";std::vector<std::string> words;
 for(auto piece:text|std::views::split(','))words.emplace_back(piece.begin(),piece.end());
 CHECK(words.size()==3&&words[1]=="two");std::string flat;for(char c:words|std::views::join)flat+=c;CHECK(flat=="onetwothree");
#if __cplusplus>=202302L
 auto collected=odd_squares|std::ranges::to<std::vector<int>>();CHECK(collected.size()==16&&collected.back()==961);
 std::array<int,3> a{1,2,3},b{10,20,30};sum=0;for(auto [x,y]:std::views::zip(a,b))sum+=x*y;CHECK(sum==140);
 std::array<int,7> ordered{1,1,2,3,3,3,4};unsigned groups=0,total=0;for(auto group:ordered|std::views::chunk_by(std::equal_to{})){++groups;total+=std::ranges::distance(group);}CHECK(groups==4&&total==7);
#endif
 return 0;
}
static std::atomic<unsigned> failures{};
static void worker(){for(unsigned i=0;i<16;i++){if(range_cases()||complex_cases(16))++failures;std::this_thread::yield();}}
extern "C" int main(){
 CHECK(!complex_cases(sizeof(complex_vectors)/sizeof(*complex_vectors))&&!range_cases());
 say("complex worst error in millionths of epsilon,32 then64\n");hex(uint64_t(largest_eps[0]*1000000));hex(uint64_t(largest_eps[1]*1000000));
 for(unsigned cycle=0;cycle<3;cycle++){
  std::thread threads[3];for(auto& t:threads)t=std::thread(worker);for(auto& t:threads)t.join();CHECK(!failures);
  chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&!stats.allocations&&!stats.mapped_bytes);
 }
 say("[ OK ] native C++ ranges,lazy views and complex MP fixtures passed\n");return 0;
}

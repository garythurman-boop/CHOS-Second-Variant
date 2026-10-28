#include <bit>
#include <limits>
#include <type_traits>
#include <thread>
#include <atomic>
#include <string.h>
#include "chos/user.h"
#include "chos/libc.h"
static void say(const char *s){chos_write(s,strlen(s));}
static int fail(unsigned line){say("[FAIL] native bit operations\n");return int(line);}
#define CHECK(x) do{if(!(x))return fail(__LINE__);}while(0)
template<class T>static T reverse_bytes(T x){T out=0;for(unsigned i=0;i<sizeof(T);i++)out=T((out<<8)|T((x>>(i*8))&255));return out;}
template<class T>static int one(T x){
 constexpr unsigned width=std::numeric_limits<T>::digits;unsigned population=0,leading=0,trailing=0;
 for(unsigned i=0;i<width;i++)population+=unsigned((x>>i)&1);
 for(unsigned i=width;i>0&&!((x>>(i-1))&1);i--)leading++;
 for(unsigned i=0;i<width&&!((x>>i)&1);i++)trailing++;
 CHECK(std::popcount(x)==int(population)&&std::countl_zero(x)==int(leading)&&std::countr_zero(x)==int(trailing));
 unsigned significant=width-leading;CHECK(std::bit_width(x)==int(significant)&&std::has_single_bit(x)==(population==1));
 T floor=significant?T(T(1)<<(significant-1)):T(0);CHECK(std::bit_floor(x)==floor);
 if(x<=T(T(1)<<(width-1))){T ceil=x<=1?T(1):T(floor==x?floor:T(floor<<1));CHECK(std::bit_ceil(x)==ceil);}
 for(int amount:{-257,-129,-65,-33,-17,-1,0,1,7,16,31,32,63,64,127,128,257}){
  int r=amount%int(width);if(r<0)r+=int(width);
  T left=r?T(T(x<<r)|T(x>>(width-unsigned(r)))):x;
  T right=r?T(T(x>>r)|T(x<<(width-unsigned(r)))):x;
  CHECK(std::rotl(x,amount)==left&&std::rotr(x,amount)==right);
 }
#if __cplusplus>202002L
 CHECK(std::byteswap(x)==reverse_bytes(x));
 using S=std::make_signed_t<T>;S signed_value=std::bit_cast<S>(x);
 CHECK(std::bit_cast<T>(std::byteswap(signed_value))==reverse_bytes(x));
#endif
 return 0;
}
static int work(uint64_t seed){
 static_assert(std::endian::native==std::endian::little);
 for(unsigned i=0;i<256;i++)CHECK(!one(uint8_t(i)));
 for(unsigned i=0;i<64;i++){uint64_t p=UINT64_C(1)<<i;CHECK(!one(p)&&!one(uint64_t(p-1))&&!one(uint64_t(~p)));}
 for(unsigned i=0;i<1000;i++){
  seed^=seed<<13;seed^=seed>>7;seed^=seed<<17;
  CHECK(!one(uint16_t(seed))&&!one(uint32_t(seed))&&!one(seed));
  unsigned __int128 wide=(static_cast<unsigned __int128>(seed)<<64)|~seed;CHECK(!one(wide));
  double value=std::bit_cast<double>(seed);CHECK(std::bit_cast<uint64_t>(value)==seed);
 }
 return 0;
}
static std::atomic<unsigned> failures;
static void worker(unsigned n){if(work(UINT64_C(0xabcde12345678900)+n))++failures;}
extern "C" int main(){
 CHECK(!work(UINT64_C(0x123456789abcdef0)));
 for(unsigned cycle=0;cycle<3;cycle++){
  std::thread t[3];for(unsigned i=0;i<3;i++)t[i]=std::thread(worker,i);for(auto& thread:t)thread.join();CHECK(!failures);
  chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&!stats.allocations&&!stats.mapped_bytes);
 }
#if __cplusplus>202002L
 say("[ OK ] native C++23 byteswap including signed128-bit values passed\n");
#else
 say("[ OK ] native C++20 bit algorithms passed\n");
#endif
 say("[ OK ] native bit operations and cleanup passed\n");return 0;
}

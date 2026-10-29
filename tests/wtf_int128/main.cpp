#include "config.h"
#include <wtf/Int128.h>
#include <chos/user.h>
#include <chos/libc.h>
#include <atomic>
#include <thread>
#include <bit>
#include <cmath>
#include <cstdio>
using U=WTF::UInt128Impl;using S=WTF::Int128Impl;
using UR=unsigned __int128;using SR=__int128;
template<class T>concept StandardStreamable=requires(std::ostream& stream,T value){stream<<value;};
static_assert(!StandardStreamable<U>&&!StandardStreamable<S>,"Unsupported C++ stream diagnostics must not advertise dead imports");
static bool fail(unsigned line){printf("[FAIL] upstream Int128 arithmetic line %u\n",line);return false;}
#define CHECK(v) do{if(!(v))return fail(__LINE__);}while(0)
static UR bits(U value){return (UR(WTF::UInt128High64(value))<<64)|WTF::UInt128Low64(value);}
static SR bits(S value){UR raw=(UR((uint64_t)WTF::Int128High64(value))<<64)|WTF::Int128Low64(value);return std::bit_cast<SR>(raw);}
extern "C" float __floattisf(SR),__floatuntisf(UR);
extern "C" double __floattidf(SR),__floatuntidf(UR);
static U original(UR value){return WTF::MakeUInt128((uint64_t)(value>>64),(uint64_t)value);}
static S original(SR value){UR raw=std::bit_cast<UR>(value);return WTF::MakeInt128(std::bit_cast<int64_t>((uint64_t)(raw>>64)),(uint64_t)raw);}
static uint64_t next(uint64_t &seed){seed=seed*6364136223846793005ULL+1442695040888963407ULL;return seed;}
static bool cases(uint64_t seed,unsigned count){
 for(unsigned n=0;n<count;n++){
  UR a=(UR(next(seed))<<64)|next(seed),b=(UR(next(seed))<<64)|next(seed);if(n&1)b=(uint64_t)b;if(!b)b=1;
  U x=original(a),y=original(b);
  CHECK(bits(x+y)==a+b&&bits(x-y)==a-b&&bits(x*y)==a*b);
  CHECK(bits(x/y)==a/b&&bits(x%y)==a%b);
  CHECK(bits(x&y)==(a&b)&&bits(x|y)==(a|b)&&bits(x^y)==(a^b)&&bits(~x)==~a);
  CHECK((x==y)==(a==b)&&(x<y)==(a<b)&&(x>y)==(a>b));
  unsigned shift=(unsigned)(next(seed)%128);CHECK(bits(x<<shift)==(a<<shift)&&bits(x>>shift)==(a>>shift));
  SR sa=(SR(next(seed)&0xfffffff)<<64)|next(seed),sb=(SR)(next(seed)>>1);if(!sb)sb=1;
  if(n&1)sa=-sa;if(n&2)sb=-sb;
  S sx=original(sa),sy=original(sb);
  CHECK(bits(sx+sy)==sa+sb&&bits(sx-sy)==sa-sb&&bits(sx/sy)==sa/sb&&bits(sx%sy)==sa%sb);
  CHECK((sx<sy)==(sa<sb)&&(sx>=sy)==(sa>=sb)&&bits(-sx)==-sa);
  SR small=(SR)(int64_t)(next(seed)>>1);if(n&4)small=-small;
  CHECK(bits(original(small)*sy)==small*sb);
  CHECK(bits(sx&sy)==(sa&sb)&&bits(sx|sy)==(sa|sb)&&bits(sx^sy)==(sa^sb));
  /* Reference signed shift via explicit unsigned sign extension, avoiding
   * undefined negative left shifts and signed arithmetic overflow. */
  UR raw=std::bit_cast<UR>(sa),right=raw>>shift;
  if(sa<0&&shift)right|=(~UR(0))<<(128-shift);
  CHECK(std::bit_cast<UR>(bits(sx<<shift))==(raw<<shift)&&std::bit_cast<UR>(bits(sx>>shift))==right);
  if(!(n&127))std::this_thread::yield();
 }
 return true;
}
static bool boundaries(){
 const UR high=UR(1)<<127,max=~UR(0);
 for(UR a:{UR(0),UR(1),UR(UINT64_MAX),UR(1)<<64,high,high+1,max}){
  for(UR b:{UR(1),UR(2),UR(3),UR(UINT64_MAX),high,max}){
   U x=original(a),y=original(b);CHECK(bits(x/y)==a/b&&bits(x%y)==a%b);
  }
  CHECK(bits(original(a)+U(1))==a+1&&bits(original(a)-U(1))==a-1);
 }
 SR minimum=std::bit_cast<SR>(high),maximum=std::bit_cast<SR>(high-1);
 for(SR a:{minimum,minimum+1,SR(-1),SR(0),SR(1),maximum}){
  for(SR b:{SR(-3),SR(-2),SR(-1),SR(1),SR(2),SR(3)}){
   if(a==minimum&&b==-1)continue; // unchanged upstream undefined-operation precondition
   CHECK(bits(original(a)/original(b))==a/b&&bits(original(a)%original(b))==a%b);
  }
 }
 for(unsigned shift:{0u,1u,24u,53u,64u,100u,126u}){
  UR value=UR(1)<<shift;float f=std::ldexp(1.0f,shift);double d=std::ldexp(1.0,shift);
  CHECK(__floatuntisf(value)==f&&__floattisf((SR)value)==f&&__floattisf(-(SR)value)==-f);
  CHECK(__floatuntidf(value)==d&&__floattidf((SR)value)==d&&__floattidf(-(SR)value)==-d);
 }
 CHECK(__floatuntisf((UR(1)<<24)+1)==std::ldexp(1.0f,24)&&__floatuntisf((UR(1)<<24)+3)==std::ldexp(1.0f,24)+4);
 CHECK(__floatuntidf((UR(1)<<53)+1)==std::ldexp(1.0,53)&&__floatuntidf((UR(1)<<53)+3)==std::ldexp(1.0,53)+4);
 CHECK(__floatuntidf(max)==std::ldexp(1.0,128)&&__floattisf(minimum)==-std::ldexp(1.0f,127));
 for(double d:{0.0,0.5,1.0,1.5,3.75,std::ldexp(1.0,64),std::ldexp(1.0,100),std::ldexp(1.0,100)+std::ldexp(1.0,53)}){
  CHECK(bits(U(d))==(UR)d&&bits(S(d))==(SR)d&&bits(S(-d))==(SR)-d);
  CHECK((double)U(d)==(double)(UR)d&&(double)S(-d)==(double)(SR)-d);
 }
 for(float f:{0.0f,0.5f,1.0f,1.5f,3.75f,std::ldexp(1.0f,64),std::ldexp(1.0f,100)})
  CHECK(bits(U(f))==(UR)f&&bits(S(-f))==(SR)-f&&(float)U(f)==(float)(UR)f);
 for(long double d:{0.0L,0.5L,1.0L,1.5L,3.75L,std::ldexp(1.0L,64)+3.0L,std::ldexp(1.0L,100)+std::ldexp(1.0L,30)}){
  CHECK(bits(U(d))==(UR)d&&bits(S(-d))==(SR)-d&&(long double)U(d)==(long double)(UR)d&&(long double)S(-d)==(long double)(SR)-d);
 }
 return true;
}
static std::atomic<unsigned> errors;
static void worker(unsigned id){if(!cases(0x47382915ULL+id,4000)||!boundaries())++errors;}
extern "C" int main(){
 if(!boundaries()||!cases(0xcde7135,1000))return 1;
 for(unsigned cycle=0;cycle<3;cycle++){
  errors=0;std::thread threads[3];for(unsigned i=0;i<3;i++)threads[i]=std::thread(worker,cycle*3+i);
  for(auto &thread:threads)thread.join();if(errors)return 1;
  chos_heap_stats_t heap;if(chos_libc_heap_info(&heap)||heap.allocations||heap.mapped_bytes)return 1;
 }
 puts("[ OK ] original WTF Int128 arithmetic and native floating conversions passed");return 0;
}

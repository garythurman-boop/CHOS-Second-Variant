#include <cmath>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <fenv.h>
#include <atomic>
#include <thread>
#include <chos/libc.h>
#include "vm_math_vectors.h"
static_assert(sizeof(long double)==16 && LDBL_MANT_DIG==113 && LDBL_MAX_EXP==16384);
extern "C" __int128 __muloti4(__int128,__int128,int*);
using U=unsigned __int128;
static std::atomic<unsigned> errors;
static bool fail(unsigned line,unsigned index=0){printf("[FAIL] native VM math line %u index %u\n",line,index);return false;}
#define CHECK(v) do{if(!(v))return fail(__LINE__,i);}while(0)
static U integer(quad_bits_t v){return U(v.hi)<<64|v.lo;}
static long double value(quad_bits_t v){long double r;memcpy(&r,&v,16);return r;}
static U bits(long double v){U r;memcpy(&r,&v,16);return r;}
static __attribute__((noinline)) long double square_root(long double x){return std::sqrt(x);}
static __attribute__((noinline)) long double hypotenuse(long double x,long double y){return std::hypot(x,y);}
static bool run_vectors(){
 unsigned i=0;CHECK(!fesetenv(FE_DFL_ENV) && fegetround()==FE_TONEAREST);
 for(const auto& v:vm_multiply_vectors){
  int overflow=7;auto a=integer(v.a),b=integer(v.b);__int128 sa,sb;memcpy(&sa,&a,16);memcpy(&sb,&b,16);
  auto result=__muloti4(sa,sb,&overflow);CHECK(U(result)==integer(v.result) && overflow==int(v.overflow));++i;
 }
 i=0;for(const auto& v:vm_sqrt_vectors){CHECK(bits(square_root(value(v.x)))==integer(v.result));++i;}
 i=0;for(const auto& v:vm_hypot_vectors){
  auto actual=bits(hypotenuse(value(v.x),value(v.y))),expected=integer(v.result);
  auto distance=actual>expected?actual-expected:expected-actual;
  // The original compensated hypot algorithm is tested within one RNE ULP;
  // exact Pythagorean/scaling cases additionally require exact expected bits.
  CHECK(distance<=1 && (!(v.exact || !expected || expected==(U(0x7fff)<<112)) || !distance));++i;
 }
 i=0;auto inf=value({0,UINT64_C(0x7fff000000000000)}),nan=value({1,UINT64_C(0x7fff800000000000)});
 CHECK(std::isinf(hypotenuse(inf,nan)) && std::isinf(hypotenuse(nan,inf)));
 CHECK(std::isnan(hypotenuse(nan,1)) && std::isnan(square_root(-1.L)));
 CHECK(bits(square_root(-0.L))==(U(1)<<127) && !bits(hypotenuse(-0.L,-0.L)));
 CHECK(fegetround()==FE_TONEAREST);return true;
}
int main(){
 if(!run_vectors())return 1;
 for(unsigned round=0;round<3;round++){
  std::thread workers[3];for(auto& w:workers)w=std::thread([]{if(!run_vectors())++errors;});for(auto& w:workers)w.join();
  if(errors)return 2;chos_heap_stats_t heap{};if(chos_libc_heap_info(&heap)||heap.allocations||heap.mapped_bytes)return 3;
 }
 puts("[ OK ] native signed128 overflow and binary128 sqrt,hypot independent vectors passed");return 0;
}

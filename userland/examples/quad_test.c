#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>
#include <float.h>
#include <pthread.h>
#include <fenv.h>
#include "chos/user.h"
#include "quad_vectors.h"
_Static_assert(LDBL_MANT_DIG==113&&LDBL_MAX_EXP==16384&&sizeof(long double)==16,"native IEEE binary128 ABI");
static void say(const char *s){chos_write(s,strlen(s));}
static int fail(unsigned line){char n[11];n[10]='\n';for(unsigned i=0;i<10;i++){n[9-i]=(char)('0'+line%10);line/=10;}say("[FAIL] binary128 line ");chos_write(n,11);return 1;}
#define CHECK(x) do{if(!(x))return fail(__LINE__);}while(0)
static long double value(quad_bits_t x){union{long double f;__uint128_t u;}v={.u=((__uint128_t)x.hi<<64)|x.lo};return v.f;}
static quad_bits_t bits(long double x){union{long double f;__uint128_t u;}v={.f=x};return (quad_bits_t){(uint64_t)v.u,(uint64_t)(v.u>>64)};}
static void hex(quad_bits_t b){char out[33];out[32]='\n';for(unsigned i=0;i<16;i++){out[15-i]="0123456789abcdef"[(b.hi>>(4*i))&15];out[31-i]="0123456789abcdef"[(b.lo>>(4*i))&15];}chos_write(out,sizeof(out));}
static int same(long double x,quad_bits_t want){quad_bits_t got=bits(x);return got.lo==want.lo&&got.hi==want.hi;}
#define NOINLINE __attribute__((noinline))
static NOINLINE long double operation(unsigned op,long double a,long double b){switch(op){case 0:return a+b;case 1:return a-b;case 2:return a*b;default:return a/b;}}
static NOINLINE double narrow64(long double x){return (double)x;}
static NOINLINE float narrow32(long double x){return (float)x;}
static NOINLINE long double extend64(double x){return x;}
static NOINLINE long double extend32(float x){return x;}
static NOINLINE long double from_i128(__int128_t x){return (long double)x;}
static NOINLINE long double from_u128(__uint128_t x){return (long double)x;}
static NOINLINE long double from_i64(int64_t x){return (long double)x;}
static NOINLINE long double from_u64(uint64_t x){return (long double)x;}
static NOINLINE long double from_i32(int32_t x){return (long double)x;}
static NOINLINE long double from_u32(uint32_t x){return (long double)x;}
static NOINLINE __int128_t to_i128(long double x){return (__int128_t)x;}
static NOINLINE __uint128_t to_u128(long double x){return (__uint128_t)x;}
static NOINLINE int64_t to_i64(long double x){return (int64_t)x;}
static NOINLINE uint64_t to_u64(long double x){return (uint64_t)x;}
static NOINLINE int32_t to_i32(long double x){return (int32_t)x;}
static NOINLINE uint32_t to_u32(long double x){return (uint32_t)x;}
static int vectors(void){
 CHECK(!fesetenv(FE_DFL_ENV));
 for(unsigned i=0;i<sizeof(quad_vectors)/sizeof(*quad_vectors);i++){
  const quad_vector_t *v=&quad_vectors[i];long double x=value(v->a),y=value(v->b);
  quad_bits_t expected[]={v->sum,v->difference,v->product,v->quotient};
  for(unsigned op=0;op<4;op++)if(!same(operation(op,x,y),expected[op])){say("arithmetic vector failure: operation,actual,expected\n");hex((quad_bits_t){op,0});hex(bits(operation(op,x,y)));hex(expected[op]);return fail(i);}
  union{double f;uint64_t u;}d={.f=narrow64(x)};union{float f;uint32_t u;}f={.f=narrow32(x)};
  CHECK(d.u==v->narrow64.lo&&f.u==v->narrow32.lo);
  CHECK((x==y)==(v->a.hi==v->b.hi&&v->a.lo==v->b.lo));CHECK((x<y)==(y>x));CHECK((x<=y)==!(x>y));
 }
 for(unsigned i=0;i<sizeof(quad_integers)/sizeof(*quad_integers);i++){
  const quad_integer_t *v=&quad_integers[i];__uint128_t n=((__uint128_t)v->integer.hi<<64)|v->integer.lo;
  CHECK(same(v->is_signed?from_i128((__int128_t)n):from_u128(n),v->wide));
 }
 for(unsigned i=0;i<sizeof(quad_extends)/sizeof(*quad_extends);i++){
  const quad_extend_t *v=&quad_extends[i];union{double f;uint64_t u;}d={.u=v->narrow};union{float f;uint32_t u;}f={.u=(uint32_t)v->narrow};
  CHECK(same(v->width==64?extend64(d.f):extend32(f.f),v->wide));
 }
 CHECK(from_i32(INT32_MIN)==-2147483648.L&&from_i32(INT32_MAX)==2147483647.L);
 CHECK(from_u32(UINT32_MAX)==4294967295.L&&from_i64(INT64_MIN)==-9223372036854775808.L);
 CHECK(from_u64(UINT64_MAX)==18446744073709551615.L&&from_i64(INT64_MAX)==9223372036854775807.L);
 CHECK(to_i32(-2147483647.75L)==-2147483647&&to_u32(4294967295.75L)==UINT32_MAX);
 CHECK(to_i64(-9223372036854775807.75L)==-INT64_MAX&&to_u64(18446744073709551615.75L)==UINT64_MAX);
 CHECK(to_i128(-0x1p100L-.75L)==-((__int128_t)1<<100));CHECK(to_u128(0x1p100L+.75L)==((__uint128_t)1<<100));
 long double inf=value((quad_bits_t){0,UINT64_C(0x7fff000000000000)}),nan=value((quad_bits_t){1,UINT64_C(0x7fff800000000000)});
 CHECK(inf>0x1p16383L&&-inf<0&&nan!=nan&&!(nan<0)&&!(nan>=0));
 CHECK(same(operation(0,-0.L,-0.L),(quad_bits_t){0,UINT64_C(0x8000000000000000)}));
 CHECK(same(operation(1,1.L,1.L),(quad_bits_t){0,0}));CHECK(operation(3,1.L,inf)==0);
 CHECK(operation(2,inf,2.L)==inf);CHECK(operation(0,nan,1.L)!=operation(0,nan,1.L));
 return 0;
}
static int modes[]={FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
static int flags[]={FE_INVALID,FE_DIVBYZERO,FE_UNDERFLOW};
static void *worker(void *arg){
 unsigned i=(unsigned)(uintptr_t)arg;if(fegetround()!=modes[i]||fetestexcept(FE_ALL_EXCEPT)!=flags[i])return (void *)1;
 for(unsigned cycle=0;cycle<64;cycle++){
  long double up=operation(0,1.L,0x1p-113L),down=operation(0,-1.L,-0x1p-113L);
  if(!same(up,(quad_bits_t){i==1?1u:0u,UINT64_C(0x3fff000000000000)})||!same(down,(quad_bits_t){i==0?1u:0u,UINT64_C(0xbfff000000000000)}))return (void *)2;
  if(fegetround()!=modes[i]||fetestexcept(FE_ALL_EXCEPT)!=(flags[i]|FE_INEXACT))return (void *)3;
  chos_yield();if(!(cycle&7u))chos_sleep_ticks(1);
 }
 return (void *)(uintptr_t)(0x100u+i);
}
int main(void){
 CHECK(!vectors());
 for(unsigned lifecycle=0;lifecycle<3;lifecycle++){
  pthread_t threads[3];
  for(unsigned i=0;i<3;i++){CHECK(!fesetround(modes[i])&&!feclearexcept(FE_ALL_EXCEPT)&&!feraiseexcept(flags[i]));CHECK(!pthread_create(&threads[i],0,worker,(void *)(uintptr_t)i));}
  CHECK(!fesetenv(FE_DFL_ENV));
  for(unsigned i=0;i<3;i++){void *r=0;CHECK(!pthread_join(threads[i],&r)&&r==(void *)(uintptr_t)(0x100u+i));}
  CHECK(fegetround()==FE_TONEAREST&&!fetestexcept(FE_ALL_EXCEPT));
 }
 say("[ OK ] native binary128 rational vectors,conversions and isolated addition rounding passed\n");return 0;
}

#include <math.h>
#include <fenv.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include "chos/user.h"
#include "libm_vectors.h"
static void say(const char *s){chos_write(s,strlen(s));}
static int fail(unsigned line){char n[11];n[10]='\n';for(unsigned i=0;i<10;i++){n[9-i]=(char)('0'+line%10);line/=10;}say("[FAIL] native libm line ");chos_write(n,sizeof(n));return 1;}
#define CHECK(x) do{if(!(x))return fail(__LINE__);}while(0)
static double from64(uint64_t b){union{uint64_t b;double x;}u={.b=b};return u.x;}
static uint64_t bits64(double x){union{uint64_t b;double x;}u={.x=x};return u.b;}
static float from32(uint32_t b){union{uint32_t b;float x;}u={.b=b};return u.x;}
static uint32_t bits32(float x){union{uint32_t b;float x;}u={.x=x};return u.b;}
#define OPERATIONS(SUFFIX,x,y) \
 case 0:return sqrt##SUFFIX(x);case 1:return sin##SUFFIX(x);case 2:return cos##SUFFIX(x);case 3:return tan##SUFFIX(x); \
 case 4:return exp##SUFFIX(x);case 5:return exp2##SUFFIX(x);case 6:return log##SUFFIX(x);case 7:return log2##SUFFIX(x); \
 case 8:return log10##SUFFIX(x);case 9:return log1p##SUFFIX(x);case 10:return expm1##SUFFIX(x);case 11:return atan##SUFFIX(x); \
 case 12:return asin##SUFFIX(x);case 13:return acos##SUFFIX(x);case 14:return atan2##SUFFIX(x,y);case 15:return pow##SUFFIX(x,y); \
 case 16:return hypot##SUFFIX(x,y);case 17:return cbrt##SUFFIX(x);case 18:return erf##SUFFIX(x);case 19:return erfc##SUFFIX(x); \
 case 20:return tgamma##SUFFIX(x);case 21:return lgamma##SUFFIX(x);case 22:return asinh##SUFFIX(x);case 23:return acosh##SUFFIX(x); \
 case 24:return atanh##SUFFIX(x);case 25:return sinh##SUFFIX(x);case 26:return cosh##SUFFIX(x);case 27:return tanh##SUFFIX(x);
static double evaluate64(unsigned op,double x,double y){switch(op){OPERATIONS(,x,y)}return NAN;}
static float evaluate32(unsigned op,float x,float y){switch(op){OPERATIONS(f,x,y)}return NAN;}
static int close_bits(uint64_t a,uint64_t b,unsigned width,unsigned ulp){
 uint64_t sign=UINT64_C(1)<<(width-1),mask=width==32?UINT64_C(0xffffffff):UINT64_MAX;
 uint64_t exponent=width==32?UINT64_C(0x7f800000):UINT64_C(0x7ff0000000000000);
 if(!(b&~sign)||((b&exponent)==exponent))return a==b;
 uint64_t oa=a&sign?(~a&mask):(a^sign),ob=b&sign?(~b&mask):(b^sign);
 return oa>ob?oa-ob<=ulp:ob-oa<=ulp;
}
static int modes[]={FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
static int flags[]={FE_INVALID,FE_DIVBYZERO,FE_UNDERFLOW};
static void *environment_worker(void *p){
 unsigned index=(unsigned)(uintptr_t)p;
 if(fegetround()!=modes[index]||fetestexcept(FE_ALL_EXCEPT)!=flags[index])return (void *)1;
 for(unsigned i=0;i<48;i++){
  double x=nearbyint(1.75);if(x!=(index==1?2.:1.))return (void *)2;
  if(fegetround()!=modes[index]||fetestexcept(FE_INVALID|FE_DIVBYZERO|FE_UNDERFLOW)!=flags[index])return (void *)3;
  chos_yield();if(!(i&7u))chos_sleep_ticks(1);
 }
 return (void *)(uintptr_t)(0x100u+index);
}
int main(void){
 CHECK(!fesetenv(FE_DFL_ENV)&&fegetround()==FE_TONEAREST&&!fetestexcept(FE_ALL_EXCEPT));
 for(unsigned i=0;i<sizeof(math_vectors)/sizeof(*math_vectors);i++){
  const math_vector_t *v=&math_vectors[i];
  uint64_t observed=v->width==64?bits64(evaluate64(v->op,from64(v->a),from64(v->b))):bits32(evaluate32(v->op,from32((uint32_t)v->a),from32((uint32_t)v->b)));
  if(!close_bits(observed,v->expected,v->width,v->ulp)){
   say("math vector index ");char n[11];n[10]='\n';unsigned value=i;
   for(unsigned j=0;j<10;j++){n[9-j]=(char)('0'+value%10);value/=10;}chos_write(n,11);return fail(__LINE__);
  }
 }
 CHECK(bits64(sqrt(-0.0))==UINT64_C(0x8000000000000000));
 CHECK(bits64(sin(-0.0))==UINT64_C(0x8000000000000000));
 CHECK(isnan(sqrt(-1.0))&&isnan(log(-1.0))&&isinf(log(0.0)));
 CHECK(isinf(exp(1000.0))&&exp(-1000.0)==0.0);
 CHECK(fma(1.0+0x1p-27,1.0-0x1p-27,-1.0)==-0x1p-54);
 CHECK(fmaf(1.0f+0x1p-13f,1.0f-0x1p-13f,-1.0f)==-0x1p-26f);
 CHECK(fmax(NAN,2.)==2.&&fmin(2.,NAN)==2.&&isnan(fmax(NAN,NAN)));
 CHECK(nextafter(0.0,1.0)==from64(1)&&nextafterf(0.0f,1.0f)==from32(1));
 CHECK(!feclearexcept(FE_ALL_EXCEPT));(void)sqrt(-1.0);CHECK(fetestexcept(FE_INVALID));
 CHECK(!feclearexcept(FE_ALL_EXCEPT));(void)log(0.0);CHECK(fetestexcept(FE_DIVBYZERO));
 CHECK(!feclearexcept(FE_ALL_EXCEPT));(void)exp(1000.0);CHECK(fetestexcept(FE_OVERFLOW));
 CHECK(fesetround(12345)==-1);
 fenv_t saved;CHECK(!fegetenv(&saved));CHECK(!feholdexcept(&saved)&&!fetestexcept(FE_ALL_EXCEPT));
 CHECK(!feraiseexcept(FE_INVALID)&&!feupdateenv(&saved)&&fetestexcept(FE_INVALID));
 for(unsigned lifecycle=0;lifecycle<3;lifecycle++){
  pthread_t t[3];
  for(unsigned i=0;i<3;i++){CHECK(!fesetround(modes[i])&&!feclearexcept(FE_ALL_EXCEPT)&&!feraiseexcept(flags[i]));CHECK(!pthread_create(&t[i],0,environment_worker,(void *)(uintptr_t)i));}
  CHECK(!fesetenv(FE_DFL_ENV));
  for(unsigned i=0;i<3;i++){void *value=0;CHECK(!pthread_join(t[i],&value)&&value==(void *)(uintptr_t)(0x100u+i));}
  CHECK(fegetround()==FE_TONEAREST&&!fetestexcept(FE_ALL_EXCEPT));
 }
 say("[ OK ] native musl math vectors,special values and inherited isolated FP environments passed\n");return 0;
}

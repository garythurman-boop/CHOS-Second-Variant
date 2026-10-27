#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <float.h>
#include <fenv.h>
#include <pthread.h>
#include "chos/user.h"
#include "floatparse_vectors.h"
static void say(const char *s){chos_write(s,strlen(s));}
static int fail(unsigned line){char n[11];n[10]='\n';for(unsigned i=0;i<10;i++){n[9-i]=(char)('0'+line%10);line/=10;}say("[FAIL] floating parse line ");chos_write(n,11);return 1;}
#define CHECK(x) do{if(!(x))return fail(__LINE__);}while(0)
static uint64_t bits64(double x){union{double f;uint64_t u;}v={.f=x};return v.u;}
static uint32_t bits32(float x){union{float f;uint32_t u;}v={.f=x};return v.u;}
static int same(long double x,quad_bits_t expected){union{long double f;__uint128_t u;}v={.f=x};return v.u==((__uint128_t)expected.hi<<64|expected.lo);}
/* C permits implementation-defined ERANGE reporting on underflow. Keep
 * overflow checks strict; tolerate musl/glibc differences only at tiny results. */
static int range_errno(int got,int reference,unsigned small,unsigned subnormal){return got==reference||(small&&!got&&reference==ERANGE)||(subnormal&&got==ERANGE&&!reference);}
static int vectors(void){
 CHECK(!fesetenv(FE_DFL_ENV));
 for(unsigned i=0;i<sizeof(floatparse_vectors)/sizeof(*floatparse_vectors);i++){
  const floatparse_vector_t *v=&floatparse_vectors[i];char *end;
  errno=0;double d=strtod(v->text,&end);int de=errno;
  if(bits64(d)!=v->d||end-v->text!=v->end){say("double vector index\n");return fail(i);}
  if(!range_errno(de,v->de,fabs(d)<=DBL_MIN,fpclassify(d)==FP_SUBNORMAL)){say("double errno vector index\n");return fail(i);}
  errno=0;float f=strtof(v->text,&end);int fe=errno;
  if(bits32(f)!=v->f||end-v->text!=v->end){say("float vector index\n");return fail(i);}
  if(!range_errno(fe,v->fe,fabsf(f)<=FLT_MIN,fpclassify(f)==FP_SUBNORMAL)){say("float errno vector index\n");return fail(i);}
  errno=0;long double w=strtold(v->text,&end);
  if(!same(w,v->wide)||end-v->text!=v->end){say("binary128 vector index\n");return fail(i);}
  CHECK(errno==0||errno==ERANGE);
 }
 const char *none[]={""," ","+","-",".","nan(","xyz"};
 for(unsigned i=0;i<sizeof(none)/sizeof(*none);i++){
  char *end;double x=strtod(none[i],&end);
  if(i==5){CHECK(isnan(x)&&end==none[i]+3);}else CHECK(x==0&&end==none[i]);
 }
 struct {const char *s;unsigned end;long double want;} cases[]={{"0x",1,0},{"1e+",1,1},{"0x1p-",3,1},{"\t -12.5tail",7,-12.5L}};
 for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);i++){char *end;CHECK(strtold(cases[i].s,&end)==cases[i].want&&end==cases[i].s+cases[i].end);}
 char *end;CHECK(isinf(strtod("-infinity!",&end))&&*end=='!');CHECK(isnan(strtold("nan(payload)!",&end))&&*end=='!');
 errno=41;CHECK(strtod("12.25",&end)==12.25&&!*end&&errno==41);CHECK(atof("-1.25tail")==-1.25&&strtof("1.25",0)==1.25f);
 int exponent;CHECK(frexpl(0x1p-16494L,&exponent)==.5L&&exponent==-16493);
 CHECK(scalbnl(.5L,16384)==0x1p16383L&&fmodl(5.5L,2.L)==1.5L&&fmodl(-5.5L,2.L)==-1.5L);
 CHECK(fabsl(-2.L)==2.L&&signbit(copysignl(1.L,-0.L)));
 CHECK(bits64(nexttoward(1.,1.L+0x1p-100L))==UINT64_C(0x3ff0000000000001));
 CHECK(bits32(nexttowardf(1.f,1.L-0x1p-100L))==UINT32_C(0x3f7fffff));
 CHECK(bits64(nexttoward(0.,-0.L))==UINT64_C(0x8000000000000000));
 return 0;
}
static int modes[]={FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
static void *worker(void *p){
 unsigned i=(unsigned)(uintptr_t)p;
 if(fegetround()!=modes[i]||!fetestexcept(FE_INVALID))return (void *)1;
 errno=17+(int)i;
 for(unsigned loop=0;loop<32;loop++){
  char *end;double a=strtod(FLOATPARSE_DOUBLE_HALFWAY,&end);if(*end||bits64(a)!=(UINT64_C(0x3ff0000000000000)+(i==1)))return (void *)2;
  a=strtod("-" FLOATPARSE_DOUBLE_HALFWAY,&end);if(*end||bits64(a)!=(UINT64_C(0xbff0000000000000)+(i==0)))return (void *)3;
  float f=strtof(FLOATPARSE_FLOAT_HALFWAY,&end);if(*end||bits32(f)!=(UINT32_C(0x3f800000)+(i==1)))return (void *)4;
  long double w=strtold(FLOATPARSE_WIDE_HALFWAY,&end);if(*end||!same(w,(quad_bits_t){i==1,UINT64_C(0x3fff000000000000)}))return (void *)5;
  if(fegetround()!=modes[i]||!fetestexcept(FE_INVALID)||errno!=17+(int)i)return (void *)6;
  chos_yield();if(!(loop&7u))chos_sleep_ticks(1);
 }
 return (void *)(uintptr_t)(0x100u+i);
}
int main(void){
 CHECK(!vectors());
 for(unsigned cycle=0;cycle<3;cycle++){
  pthread_t t[3];for(unsigned i=0;i<3;i++){CHECK(!fesetround(modes[i])&&!feclearexcept(FE_ALL_EXCEPT)&&!feraiseexcept(FE_INVALID));CHECK(!pthread_create(&t[i],0,worker,(void *)(uintptr_t)i));}
  CHECK(!fesetenv(FE_DFL_ENV));for(unsigned i=0;i<3;i++){void *v=0;CHECK(!pthread_join(t[i],&v)&&v==(void *)(uintptr_t)(0x100u+i));}
  CHECK(fegetround()==FE_TONEAREST&&!fetestexcept(FE_ALL_EXCEPT));
 }
 say("[ OK ] native floating parsing,exact ratio endpoints and isolated halfway rounding passed\n");return 0;
}

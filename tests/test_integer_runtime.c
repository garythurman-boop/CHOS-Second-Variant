/* DUT symbols are renamed in their translation units. The oracle expressions
 * in this file lower to the host compiler/libgcc, never the renamed DUT. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <fenv.h>
typedef unsigned __int128 U;typedef __int128 S;
extern U chos_test_udivmodti4(U,U,U *);
extern U chos_test_udivti3(U,U),chos_test_umodti3(U,U);
extern S chos_test_divti3(S,S),chos_test_modti3(S,S);
extern S chos_test_fixsfti(float),chos_test_fixdfti(double);
extern U chos_test_fixunssfti(float),chos_test_fixunsdfti(double);
extern float chos_test_floattisf(S),chos_test_floatuntisf(U);
extern double chos_test_floattidf(S),chos_test_floatuntidf(U);
static uint64_t seed=0x47135;
static uint64_t next(void){seed=seed*6364136223846793005ULL+1442695040888963407ULL;return seed;}
static S signed_bits(U u){S s;memcpy(&s,&u,16);return s;}
static void check_unsigned(U a,U b){
 U rem=~(U)0;assert(b&&chos_test_udivmodti4(a,b,&rem)==a/b&&rem==a%b);
 assert(chos_test_udivmodti4(a,b,NULL)==a/b&&chos_test_udivti3(a,b)==a/b&&chos_test_umodti3(a,b)==a%b);
 assert(chos_test_floatuntidf(a)==(double)a);
}
int main(void){
 assert(!fesetround(FE_TONEAREST));
 U sign=(U)1<<127,max=~(U)0,values[]={0,1,2,3,UINT64_MAX,(U)1<<64,((U)1<<64)+1,sign-1,sign,sign+1,max};
 for(size_t i=0;i<sizeof(values)/sizeof(values[0]);i++)for(size_t j=1;j<sizeof(values)/sizeof(values[0]);j++)check_unsigned(values[i],values[j]);
 S minimum=signed_bits(sign),sv[]={signed_bits(sign),signed_bits(sign)+1,-3,-2,-1,0,1,2,3,signed_bits(sign-1)};
 for(size_t i=0;i<sizeof(sv)/sizeof(sv[0]);i++)for(size_t j=0;j<sizeof(sv)/sizeof(sv[0]);j++)if(sv[j]&&!(sv[i]==minimum&&sv[j]==-1)){
  assert(chos_test_divti3(sv[i],sv[j])==sv[i]/sv[j]&&chos_test_modti3(sv[i],sv[j])==sv[i]%sv[j]);
 }
 for(unsigned i=0;i<200000;i++){
  U a=((U)next()<<64)|next(),b=((U)next()<<64)|next();if(i&1)b=(uint64_t)b;if(!b)b=1;check_unsigned(a,b);
  S sa=signed_bits(a),sb=signed_bits(b);if(sa==minimum&&sb==-1)sb=1;
  assert(chos_test_divti3(sa,sb)==sa/sb&&chos_test_modti3(sa,sb)==sa%sb&&chos_test_floattidf(sa)==(double)sa&&chos_test_floattisf(sa)==(float)sa);
  assert(chos_test_floatuntisf(a>>1)==(float)(a>>1));
  U bounded=a&(((U)1<<112)-1);S bounded_signed=(S)bounded;if(i&1)bounded_signed=-bounded_signed;
  float f=(float)bounded;double d=(double)bounded;
  assert(chos_test_floatuntisf(bounded)==f&&chos_test_floattisf(bounded_signed)==(float)bounded_signed);
  assert(chos_test_fixunssfti(f)==(U)f&&chos_test_fixunsdfti(d)==(U)d);
  if(i&2){f=-f;d=-d;}
  assert(chos_test_fixsfti(f)==(S)f&&chos_test_fixdfti(d)==(S)d);
  float fraction=(float)(int32_t)next()/65537.0f;double fraction64=(double)(int64_t)next()/65537.0;
  assert(chos_test_fixsfti(fraction)==(S)fraction&&chos_test_fixdfti(fraction64)==(S)fraction64);
  assert(chos_test_fixunssfti(fabsf(fraction))==(U)fabsf(fraction)&&chos_test_fixunsdfti(fabs(fraction64))==(U)fabs(fraction64));
 }
 for(int exponent=-150;exponent<=126;exponent++){
  float f=ldexpf(1.0f,exponent);double d=ldexp(1.0,exponent);
  assert(chos_test_fixunssfti(f)==(U)f&&chos_test_fixunsdfti(d)==(U)d);
  assert(chos_test_fixsfti(f)==(S)f&&chos_test_fixdfti(d)==(S)d);
  float fraction=(float)(int32_t)next()/65537.0f;double fraction64=(double)(int64_t)next()/65537.0;
  assert(chos_test_fixsfti(fraction)==(S)fraction&&chos_test_fixdfti(fraction64)==(S)fraction64);
  assert(chos_test_fixunssfti(fabsf(fraction))==(U)fabsf(fraction)&&chos_test_fixunsdfti(fabs(fraction64))==(U)fabs(fraction64));
  assert(chos_test_fixsfti(-f)==(S)-f&&chos_test_fixdfti(-d)==(S)-d);
 }
 puts("original LLVM integer runtime: 200000 independent host-oracle division/modulo/conversion cases and boundaries passed");
}

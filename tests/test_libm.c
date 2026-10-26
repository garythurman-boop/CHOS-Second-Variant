#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <math.h>
#include <fenv.h>
#include "libm_vectors.h"
#define UNARY(name) extern double chos_m_##name(double);extern float chos_m_##name##f(float);
#define BINARY(name) extern double chos_m_##name(double,double);extern float chos_m_##name##f(float,float);
UNARY(sqrt) UNARY(sin) UNARY(cos) UNARY(tan) UNARY(exp) UNARY(exp2) UNARY(log) UNARY(log2) UNARY(log10)
UNARY(log1p) UNARY(expm1) UNARY(atan) UNARY(asin) UNARY(acos) BINARY(atan2) BINARY(pow) BINARY(hypot)
UNARY(cbrt) UNARY(erf) UNARY(erfc) UNARY(tgamma) UNARY(lgamma) UNARY(asinh) UNARY(acosh) UNARY(atanh)
UNARY(sinh) UNARY(cosh) UNARY(tanh)
extern double chos_m_fma(double,double,double);
extern float chos_m_fmaf(float,float,float);
static double from64(uint64_t b){union{uint64_t b;double x;}u={.b=b};return u.x;}
static uint64_t bits64(double x){union{uint64_t b;double x;}u={.x=x};return u.b;}
static float from32(uint32_t b){union{uint32_t b;float x;}u={.b=b};return u.x;}
static uint32_t bits32(float x){union{uint32_t b;float x;}u={.x=x};return u.b;}
#define CASES(SUFFIX,x,y) \
 case 0:return chos_m_sqrt##SUFFIX(x);case 1:return chos_m_sin##SUFFIX(x);case 2:return chos_m_cos##SUFFIX(x);case 3:return chos_m_tan##SUFFIX(x); \
 case 4:return chos_m_exp##SUFFIX(x);case 5:return chos_m_exp2##SUFFIX(x);case 6:return chos_m_log##SUFFIX(x);case 7:return chos_m_log2##SUFFIX(x); \
 case 8:return chos_m_log10##SUFFIX(x);case 9:return chos_m_log1p##SUFFIX(x);case 10:return chos_m_expm1##SUFFIX(x);case 11:return chos_m_atan##SUFFIX(x); \
 case 12:return chos_m_asin##SUFFIX(x);case 13:return chos_m_acos##SUFFIX(x);case 14:return chos_m_atan2##SUFFIX(x,y);case 15:return chos_m_pow##SUFFIX(x,y); \
 case 16:return chos_m_hypot##SUFFIX(x,y);case 17:return chos_m_cbrt##SUFFIX(x);case 18:return chos_m_erf##SUFFIX(x);case 19:return chos_m_erfc##SUFFIX(x); \
 case 20:return chos_m_tgamma##SUFFIX(x);case 21:return chos_m_lgamma##SUFFIX(x);case 22:return chos_m_asinh##SUFFIX(x);case 23:return chos_m_acosh##SUFFIX(x); \
 case 24:return chos_m_atanh##SUFFIX(x);case 25:return chos_m_sinh##SUFFIX(x);case 26:return chos_m_cosh##SUFFIX(x);case 27:return chos_m_tanh##SUFFIX(x);
static double evaluate64(unsigned op,double x,double y){switch(op){CASES(,x,y)}abort();}
static float evaluate32(unsigned op,float x,float y){switch(op){CASES(f,x,y)}abort();}
static int close_bits(uint64_t a,uint64_t b,unsigned width,unsigned ulp){
    uint64_t sign=UINT64_C(1)<<(width-1),mask=width==32?UINT64_C(0xffffffff):UINT64_MAX;
    uint64_t exponent=width==32?UINT64_C(0x7f800000):UINT64_C(0x7ff0000000000000);
    if(!(b&~sign)||(b&exponent)==exponent)return a==b;
    uint64_t x=a&sign?(~a&mask):a^sign,y=b&sign?(~b&mask):b^sign;
    return x>y?x-y<=ulp:y-x<=ulp;
}
int main(void){
    assert(!fesetenv(FE_DFL_ENV));
    for(unsigned i=0;i<sizeof(math_vectors)/sizeof(*math_vectors);i++){
        const math_vector_t *v=&math_vectors[i];
        uint64_t observed=v->width==64?bits64(evaluate64(v->op,from64(v->a),from64(v->b))):bits32(evaluate32(v->op,from32((uint32_t)v->a),from32((uint32_t)v->b)));
        if(!close_bits(observed,v->expected,v->width,v->ulp)){
            fprintf(stderr,"math vector%u op%u width%u: observed=%llx expected=%llx\n",i,v->op,v->width,(unsigned long long)observed,(unsigned long long)v->expected);return 1;
        }
    }
    assert(bits64(chos_m_sqrt(-0.0))==UINT64_C(0x8000000000000000));
    assert(isnan(chos_m_sqrt(-1.0))&&isinf(chos_m_exp(1000.0)));
    assert(chos_m_fma(1.+0x1p-27,1.-0x1p-27,-1.)==-0x1p-54);
    assert(chos_m_fmaf(1.f+0x1p-13f,1.f-0x1p-13f,-1.f)==-0x1p-26f);
    puts("musl generic host math:889 high-precision scalar fixtures,special values and FMA passed");return 0;
}

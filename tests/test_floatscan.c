#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <locale.h>
#include <fenv.h>
static _Thread_local int error;
int *__chos_errno_location(void){return &error;}
extern int *__errno_location(void);
extern float reference_strtof(const char *,char **) __asm__("strtof");
extern double reference_strtod(const char *,char **) __asm__("strtod");
extern long double reference_strtold(const char *,char **) __asm__("strtold");
static uint32_t state=0x5343414e;
static uint32_t random_value(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
static int check(const char *source){
 size_t length=strlen(source);char *s=malloc(length+1);assert(s);memcpy(s,source,length+1);
 char *a,*b;
/* Underflow ERANGE is implementation-defined in C; overflow/value/endpoints
 * remain strict. musl may also use EINVAL for no conversion. */
#define DIFFERENTIAL(type,native,reference) do { \
 errno=0;*__errno_location()=0;type x=native(s,&a);int err=errno;type y=reference(s,&b);int referr=*__errno_location(); \
 if(!((isnan(x)&&isnan(y))||((x==y)&&(!x?!!signbit(x)==!!signbit(y):1)))||a-s!=b-s||(err!=referr && !(a==s&&b==s&&err==EINVAL&&!referr) && !((fpclassify(x)==FP_SUBNORMAL&&(err==0||err==ERANGE)&&(referr==0||referr==ERANGE)) || (err==0&&referr==ERANGE&&fabsl(x)<=(sizeof(type)==sizeof(float)?FLT_MIN:sizeof(type)==sizeof(double)?DBL_MIN:LDBL_MIN))))){ \
 fprintf(stderr,"float parsing mismatch: %s input=%s offsets=%td/%td errno=%d/%d\n",#native,s,a-s,b-s,err,referr);free(s);return 1;} \
}while(0)
 DIFFERENTIAL(float,strtof,reference_strtof);DIFFERENTIAL(double,strtod,reference_strtod);DIFFERENTIAL(long double,strtold,reference_strtold);
 free(s);return 0;
}
int main(void){
 assert(setlocale(LC_ALL,"C"));assert(!fesetround(FE_TONEAREST));
 const char *cases[]={""," ","+","-",".",".e2","-0","+0.000","12.25tail","  -12.5e+2!","0x","0xp1","0x1p","0x1p+","0x1.8p2tail","inf","INFINITYx","infinite","nan","-NAN(payload)","nan(","nan(foo!","nan(abcd)","infin","1e","1e+","1e-0x","1.00000000000000011102230246251565404236316680908203125","1.000000059604644775390625","1e-6000","-1e6000","2.2250738585072014e-308","4.9406564584124654e-324","1.7976931348623157e308","3.4028234663852886e38","1.1754943508222875e-38","1.401298464324817e-45"};
 for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);i++)assert(!check(cases[i]));
 for(unsigned i=0;i<20000;i++){
  char text[200];
  if(i&1){
   unsigned n=random_value()%140;for(unsigned j=0;j<n;j++)text[j]="0123456789abcdefX.+-eEpP()_nanINF \t!"[random_value()%34];text[n]=0;
  }else snprintf(text,sizeof(text),"%c%u.%09ue%+d%s",random_value()&1?'-':'+',random_value(),random_value()%1000000000,(int)(random_value()%12001)-6000,random_value()&1?"tail":"");
  assert(!check(text));
 }
 errno=41;char *end;assert(strtod("12.25",&end)==12.25&&!*end&&errno==41);assert(atof("-1.25x")==-1.25);
 puts("floating parser: 20,000 glibc differential decimal/hex/malformed inputs,all three widths,endpoint/range and exact-sized allocations passed");return 0;
}

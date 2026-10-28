#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <float.h>
#include <limits.h>
#include <fenv.h>
#include <locale.h>
extern int puts(const char *);
extern int printf(const char *,...);
/* Compare a separately named implementation; never interpose host/sanitizer IO. */
int chos_test_vsnprintf(char *,size_t,const char *,va_list);
int chos_test_snprintf(char *,size_t,const char *,...);
int vsnprintf(char *,size_t,const char *,va_list);
static _Thread_local int error_number;
int *__chos_errno_location(void){return &error_number;}
static void compare(const char *format,...){
 unsigned char actual[4098],expected[4098];size_t sizes[]={0,1,2,3,7,15,31,64,129,4096};
 for(unsigned i=0;i<sizeof(sizes)/sizeof(*sizes);i++){
  size_t size=sizes[i];memset(actual,0x55,sizeof(actual));memset(expected,0x55,sizeof(expected));
  va_list a,b;va_start(a,format);va_copy(b,a);
  int got=chos_test_vsnprintf((char *)actual+1,size,format,a),want=vsnprintf((char *)expected+1,size,format,b);
  va_end(a);va_end(b);
  if(got!=want||memcmp(actual,expected,sizeof(actual))){
   char rendered[200],reference[200];va_list c,d;va_start(c,format);va_copy(d,c);
   chos_test_vsnprintf(rendered,sizeof(rendered),format,c);vsnprintf(reference,sizeof(reference),format,d);va_end(c);va_end(d);
   printf("format mismatch: %s size=%zu got=%d want=%d actual=%s expected=%s\n",format,size,got,want,rendered,reference);abort();
  }
 }
}
static uint64_t random_state=UINT64_C(0x51f0a84e42);
static uint64_t next(void){random_state^=random_state<<13;random_state^=random_state>>7;random_state^=random_state<<17;return random_state;}
static void integers(void){
 for(unsigned i=0;i<4000;i++){
  long long v=(long long)next();unsigned long long u=next();
  compare("%lld %llu %#llx %#llo %020lld",v,u,u,u,v);
  compare("%*.*lld",(int)(next()%70)-35,(int)(next()%25)-3,v);
  compare("%hhd/%hhu/%hd/%hu/%d/%u",(int)v,(unsigned)v,(int)v,(unsigned)v,(int)v,(unsigned)v);
 }
 compare("%zd %zu %td %ju %jd",(ptrdiff_t)-123,(size_t)456,(ptrdiff_t)-789,(uintmax_t)UINT64_MAX,(intmax_t)INT64_MIN);
 compare("%+d|% d|%#x|%#.0o|%.0u",0,12,0,0u,0u);
 compare("%2$*1$d %3$s",12,-37,"position");compare("%3$.*2$f/%1$d",2,7,1.25);
}
static void strings(void){
 const char *text="string formatting";
 for(int width=-25;width<26;width+=5)for(int precision=0;precision<22;precision+=3)compare("%*.*s/%c/%%",width,precision,text,'X');
 compare("%ls/%lc",L"wide \u20ac \U0001f680",(unsigned)0x3bb);
 char out[12];assert(chos_test_snprintf(NULL,0,"%s:%d","value",123)==9);
 assert(chos_test_snprintf(out,1,"abcdef")==6&&!out[0]);
 int n=-1;assert(chos_test_snprintf(out,4,"abc%nXYZ",&n)==6&&n==3&&!strcmp(out,"abc"));
 error_number=EINVAL;assert(chos_test_snprintf(out,sizeof(out),"%m")==16&&!strcmp(out,"Invalid arg"));
 error_number=0;assert(chos_test_snprintf(out,sizeof(out),"%2147483648d",3)==-1&&error_number==EOVERFLOW);
 error_number=0;assert(chos_test_snprintf(out,sizeof(out),"%1$d %3$d",1,2,3)==-1&&error_number==EINVAL);
 error_number=0;assert(chos_test_snprintf(out,sizeof(out),"%*d",INT_MIN,1)==-1&&error_number==EOVERFLOW);
 const char *invalid_positions[]={"%0$d","%1$*0$d","%1$.*0$d"};
 for(unsigned i=0;i<3;i++){error_number=0;assert(chos_test_snprintf(out,sizeof(out),invalid_positions[i],3)==-1&&error_number==EINVAL);}
 wchar_t invalid[]={0xd800,0};assert(chos_test_snprintf(out,sizeof(out),"%ls",invalid)==-1);
}
static void floating(void){
 double special[]={0.,-0.,1.,-1.,1.25,-3.5,1e-300,1e300,DBL_MIN,DBL_MAX,0x1p-1074,INFINITY,-INFINITY,NAN};
 for(unsigned i=0;i<sizeof(special)/sizeof(*special);i++){
  double x=special[i];compare("%+.17g/%#.9e/%+024.8f",x,x,x);if(!x || fabs(x)>=DBL_MIN)compare("%.13a/%A",x,x);
  // C permits different hexadecimal normalization for subnormals. Test the
  // represented value rather than requiring glibc's exponent spelling.
  char hex[100];int count=chos_test_snprintf(hex,sizeof(hex),"%.13a",x);assert(count==(int)strlen(hex));
  char *end;double roundtrip=strtod(hex,&end);assert(!*end);
  assert(isnan(x)?isnan(roundtrip):roundtrip==x&&signbit(roundtrip)==signbit(x));
 }
 for(unsigned i=0;i<1500;i++){
  uint64_t bits=next();double x;memcpy(&x,&bits,8);if(!isfinite(x))continue;
  compare("%.*g/%.*e/%.*f",(int)(next()%24),x,(int)(next()%24),x,(int)(next()%12),x);
 }
 int modes[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
 for(unsigned i=0;i<4;i++){
  assert(!fesetround(modes[i]));compare("%.0f/%.0f/%.1f/%.1f",2.5,-2.5,1.25,-1.25);
  compare("%.20Lg/%.30Lf/%.20Le",1.L/3,0x1.123456789abcp-30L,0x1.ffffffffffffp500L);
 }
 assert(!fesetround(FE_TONEAREST));
}
int main(void){assert(setlocale(LC_ALL,"C.UTF-8"));integers();strings();floating();puts("format: guard bytes,ten capacities,4000 integer and1500 floating inputs,UTF8,errors and four rounding modes passed");return 0;}

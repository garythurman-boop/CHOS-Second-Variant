#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <errno.h>
#include <wchar.h>
#include <locale.h>
static _Thread_local int native_error;
int *__chos_errno_location(void){return &native_error;}
int chos_memory_sscanf(const char *,const char *,...);
int chos_memory_vsscanf(const char *,const char *,va_list);
static int scan(const char *s,const char *f,...){va_list a;va_start(a,f);int r=chos_memory_vsscanf(s,f,a);va_end(a);return r;}
static uint64_t rng=UINT64_C(0x5343414e4f524143);
static uint64_t random64(void){rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return rng;}
int main(void){
 for(unsigned i=0;i<10000;i++){
  int n=(int)(random64()%2000001)-1000000,a=0,b=0,an=-1,bn=-1;unsigned u=(unsigned)random64(),au=0,bu=0;
  char s[128];snprintf(s,sizeof s,"  %d : %x tail",n,u);
  int ar=scan(s," %d : %x %n",&a,&au,&an),br=sscanf(s," %d : %x %n",&b,&bu,&bn);
  assert(ar==br&&a==n&&b==n&&au==u&&bu==u&&an==bn);
  double d=(double)((int)(random64()%200001)-100000)/64.0,ad=0,bd=0;snprintf(s,sizeof s,"%a",d);
  ar=chos_memory_sscanf(s,"%la%n",&ad,&an);br=sscanf(s,"%la%n",&bd,&bn);
  assert(ar==br&&ad==d&&bd==d&&an==bn);
 }
 int a=99,n=-1;assert(scan("","%d",&a)==EOF&&a==99);assert(scan("x","%d",&a)==0&&a==99);
 assert(scan("","%n",&n)==0&&n==0);assert(scan("+","%d",&a)==0&&a==99);
 char word[8]={0},chars[4]={0};assert(scan("abc123 ZQ","%3[a-z]%*3d %2c%n",word,chars,&n)==2&&!strcmp(word,"abc")&&!memcmp(chars,"ZQ",2)&&n==9);
 a=n=0;assert(scan("12 34","%2$d %1$d",&a,&n)==2&&a==34&&n==12);
 char *allocated=NULL;assert(scan("hello world","%ms",&allocated)==1&&!strcmp(allocated,"hello"));free(allocated);
 long long ll=0;unsigned long long ull=0;assert(scan("-9223372036854775808 18446744073709551615","%lld %llu",&ll,&ull)==2&&ll==INT64_MIN&&ull==UINT64_MAX);
 float f=0;double d=0;long double ld=0;assert(scan("0x1p-149 0x1p-1074 1.25","%a %la %Lf",&f,&d,&ld)==3&&f==0x1p-149f&&d==0x1p-1074&&ld==1.25L);
 native_error=0;assert(scan(NULL,"%d",&a)==EOF&&native_error==EINVAL);native_error=0;assert(scan("1",NULL,&a)==EOF&&native_error==EINVAL);
 a=99;native_error=0;assert(scan("77","%2147483648d",&a)==EOF&&a==99&&native_error==EOVERFLOW);
 native_error=0;assert(scan("77","%999999999999999999999999999d",&a)==EOF&&a==99&&native_error==EOVERFLOW);
 native_error=0;assert(scan("77","%0$d",&a)==EOF&&a==99&&native_error==EINVAL);
 // Exact-size input objects/red zones catch read-ahead beyond NUL. A %c at
 // empty input must see EOF, not consume the terminating NUL as a character.
 for(unsigned size=0;size<257;size++){
  char *s=malloc(size+1);assert(s);memset(s,'7',size);s[size]=0;
  char *out=malloc(size+2);assert(out);memset(out,0x5a,size+2);
  int r=scan(s,"%257[7]",out);assert(r==(size?1:EOF));if(size)assert(strlen(out)==size);else assert((unsigned char)out[0]==0x5a);
  assert((unsigned char)out[size+1]==0x5a);free(out);free(s);
 }
 char zero[1]={0},c=42;assert(scan(zero,"%c",&c)==EOF&&c==42);
 assert(setlocale(LC_CTYPE,"C.UTF-8"));
 wchar_t wa=0,wb=0,xa=0,xb=0;int na=-1,nb=-1;
 assert(scan("\xc3\xa9X","%lc%n%lc",&wa,&na,&xa)==sscanf("\xc3\xa9X","%lc%n%lc",&wb,&nb,&xb));
 assert(wa==0xe9&&wb==wa&&xa=='X'&&xb==xa&&na==2&&nb==na);
 wchar_t av[8]={0},bv[8]={0};
 assert(scan("\xc3\xa9\xf0\x9f\x99\x82Z","%2lc%n%lc",av,&na,&xa)==2);
 assert(sscanf("\xc3\xa9\xf0\x9f\x99\x82Z","%2lc%n%lc",bv,&nb,&xb)==2);
 assert(av[0]==0xe9&&av[1]==0x1f642&&!memcmp(av,bv,2*sizeof(wchar_t))&&na==6&&nb==na&&xa=='Z'&&xb==xa);
 memset(av,0,sizeof av);assert(scan("\xc3\xa9\xf0\x9f\x99\x82Z","%1ls%n%lc",av,&na,&xa)==2&&av[0]==0xe9&&!av[1]&&na==2&&xa==0x1f642);
 assert(scan("\xc3\xa9X","%*1lc%lc",&xa)==1&&xa=='X');
 wa=77;native_error=0;assert(scan("\xc3","%lc",&wa)==EOF&&wa==77&&native_error==EILSEQ);
 memset(av,0,sizeof av);native_error=0;assert(scan("\xc3\xa9","%l[\xc3\xa9]",av)==EOF&&native_error==ENOTSUP&&!av[0]);
 puts("memory scanf:10000 independent host integer/hex-float oracles,counts/widths/scansets/varargs/allocations/extrema and257 exact-size bounds passed");return 0;
}

#include <stdlib.h>
#include <uchar.h>
#include <errno.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <locale.h>
static _Thread_local int error;
int *__chos_errno_location(void){return &error;}
extern int *__errno_location(void);
extern long reference_strtol(const char *,char **,int) __asm__("strtol");
extern long long reference_strtoll(const char *,char **,int) __asm__("strtoll");
extern unsigned long reference_strtoul(const char *,char **,int) __asm__("strtoul");
extern unsigned long long reference_strtoull(const char *,char **,int) __asm__("strtoull");
extern size_t reference_c32rtomb(char *,uint32_t,void *) __asm__("c32rtomb");
extern size_t reference_mbrtoc32(uint32_t *,const char *,size_t,void *) __asm__("mbrtoc32");
static uint32_t random_state=0x238176;
static uint32_t random_value(void){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static int compare(const void *a,const void *b){int x=*(const int *)a,y=*(const int *)b;return (x>y)-(x<y);}
static void numeric(void){
 const char *cases[]={"","-","+"," \t\n-0x10rest","0x","0xz","0777 ","-9223372036854775808","9223372036854775807","9223372036854775808","-9223372036854775809","18446744073709551615","18446744073709551616","-18446744073709551615","-18446744073709551616","123z","xyz","99999999999999999999999999999999999999999999999999999999999!"};
 int bases[]={0,2,3,8,10,16,36};
 for(unsigned iteration=0;iteration<20000;iteration++){
  char generated[128];const char *s;
  if(iteration<sizeof(cases)/sizeof(*cases))s=cases[iteration];
  else {unsigned n=random_value()%100;for(unsigned i=0;i<n;i++)generated[i]="0123456789abcdefXYZ+- \t!"[random_value()%23];generated[n]=0;s=generated;}
  int base=bases[iteration%7];char *a,*b;
#define DIFFERENTIAL(native,reference) do {errno=0;*__errno_location()=0; \
  __typeof__(native(s,&a,base)) x=native(s,&a,base),y=reference(s,&b,base); \
  assert(x==y&&a-s==b-s&&errno==*__errno_location());}while(0)
  DIFFERENTIAL(strtol,reference_strtol);DIFFERENTIAL(strtoll,reference_strtoll);
  DIFFERENTIAL(strtoul,reference_strtoul);DIFFERENTIAL(strtoull,reference_strtoull);
 }
 char *end;errno=0;assert(!strtol("12",&end,1)&&errno==EINVAL&&*end=='1');
 errno=23;assert(strtol("123",&end,10)==123&&!*end&&errno==23);
 assert(atoi(" -123 ")==-123&&atol("2147483648")==2147483648L&&atoll("-900000000000")==-900000000000LL);
 for(int n=-99;n<=99;n++)for(int d=-11;d<=11;d++)if(d){
  div_t x=div(n,d);ldiv_t y=ldiv(n,d);lldiv_t z=lldiv(n,d);
  assert(x.quot==n/d&&x.rem==n%d&&y.quot==x.quot&&z.rem==x.rem);
 }
 assert(abs(-23)==23&&labs(-123L)==123&&llabs(-999LL)==999);
 int values[513];
 for(unsigned round=0;round<100;round++){
  unsigned n=random_value()%514;
  for(unsigned i=0;i<n;i++)values[i]=(int)(random_value()%1000)-500;
  qsort(values,n,sizeof(int),compare);
  for(unsigned i=1;i<n;i++)assert(values[i-1]<=values[i]);
  for(unsigned i=0;i<n;i++){int *found=bsearch(&values[i],values,n,sizeof(int),compare);assert(found&&*found==values[i]);}
  int absent=5000;assert(!bsearch(&absent,values,n,sizeof(int),compare));
 }
 errno=29;qsort(0,0,0,0);assert(!bsearch(0,0,0,0,0)&&errno==29);
 qsort(values,SIZE_MAX,sizeof(int),compare);assert(errno==EINVAL);
 assert(!bsearch(values,values,SIZE_MAX,sizeof(int),compare)&&errno==EINVAL);
}
static void unicode(void){
 assert(setlocale(LC_CTYPE,"C.UTF-8"));
 for(uint32_t c=0;c<=0x10ffff;c++){
  if(c>=0xd800&&c<=0xdfff)continue;
  char a[8]={0},b[8]={0};mbstate_t state={0};_Alignas(16) unsigned char reference_state[64]={0};
  size_t n=c32rtomb(a,c,&state),m=reference_c32rtomb(b,c,reference_state);
  assert(n==m&&n>=1&&n<=4&&!memcmp(a,b,n)&&mbsinit(&state));
  char32_t decoded=UINT32_MAX,reference=UINT32_MAX;
  assert(mbrtoc32(&decoded,a,n,&state)==reference_mbrtoc32(&reference,b,n,reference_state)&&decoded==c&&reference==c&&mbsinit(&state));
  state=(mbstate_t){0};
  for(size_t i=0;i<n;i++){
   size_t r=mbrtoc32(&decoded,a+i,1,&state);
   assert(r==(i+1<n?(size_t)-2:c?1:0));
  }
  assert(decoded==c&&mbsinit(&state));
  char16_t unit=0;size_t r=mbrtoc16(&unit,a,n,&state);assert(r==(c?n:0));
  mbstate_t encoder={0};char buffer[4];
  if(c>0xffff){
   assert(unit==0xd800u+((c-0x10000u)>>10)&&!mbsinit(&state));
   assert(!c16rtomb(buffer,unit,&encoder));
   assert(mbrtoc16(&unit,"",0,&state)==(size_t)-3&&unit==0xdc00u+((c-0x10000u)&1023u));
  }
  assert(c16rtomb(buffer,unit,&encoder)==n&&!memcmp(buffer,a,n)&&mbsinit(&encoder)&&mbsinit(&state));
 }
 const char *bad[]={"\x80","\xc0\x80","\xc1\xbf","\xe0\x80\x80","\xed\xa0\x80","\xed\xbf\xbf","\xf0\x80\x80\x80","\xf4\x90\x80\x80","\xf5\x80\x80\x80","\xff","\xc2x"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){
  mbstate_t state={0};char32_t c=1234;errno=0;
  assert(mbrtoc32(&c,bad[i],strlen(bad[i]),&state)==(size_t)-1&&errno==EILSEQ&&c==1234&&mbsinit(&state));
 }
 mbstate_t s={0};char32_t c=77;char output[4]={1,2,3,4};
 assert(mbrtoc32(&c,"\xe2",1,&s)==(size_t)-2&&c==77);
 assert(mbrtoc32(&c,0,100,&s)==(size_t)-1&&mbsinit(&s));
 assert(!mbrtoc32(&c,0,100,&s)&&c==77);
 assert(mbrtoc32(&c,"x",0,&s)==(size_t)-2&&mbsinit(&s));
 assert(c32rtomb(output,0x110000,&s)==(size_t)-1&&output[0]==1);
 assert(c32rtomb(output,0xd800,&s)==(size_t)-1&&output[0]==1);
 assert(c32rtomb(0,0x110000,&s)==1);
 char16_t u=77;assert(!mbrtoc16(&u,0,10,&s)&&u==77);
 assert(!c16rtomb(output,0xd800,&s));assert(c16rtomb(output,'x',&s)==(size_t)-1&&mbsinit(&s));
 assert(c16rtomb(output,0xdc00,&s)==(size_t)-1);
 assert(!c16rtomb(output,0xd800,&s));assert(c16rtomb(0,'x',&s)==(size_t)-1&&mbsinit(&s));
 assert(mbsinit(0));
}
int main(void){numeric();unicode();puts("native base C: numeric differential cases,sorting/search,all Unicode scalars,chunking,UTF-16 and malformed input passed");return 0;}

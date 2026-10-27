#include <stdlib.h>
#include <uchar.h>
#include <errno.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <locale.h>
#include <ctype.h>
#include <inttypes.h>
static _Thread_local int error;
int *__chos_errno_location(void){return &error;}
extern int *__errno_location(void);
extern long reference_strtol(const char *,char **,int) __asm__("strtol");
extern long long reference_strtoll(const char *,char **,int) __asm__("strtoll");
extern unsigned long reference_strtoul(const char *,char **,int) __asm__("strtoul");
extern unsigned long long reference_strtoull(const char *,char **,int) __asm__("strtoull");
extern size_t reference_c32rtomb(char *,uint32_t,void *) __asm__("c32rtomb");
extern size_t reference_mbrtoc32(uint32_t *,const char *,size_t,void *) __asm__("mbrtoc32");
extern int reference_isalnum(int) __asm__("isalnum");
extern int reference_isalpha(int) __asm__("isalpha");
extern int reference_isblank(int) __asm__("isblank");
extern int reference_iscntrl(int) __asm__("iscntrl");
extern int reference_isdigit(int) __asm__("isdigit");
extern int reference_isgraph(int) __asm__("isgraph");
extern int reference_islower(int) __asm__("islower");
extern int reference_isprint(int) __asm__("isprint");
extern int reference_ispunct(int) __asm__("ispunct");
extern int reference_isspace(int) __asm__("isspace");
extern int reference_isupper(int) __asm__("isupper");
extern int reference_isxdigit(int) __asm__("isxdigit");
extern int reference_tolower(int) __asm__("tolower");
extern int reference_toupper(int) __asm__("toupper");
extern int reference_isascii(int) __asm__("isascii");
extern int reference_toascii(int) __asm__("toascii");
extern intmax_t reference_strtoimax(const char *,char **,int) __asm__("strtoimax");
extern uintmax_t reference_strtoumax(const char *,char **,int) __asm__("strtoumax");
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
  DIFFERENTIAL(strtoimax,reference_strtoimax);DIFFERENTIAL(strtoumax,reference_strtoumax);
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
static void formats(void){
 char text[80];
 {
  int8_t s_d=-37,r_d=0;assert(snprintf(text,sizeof(text),"%" PRId8,s_d)==3);assert(sscanf(text,"%" SCNd8,&r_d)==1&&r_d==s_d);
  int8_t s_i=-37,r_i=0;assert(snprintf(text,sizeof(text),"%" PRIi8,s_i)==3);assert(sscanf(text,"%" SCNi8,&r_i)==1&&r_i==s_i);
  uint8_t u_o=117,v_o=0;assert(snprintf(text,sizeof(text),"%" PRIo8,u_o)>0);assert(sscanf(text,"%" SCNo8,&v_o)==1&&u_o==v_o);
  uint8_t u_u=117,v_u=0;assert(snprintf(text,sizeof(text),"%" PRIu8,u_u)>0);assert(sscanf(text,"%" SCNu8,&v_u)==1&&u_u==v_u);
  uint8_t u_x=117,v_x=0;assert(snprintf(text,sizeof(text),"%" PRIx8,u_x)>0);assert(sscanf(text,"%" SCNx8,&v_x)==1&&u_x==v_x);
 }
 {
  int16_t s_d=-37,r_d=0;assert(snprintf(text,sizeof(text),"%" PRId16,s_d)==3);assert(sscanf(text,"%" SCNd16,&r_d)==1&&r_d==s_d);
  int16_t s_i=-37,r_i=0;assert(snprintf(text,sizeof(text),"%" PRIi16,s_i)==3);assert(sscanf(text,"%" SCNi16,&r_i)==1&&r_i==s_i);
  uint16_t u_o=117,v_o=0;assert(snprintf(text,sizeof(text),"%" PRIo16,u_o)>0);assert(sscanf(text,"%" SCNo16,&v_o)==1&&u_o==v_o);
  uint16_t u_u=117,v_u=0;assert(snprintf(text,sizeof(text),"%" PRIu16,u_u)>0);assert(sscanf(text,"%" SCNu16,&v_u)==1&&u_u==v_u);
  uint16_t u_x=117,v_x=0;assert(snprintf(text,sizeof(text),"%" PRIx16,u_x)>0);assert(sscanf(text,"%" SCNx16,&v_x)==1&&u_x==v_x);
 }
 {
  int32_t s_d=-37,r_d=0;assert(snprintf(text,sizeof(text),"%" PRId32,s_d)==3);assert(sscanf(text,"%" SCNd32,&r_d)==1&&r_d==s_d);
  int32_t s_i=-37,r_i=0;assert(snprintf(text,sizeof(text),"%" PRIi32,s_i)==3);assert(sscanf(text,"%" SCNi32,&r_i)==1&&r_i==s_i);
  uint32_t u_o=117,v_o=0;assert(snprintf(text,sizeof(text),"%" PRIo32,u_o)>0);assert(sscanf(text,"%" SCNo32,&v_o)==1&&u_o==v_o);
  uint32_t u_u=117,v_u=0;assert(snprintf(text,sizeof(text),"%" PRIu32,u_u)>0);assert(sscanf(text,"%" SCNu32,&v_u)==1&&u_u==v_u);
  uint32_t u_x=117,v_x=0;assert(snprintf(text,sizeof(text),"%" PRIx32,u_x)>0);assert(sscanf(text,"%" SCNx32,&v_x)==1&&u_x==v_x);
 }
 {
  int64_t s_d=-37,r_d=0;assert(snprintf(text,sizeof(text),"%" PRId64,s_d)==3);assert(sscanf(text,"%" SCNd64,&r_d)==1&&r_d==s_d);
  int64_t s_i=-37,r_i=0;assert(snprintf(text,sizeof(text),"%" PRIi64,s_i)==3);assert(sscanf(text,"%" SCNi64,&r_i)==1&&r_i==s_i);
  uint64_t u_o=117,v_o=0;assert(snprintf(text,sizeof(text),"%" PRIo64,u_o)>0);assert(sscanf(text,"%" SCNo64,&v_o)==1&&u_o==v_o);
  uint64_t u_u=117,v_u=0;assert(snprintf(text,sizeof(text),"%" PRIu64,u_u)>0);assert(sscanf(text,"%" SCNu64,&v_u)==1&&u_u==v_u);
  uint64_t u_x=117,v_x=0;assert(snprintf(text,sizeof(text),"%" PRIx64,u_x)>0);assert(sscanf(text,"%" SCNx64,&v_x)==1&&u_x==v_x);
 }
 {
  int_least8_t s_d=-37,r_d=0;assert(snprintf(text,sizeof(text),"%" PRIdLEAST8,s_d)==3);assert(sscanf(text,"%" SCNdLEAST8,&r_d)==1&&r_d==s_d);
  int_least8_t s_i=-37,r_i=0;assert(snprintf(text,sizeof(text),"%" PRIiLEAST8,s_i)==3);assert(sscanf(text,"%" SCNiLEAST8,&r_i)==1&&r_i==s_i);
  uint_least8_t u_o=117,v_o=0;assert(snprintf(text,sizeof(text),"%" PRIoLEAST8,u_o)>0);assert(sscanf(text,"%" SCNoLEAST8,&v_o)==1&&u_o==v_o);
  uint_least8_t u_u=117,v_u=0;assert(snprintf(text,sizeof(text),"%" PRIuLEAST8,u_u)>0);assert(sscanf(text,"%" SCNuLEAST8,&v_u)==1&&u_u==v_u);
  uint_least8_t u_x=117,v_x=0;assert(snprintf(text,sizeof(text),"%" PRIxLEAST8,u_x)>0);assert(sscanf(text,"%" SCNxLEAST8,&v_x)==1&&u_x==v_x);
 }
 {
  int_least16_t s_d=-37,r_d=0;assert(snprintf(text,sizeof(text),"%" PRIdLEAST16,s_d)==3);assert(sscanf(text,"%" SCNdLEAST16,&r_d)==1&&r_d==s_d);
  int_least16_t s_i=-37,r_i=0;assert(snprintf(text,sizeof(text),"%" PRIiLEAST16,s_i)==3);assert(sscanf(text,"%" SCNiLEAST16,&r_i)==1&&r_i==s_i);
  uint_least16_t u_o=117,v_o=0;assert(snprintf(text,sizeof(text),"%" PRIoLEAST16,u_o)>0);assert(sscanf(text,"%" SCNoLEAST16,&v_o)==1&&u_o==v_o);
  uint_least16_t u_u=117,v_u=0;assert(snprintf(text,sizeof(text),"%" PRIuLEAST16,u_u)>0);assert(sscanf(text,"%" SCNuLEAST16,&v_u)==1&&u_u==v_u);
  uint_least16_t u_x=117,v_x=0;assert(snprintf(text,sizeof(text),"%" PRIxLEAST16,u_x)>0);assert(sscanf(text,"%" SCNxLEAST16,&v_x)==1&&u_x==v_x);
 }
 {
  int_least32_t s_d=-37,r_d=0;assert(snprintf(text,sizeof(text),"%" PRIdLEAST32,s_d)==3);assert(sscanf(text,"%" SCNdLEAST32,&r_d)==1&&r_d==s_d);
  int_least32_t s_i=-37,r_i=0;assert(snprintf(text,sizeof(text),"%" PRIiLEAST32,s_i)==3);assert(sscanf(text,"%" SCNiLEAST32,&r_i)==1&&r_i==s_i);
  uint_least32_t u_o=117,v_o=0;assert(snprintf(text,sizeof(text),"%" PRIoLEAST32,u_o)>0);assert(sscanf(text,"%" SCNoLEAST32,&v_o)==1&&u_o==v_o);
  uint_least32_t u_u=117,v_u=0;assert(snprintf(text,sizeof(text),"%" PRIuLEAST32,u_u)>0);assert(sscanf(text,"%" SCNuLEAST32,&v_u)==1&&u_u==v_u);
  uint_least32_t u_x=117,v_x=0;assert(snprintf(text,sizeof(text),"%" PRIxLEAST32,u_x)>0);assert(sscanf(text,"%" SCNxLEAST32,&v_x)==1&&u_x==v_x);
 }
 {
  int_least64_t s_d=-37,r_d=0;assert(snprintf(text,sizeof(text),"%" PRIdLEAST64,s_d)==3);assert(sscanf(text,"%" SCNdLEAST64,&r_d)==1&&r_d==s_d);
  int_least64_t s_i=-37,r_i=0;assert(snprintf(text,sizeof(text),"%" PRIiLEAST64,s_i)==3);assert(sscanf(text,"%" SCNiLEAST64,&r_i)==1&&r_i==s_i);
  uint_least64_t u_o=117,v_o=0;assert(snprintf(text,sizeof(text),"%" PRIoLEAST64,u_o)>0);assert(sscanf(text,"%" SCNoLEAST64,&v_o)==1&&u_o==v_o);
  uint_least64_t u_u=117,v_u=0;assert(snprintf(text,sizeof(text),"%" PRIuLEAST64,u_u)>0);assert(sscanf(text,"%" SCNuLEAST64,&v_u)==1&&u_u==v_u);
  uint_least64_t u_x=117,v_x=0;assert(snprintf(text,sizeof(text),"%" PRIxLEAST64,u_x)>0);assert(sscanf(text,"%" SCNxLEAST64,&v_x)==1&&u_x==v_x);
 }
 {
  int_fast8_t s_d=-37,r_d=0;assert(snprintf(text,sizeof(text),"%" PRIdFAST8,s_d)==3);assert(sscanf(text,"%" SCNdFAST8,&r_d)==1&&r_d==s_d);
  int_fast8_t s_i=-37,r_i=0;assert(snprintf(text,sizeof(text),"%" PRIiFAST8,s_i)==3);assert(sscanf(text,"%" SCNiFAST8,&r_i)==1&&r_i==s_i);
  uint_fast8_t u_o=117,v_o=0;assert(snprintf(text,sizeof(text),"%" PRIoFAST8,u_o)>0);assert(sscanf(text,"%" SCNoFAST8,&v_o)==1&&u_o==v_o);
  uint_fast8_t u_u=117,v_u=0;assert(snprintf(text,sizeof(text),"%" PRIuFAST8,u_u)>0);assert(sscanf(text,"%" SCNuFAST8,&v_u)==1&&u_u==v_u);
  uint_fast8_t u_x=117,v_x=0;assert(snprintf(text,sizeof(text),"%" PRIxFAST8,u_x)>0);assert(sscanf(text,"%" SCNxFAST8,&v_x)==1&&u_x==v_x);
 }
 {
  int_fast16_t s_d=-37,r_d=0;assert(snprintf(text,sizeof(text),"%" PRIdFAST16,s_d)==3);assert(sscanf(text,"%" SCNdFAST16,&r_d)==1&&r_d==s_d);
  int_fast16_t s_i=-37,r_i=0;assert(snprintf(text,sizeof(text),"%" PRIiFAST16,s_i)==3);assert(sscanf(text,"%" SCNiFAST16,&r_i)==1&&r_i==s_i);
  uint_fast16_t u_o=117,v_o=0;assert(snprintf(text,sizeof(text),"%" PRIoFAST16,u_o)>0);assert(sscanf(text,"%" SCNoFAST16,&v_o)==1&&u_o==v_o);
  uint_fast16_t u_u=117,v_u=0;assert(snprintf(text,sizeof(text),"%" PRIuFAST16,u_u)>0);assert(sscanf(text,"%" SCNuFAST16,&v_u)==1&&u_u==v_u);
  uint_fast16_t u_x=117,v_x=0;assert(snprintf(text,sizeof(text),"%" PRIxFAST16,u_x)>0);assert(sscanf(text,"%" SCNxFAST16,&v_x)==1&&u_x==v_x);
 }
 {
  int_fast32_t s_d=-37,r_d=0;assert(snprintf(text,sizeof(text),"%" PRIdFAST32,s_d)==3);assert(sscanf(text,"%" SCNdFAST32,&r_d)==1&&r_d==s_d);
  int_fast32_t s_i=-37,r_i=0;assert(snprintf(text,sizeof(text),"%" PRIiFAST32,s_i)==3);assert(sscanf(text,"%" SCNiFAST32,&r_i)==1&&r_i==s_i);
  uint_fast32_t u_o=117,v_o=0;assert(snprintf(text,sizeof(text),"%" PRIoFAST32,u_o)>0);assert(sscanf(text,"%" SCNoFAST32,&v_o)==1&&u_o==v_o);
  uint_fast32_t u_u=117,v_u=0;assert(snprintf(text,sizeof(text),"%" PRIuFAST32,u_u)>0);assert(sscanf(text,"%" SCNuFAST32,&v_u)==1&&u_u==v_u);
  uint_fast32_t u_x=117,v_x=0;assert(snprintf(text,sizeof(text),"%" PRIxFAST32,u_x)>0);assert(sscanf(text,"%" SCNxFAST32,&v_x)==1&&u_x==v_x);
 }
 {
  int_fast64_t s_d=-37,r_d=0;assert(snprintf(text,sizeof(text),"%" PRIdFAST64,s_d)==3);assert(sscanf(text,"%" SCNdFAST64,&r_d)==1&&r_d==s_d);
  int_fast64_t s_i=-37,r_i=0;assert(snprintf(text,sizeof(text),"%" PRIiFAST64,s_i)==3);assert(sscanf(text,"%" SCNiFAST64,&r_i)==1&&r_i==s_i);
  uint_fast64_t u_o=117,v_o=0;assert(snprintf(text,sizeof(text),"%" PRIoFAST64,u_o)>0);assert(sscanf(text,"%" SCNoFAST64,&v_o)==1&&u_o==v_o);
  uint_fast64_t u_u=117,v_u=0;assert(snprintf(text,sizeof(text),"%" PRIuFAST64,u_u)>0);assert(sscanf(text,"%" SCNuFAST64,&v_u)==1&&u_u==v_u);
  uint_fast64_t u_x=117,v_x=0;assert(snprintf(text,sizeof(text),"%" PRIxFAST64,u_x)>0);assert(sscanf(text,"%" SCNxFAST64,&v_x)==1&&u_x==v_x);
 }
 {
  intmax_t s_d=-37,r_d=0;assert(snprintf(text,sizeof(text),"%" PRIdMAX,s_d)==3);assert(sscanf(text,"%" SCNdMAX,&r_d)==1&&r_d==s_d);
  intmax_t s_i=-37,r_i=0;assert(snprintf(text,sizeof(text),"%" PRIiMAX,s_i)==3);assert(sscanf(text,"%" SCNiMAX,&r_i)==1&&r_i==s_i);
  uintmax_t u_o=117,v_o=0;assert(snprintf(text,sizeof(text),"%" PRIoMAX,u_o)>0);assert(sscanf(text,"%" SCNoMAX,&v_o)==1&&u_o==v_o);
  uintmax_t u_u=117,v_u=0;assert(snprintf(text,sizeof(text),"%" PRIuMAX,u_u)>0);assert(sscanf(text,"%" SCNuMAX,&v_u)==1&&u_u==v_u);
  uintmax_t u_x=117,v_x=0;assert(snprintf(text,sizeof(text),"%" PRIxMAX,u_x)>0);assert(sscanf(text,"%" SCNxMAX,&v_x)==1&&u_x==v_x);
 }
 {
  intptr_t s_d=-37,r_d=0;assert(snprintf(text,sizeof(text),"%" PRIdPTR,s_d)==3);assert(sscanf(text,"%" SCNdPTR,&r_d)==1&&r_d==s_d);
  intptr_t s_i=-37,r_i=0;assert(snprintf(text,sizeof(text),"%" PRIiPTR,s_i)==3);assert(sscanf(text,"%" SCNiPTR,&r_i)==1&&r_i==s_i);
  uintptr_t u_o=117,v_o=0;assert(snprintf(text,sizeof(text),"%" PRIoPTR,u_o)>0);assert(sscanf(text,"%" SCNoPTR,&v_o)==1&&u_o==v_o);
  uintptr_t u_u=117,v_u=0;assert(snprintf(text,sizeof(text),"%" PRIuPTR,u_u)>0);assert(sscanf(text,"%" SCNuPTR,&v_u)==1&&u_u==v_u);
  uintptr_t u_x=117,v_x=0;assert(snprintf(text,sizeof(text),"%" PRIxPTR,u_x)>0);assert(sscanf(text,"%" SCNxPTR,&v_x)==1&&u_x==v_x);
 }
}
static void bytes(void){
 assert(setlocale(LC_CTYPE,"C"));
 for(int c=-1;c<=255;c++){
  assert(!!isalnum(c)==!!reference_isalnum(c));
  assert(!!isalpha(c)==!!reference_isalpha(c));
  assert(!!isblank(c)==!!reference_isblank(c));
  assert(!!iscntrl(c)==!!reference_iscntrl(c));
  assert(!!isdigit(c)==!!reference_isdigit(c));
  assert(!!isgraph(c)==!!reference_isgraph(c));
  assert(!!islower(c)==!!reference_islower(c));
  assert(!!isprint(c)==!!reference_isprint(c));
  assert(!!ispunct(c)==!!reference_ispunct(c));
  assert(!!isspace(c)==!!reference_isspace(c));
  assert(!!isupper(c)==!!reference_isupper(c));
  assert(!!isxdigit(c)==!!reference_isxdigit(c));
  assert(tolower(c)==reference_tolower(c));
  assert(toupper(c)==reference_toupper(c));
  assert(isascii(c)==reference_isascii(c));
  assert(toascii(c)==reference_toascii(c));
 }
 assert(imaxabs(-INTMAX_MAX)==INTMAX_MAX);
 for(intmax_t n=-99;n<=99;n++)for(intmax_t d=-11;d<=11;d++)if(d){imaxdiv_t v=imaxdiv(n,d);assert(v.quot==n/d&&v.rem==n%d);}
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
int main(void){numeric();formats();bytes();unicode();puts("native base C: numeric/intmax differential cases,all integer format widths,C-locale bytes,sorting/search,all Unicode scalars,chunking,UTF-16 and malformed input passed");return 0;}

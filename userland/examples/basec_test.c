#include <stdlib.h>
#include <uchar.h>
#include <pthread.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include "chos/user.h"
#include "chos/libc.h"
static const char *sequences[]={"\xe2\x82\xac","\xe6\x97\xa5","\xf0\x9f\x98\x80"};
static const char32_t scalars[]={0x20ac,0x65e5,0x1f600};
static void say(const char *s){chos_write(s,strlen(s));}
static int fail(unsigned line){char s[11];s[10]='\n';for(unsigned i=0;i<10;i++){s[9-i]=(char)('0'+line%10);line/=10;}say("[FAIL] base C line ");chos_write(s,sizeof(s));return 1;}
#define CHECK(x) do{if(!(x))return fail(__LINE__);}while(0)
static int compare(const void *a,const void *b){int x=*(const int *)a,y=*(const int *)b;return (x>y)-(x<y);}
static void *worker(void *argument){
 unsigned index=(unsigned)(uintptr_t)argument;const char *seq=sequences[index];size_t length=strlen(seq);char32_t expected=scalars[index];
 for(unsigned i=0;i<64;i++){
  errno=100+(int)index;char32_t c=0;
  if(mbrtoc32(&c,seq,1,0)!=(size_t)-2)return (void *)1;
  chos_yield();if(!(i&7u))chos_sleep_ticks(1);
  if(mbrtoc32(&c,seq+1,length-1,0)!=length-1||c!=expected||errno!=100+(int)index)return (void *)2;
  char output[4];if(c32rtomb(output,c,0)!=length||memcmp(output,seq,length))return (void *)3;
  char16_t unit=0;
  if(mbrtoc16(&unit,seq,length,0)!=length)return (void *)4;
  if(expected>0xffff){
   if(c16rtomb(output,unit,0)!=0)return (void *)5;chos_yield();
   if(mbrtoc16(&unit,"",0,0)!=(size_t)-3)return (void *)6;
  }
  if(c16rtomb(output,unit,0)!=length||memcmp(output,seq,length))return (void *)7;
  if(mbrtoc32(&c,"\xff",1,0)!=(size_t)-1||errno!=EILSEQ)return (void *)8;
  chos_yield();if(errno!=EILSEQ)return (void *)9;
 }
 return (void *)(uintptr_t)(0x321u+index);
}
int main(void){
 char *end;errno=17;
 CHECK(strtol(" \t-0x123tail",&end,0)==-0x123&&!strcmp(end,"tail")&&errno==17);
 CHECK(strtoll("-9223372036854775808",&end,10)==LLONG_MIN&&!*end);
 CHECK(strtoull("18446744073709551615",&end,10)==ULLONG_MAX&&!*end);
 errno=0;CHECK(strtoll("9223372036854775808",&end,10)==LLONG_MAX&&errno==ERANGE&&!*end);
 errno=0;CHECK(strtoull("18446744073709551616x",&end,10)==ULLONG_MAX&&errno==ERANGE&&*end=='x');
 CHECK(strtol("",&end,10)==0&&*end==0);
 CHECK(atoi("-91")==-91&&atol("12345")==12345&&atoll("900000000000")==900000000000LL);
 CHECK(div(-29,6).quot==-4&&ldiv(-29,6).rem==-5&&lldiv(-29,6).rem==-5);
 int values[513];for(unsigned i=0;i<513;i++)values[i]=(int)((i*71u)%513u)-256;
 qsort(values,513,sizeof(int),compare);for(unsigned i=1;i<513;i++)CHECK(values[i-1]<=values[i]);
 for(unsigned i=0;i<513;i++){int *p=bsearch(&values[i],values,513,sizeof(int),compare);CHECK(p&&*p==values[i]);}
 mbstate_t state={0};char32_t c=0;
 CHECK(mbrtoc32(&c,"\xf4\x8f\xbf\xbf",4,&state)==4&&c==0x10ffff&&mbsinit(&state));
 CHECK(mbrtoc32(&c,"\xed\xa0\x80",3,&state)==(size_t)-1&&errno==EILSEQ&&mbsinit(&state));
 for(unsigned lifecycle=0;lifecycle<3;lifecycle++){
  pthread_t workers[3];for(unsigned i=0;i<3;i++)CHECK(!pthread_create(&workers[i],0,worker,(void *)(uintptr_t)i));
  for(unsigned i=0;i<3;i++){void *result=0;CHECK(!pthread_join(workers[i],&result)&&result==(void *)(uintptr_t)(0x321u+i));}
  chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&stats.allocations==0&&stats.regions==0);
 }
 say("[ OK ] native base C parsing,sort/search,UTF-8/16/32 and implicit thread state passed\n");return 0;
}

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <fenv.h>
#include <thread>
#include <atomic>
#include "chos/user.h"
#include "chos/libc.h"
#include "format_vectors.h"
static void say(const char *s){chos_write(s,strlen(s));}
static int fail(unsigned line){char n[11];n[10]='\n';for(unsigned i=0;i<10;i++){n[9-i]=char('0'+line%10);line/=10;}say("[FAIL] native format line ");chos_write(n,11);return 1;}
#define CHECK(x) do{if(!(x))return fail(__LINE__);}while(0)
static int copied(char *out,size_t size,const char *format,...){va_list first,second;va_start(first,format);va_copy(second,first);int a=vsnprintf(nullptr,0,format,first),b=vsnprintf(out,size,format,second);va_end(second);va_end(first);return a==b?b:-100;}
static int vectors(){
 for(const auto& v:format_vectors){
  unsigned char memory[258];size_t expected=strlen(v.expected);CHECK(expected<256);
  for(size_t cap: {size_t(0),size_t(1),size_t(2),size_t(7),size_t(31),size_t(256)}){
   memset(memory,0x55,sizeof(memory));int n;
   if(v.width==64){double value;memcpy(&value,&v.lo,8);n=snprintf((char *)memory+1,cap,v.format,value);}
   else{long double value;uint64_t words[2]={v.lo,v.hi};static_assert(sizeof(value)==16);memcpy(&value,words,16);n=snprintf((char *)memory+1,cap,v.format,value);}
   if(n!=(int)expected){say(v.format);say(" expected: ");say(v.expected);say(" actual: ");if(cap)say((char *)memory+1);say("\n");return fail(__LINE__);}
   CHECK(memory[0]==0x55);size_t used=cap?(expected<cap?expected:cap-1):0;
   CHECK(!memcmp(memory+1,v.expected,used));if(cap)CHECK(memory[1+used]==0);
   for(size_t j=1+cap;j<sizeof(memory);j++)CHECK(memory[j]==0x55);
  }
 }
 return 0;
}
static int basic(){
 char out[128];CHECK(snprintf(out,sizeof(out),"%lld/%llu/%#x/%zu",(long long)INT64_MIN,(unsigned long long)UINT64_MAX,0x1a,(size_t)17)==49);
 CHECK(!strcmp(out,"-9223372036854775808/18446744073709551615/0x1a/17"));
 CHECK(copied(out,sizeof(out),"%2$*1$d %3$s",6,-37,"position")==15&&!strcmp(out,"   -37 position"));
 CHECK(copied(out,sizeof(out),"%d/%.2f/%.2Lf/%s",3,1.25,3.5L,"mixed")==17&&!strcmp(out,"3/1.25/3.50/mixed"));
 int n=-1;CHECK(snprintf(out,4,"abc%nXYZ",&n)==6&&n==3&&!strcmp(out,"abc"));
 CHECK(snprintf(nullptr,0,"abcdef")==6);
 CHECK(snprintf(out,sizeof(out),"%ls/%lc",L"\u20ac \U0001f680",(unsigned)0x03bb)==11&&!strcmp(out,"\xe2\x82\xac \xf0\x9f\x9a\x80/\xce\xbb"));
 errno=0;CHECK(snprintf(out,sizeof(out),"%*d",INT_MIN,1)==-1&&errno==EOVERFLOW);
 errno=0;CHECK(snprintf(out,sizeof(out),"%0$d",1)==-1&&errno==EINVAL);
 wchar_t invalid[]={0xd800,0};errno=0;CHECK(snprintf(out,sizeof(out),"%ls",invalid)==-1&&errno==EILSEQ);
 return 0;
}
static std::atomic<unsigned> failures;
static int work(unsigned index){
 int modes[]={FE_TONEAREST,FE_UPWARD,FE_DOWNWARD};const char *expected[]={"2/-2","3/-2","2/-3"};
 CHECK(!fesetround(modes[index]));
 for(unsigned cycle=0;cycle<32;cycle++){
  char out[128];errno=31+(int)index;
  CHECK(snprintf(out,sizeof(out),"%.0f/%.0f",2.5,-2.5)==4&&!strcmp(out,expected[index]));
  CHECK(snprintf(out,sizeof(out),"%d:%.2Lf:%s",42,3.25L,"thread")==14&&!strcmp(out,"42:3.25:thread"));
  chos_yield();CHECK(fegetround()==modes[index]&&errno==31+(int)index);
 }
 return 0;
}
static void worker(unsigned index){if(work(index))++failures;}
extern "C" int main(){
 CHECK(!fesetround(FE_TONEAREST));CHECK(!basic()&&!vectors());
 for(unsigned cycle=0;cycle<3;cycle++){
  std::thread t[3];for(unsigned i=0;i<3;i++)t[i]=std::thread(worker,i);for(auto& thread:t)thread.join();CHECK(!failures&&fegetround()==FE_TONEAREST);
  chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&!stats.allocations&&!stats.mapped_bytes);
 }
 say("[ OK ] native bounded formatting,binary128 and private rounding states passed\n");return 0;
}

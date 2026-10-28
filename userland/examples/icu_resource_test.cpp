#include "unicode/uclean.h"
#include "unicode/ucnv.h"
#include "unicode/ucnv_err.h"
#include "unicode/udata.h"
#include "unicode/icudataver.h"
#include "unicode/putil.h"
#include "unicode/ures.h"
#include "unicode/unum.h"
#include "unicode/ubrk.h"
#include "chos/user.h"
#include "chos/libc.h"
#include <stdio.h>
#include <string.h>
#include <thread>
#include <atomic>
static void say(const char *s){chos_write(s,strlen(s));}
static int fail(unsigned line){char s[100];snprintf(s,sizeof(s),"[FAIL] native ICU generated resources line %u\n",line);say(s);return 1;}
#define CHECK(x) do{if(!(x))return fail(__LINE__);}while(0)
static int clean(){u_cleanup();chos_heap_stats_t s;CHECK(!chos_libc_heap_info(&s)&&!s.allocations&&!s.mapped_bytes);return 0;}
static int init(){UErrorCode status=U_ZERO_ERROR;u_init(&status);CHECK(U_SUCCESS(status));UVersionInfo v;u_getDataVersion(v,&status);CHECK(U_SUCCESS(status)&&v[0]==79&&v[1]==1);return 0;}
static int large_resources(){
 const char *packages[]={nullptr,U_ICUDATA_NAME "-curr",nullptr};const char *names[]={"pool","pool","supplementalData"};
 for(unsigned i=0;i<3;i++){
  UErrorCode status=U_ZERO_ERROR;UDataMemory *data=udata_open(packages[i],"res",names[i],&status);CHECK(data&&U_SUCCESS(status)&&udata_getMemory(data));
  UDataInfo info{};info.size=sizeof(info);udata_getInfo(data,&info);CHECK(!memcmp(info.dataFormat,"ResB",4));udata_close(data);
 }
 return 0;
}
static int numbers(){
 const char *locales[]={"en","de","fr"};const char16_t *expected[]={u"12,345.5",u"12.345,5",u"12\u202f345,5"};
 for(unsigned i=0;i<3;i++){
  UErrorCode status=U_ZERO_ERROR;auto *f=unum_open(UNUM_DECIMAL,nullptr,0,locales[i],nullptr,&status);CHECK(f&&U_SUCCESS(status));
  char16_t out[64];int32_t n=unum_formatDouble(f,12345.5,out,64,nullptr,&status);CHECK(U_SUCCESS(status)&&n==8&&!memcmp(out,expected[i],16));
  int32_t position=0;double value=unum_parseDouble(f,out,n,&position,&status);CHECK(U_SUCCESS(status)&&value==12345.5&&position==n);
  position=0;status=U_ZERO_ERROR;(void)unum_parseDouble(f,u"invalid",7,&position,&status);CHECK(status==U_PARSE_ERROR&&position==0);
  unum_close(f);
 }
 return 0;
}
static int converters(){
 UErrorCode status=U_ZERO_ERROR;auto *converter=ucnv_open("windows-1252",&status);CHECK(converter&&U_SUCCESS(status));
 const char input[]={char(0x80),char(0x97),char(0x8c),char(0xe9)};const char16_t expected[]=u"\u20ac\u2014\u0152\u00e9";char16_t out[20];char bytes[20];
 int n=ucnv_toUChars(converter,out,20,input,4,&status);CHECK(U_SUCCESS(status)&&n==4&&!memcmp(out,expected,8));
 n=ucnv_fromUChars(converter,bytes,20,expected,4,&status);CHECK(U_SUCCESS(status)&&n==4&&!memcmp(bytes,input,4));
 ucnv_setFromUCallBack(converter,UCNV_FROM_U_CALLBACK_STOP,nullptr,nullptr,nullptr,&status);CHECK(U_SUCCESS(status));
 status=U_ZERO_ERROR;(void)ucnv_fromUChars(converter,bytes,20,u"\u03bb",1,&status);CHECK(status==U_INVALID_CHAR_FOUND);ucnv_close(converter);
 status=U_ZERO_ERROR;converter=ucnv_open("ibm-943_P130-1999",&status);CHECK(converter&&U_SUCCESS(status));
 const char japan[]={char(0x93),char(0xfa),char(0x96),char(0x7b)};
 n=ucnv_toUChars(converter,out,20,japan,4,&status);CHECK(U_SUCCESS(status)&&n==2&&!memcmp(out,u"\u65e5\u672c",4));
 n=ucnv_fromUChars(converter,bytes,20,out,2,&status);CHECK(U_SUCCESS(status)&&n==4&&!memcmp(bytes,japan,4));ucnv_close(converter);
 status=U_ZERO_ERROR;converter=ucnv_open("CHOS-NOT-A-CONVERTER",&status);CHECK(!converter&&U_FAILURE(status));return 0;
}
static int breaks(){
 UErrorCode status=U_ZERO_ERROR;auto *b=ubrk_open(UBRK_CHARACTER,"en",u"A\u0308\U0001f680B",5,&status);CHECK(b&&U_SUCCESS(status));
 const int positions[]={0,2,4,5};unsigned count=0;for(int at=ubrk_first(b);at!=UBRK_DONE;at=ubrk_next(b)){CHECK(count<4&&at==positions[count]);count++;}CHECK(count==4);ubrk_close(b);
 status=U_ZERO_ERROR;b=ubrk_open(UBRK_WORD,"en",u"Hello, world!",13,&status);CHECK(b&&U_SUCCESS(status));
 const int words[]={0,5,6,7,12,13};count=0;for(int at=ubrk_first(b);at!=UBRK_DONE;at=ubrk_next(b)){CHECK(count<6&&at==words[count]);count++;}CHECK(count==6);ubrk_close(b);return 0;
}
static std::atomic<unsigned> failures;
static void worker(){for(unsigned i=0;i<3;i++){if(numbers()||converters()||breaks())++failures;std::this_thread::yield();}}
extern "C" int main(){
 u_setDataDirectory("/ICU-MISSING");UErrorCode status=U_ZERO_ERROR;u_init(&status);CHECK(U_FAILURE(status));CHECK(!clean());
 for(unsigned cycle=0;cycle<3;cycle++){
  u_setDataDirectory("/ICU");CHECK(!large_resources()&&!init()&&!numbers()&&!converters()&&!breaks());
  std::thread t[3];for(auto& thread:t)thread=std::thread(worker);for(auto& thread:t)thread.join();CHECK(!failures);
  CHECK(!clean());
 }
 say("[ OK ] native ICU generated locale,converter,break and cleanup passed\n");return 0;
}

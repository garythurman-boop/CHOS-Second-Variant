#include "config.h"
#include <wtf/DateMath.h>
#include <wtf/WTFProcess.h>
#include <wtf/SIMDUTF.h>
#include <chos/user.h>
#include <chos/libc.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <stdio.h>
#include <sys/mman.h>
#include <atomic>
#include <thread>
#include <cstdlib>
#include "calendar_vectors.h"
static bool fail(unsigned line){printf("[FAIL] native calendar services line %u\n",line);return false;}
#define CHECK(v) do{if(!(v))return fail(__LINE__);}while(0)
static void say(const char *s){chos_write(s,strlen(s));}
static void forbidden_finalizer(){say("[FAIL] native immediate-exit finalizer ran\n");}
static void expected_finalizer(){say("[EXPECT] native normal exit finalizer\n");}
static void export_fields(const tm &v,int f[9]){
 f[0]=v.tm_sec;f[1]=v.tm_min;f[2]=v.tm_hour;f[3]=v.tm_mday;f[4]=v.tm_mon;f[5]=v.tm_year;f[6]=v.tm_wday;f[7]=v.tm_yday;f[8]=v.tm_isdst;
}
static bool dates(unsigned begin,unsigned stride){
 for(size_t i=begin;i<std::size(calendar_vectors);i+=stride){auto &v=calendar_vectors[i];
  tm input{v.fields[0],v.fields[1],v.fields[2],v.fields[3],v.fields[4],v.fields[5],v.fields[6],v.fields[7],v.fields[8]},copy=input;
  int fields[9];errno=0;time_t result=timegm(&copy);
  if(v.error)CHECK(result==-1&&errno==EOVERFLOW&&!memcmp(&copy,&input,sizeof copy));
  else{export_fields(copy,fields);CHECK(result==v.seconds&&!errno&&!memcmp(fields,v.normalized,sizeof fields));
   time_t seconds=v.seconds;tm split;errno=0;CHECK(gmtime_r(&seconds,&split)==&split&&!errno);
   export_fields(split,fields);CHECK(!memcmp(fields,v.normalized,sizeof fields));
   CHECK(localtime_r(&seconds,&split)==&split);export_fields(split,fields);CHECK(!memcmp(fields,v.normalized,sizeof fields));
  }
  copy=input;errno=0;result=mktime(&copy);
  if(input.tm_isdst>0)CHECK(result==-1&&errno==ENOTSUP&&!memcmp(&copy,&input,sizeof copy));
  else if(v.error)CHECK(result==-1&&errno==EOVERFLOW&&!memcmp(&copy,&input,sizeof copy));
  else{export_fields(copy,fields);CHECK(result==v.seconds&&!errno&&!memcmp(fields,v.normalized,sizeof fields));}
 }
 return true;
}
static bool engine_dates(){
 struct{const char *text;double milliseconds;} cases[]={
  {"1970-01-01T00:00:00.000Z",0},{"1969-12-31T23:59:59.000Z",-1000},
  {"2000-02-29T00:00:00Z",951782400000.0},{"2100-03-01T00:00:00Z",4107542400000.0},
  {"2000-01-01T01:30:00+01:30",946684800000.0}};
 for(auto &v:cases){bool local=true;auto text=std::span(reinterpret_cast<const Latin1Character *>(v.text),strlen(v.text));
  CHECK(WTF::parseES5Date(text,local)==v.milliseconds&&!local);
  auto offset=WTF::calculateLocalTimeOffset(v.milliseconds);CHECK(!offset.offset&&!offset.isDST);
 }
 for(auto s:{"not a date","2024-13-01T00:00:00Z","2000-01-01T00:00:00+25:00"}){
  bool local=false;auto value=WTF::parseES5Date(std::span(reinterpret_cast<const Latin1Character *>(s),strlen(s)),local);CHECK(__builtin_isnan(value));
 }
 return true;
}
static bool text_services(){
 CHECK(!strcasecmp("UTF-8","utf-8")&&!strncasecmp("HTTPSjunk","https?",5));
 CHECK(strcasecmp("A","b")<0&&strncasecmp("x","X",0)==0);
 CHECK(!strcmp(strerror(EPERM),"Operation not permitted"));
 for(int code:{EPERM,EINVAL,ENOMEM,-1,2147483647}){
  const char *message=strerror(code);size_t length=strlen(message);
  for(size_t cap:{size_t(0),size_t(1),length,length+1,size_t(128)}){
   unsigned char b[132];memset(b,0xa5,sizeof b);errno=EBUSY;int result=strerror_r(code,reinterpret_cast<char *>(b+1),cap);
   CHECK(result==(cap>length?0:ERANGE)&&errno==EBUSY&&b[0]==0xa5&&b[cap+1]==0xa5);
   if(cap){size_t copied=cap>length?length:cap-1;CHECK(!memcmp(b+1,message,copied)&&!b[copied+1]);}
  }
 }
 return true;
}
static bool unicode(){
 const char text[]="A\xc2\xa2\xe2\x82\xac\xf0\x9f\x98\x80";
 const char16_t expected[]={0x41,0xa2,0x20ac,0xd83d,0xde00};
 CHECK(simdutf::validate_utf8(text,sizeof text-1));char16_t out[7];for(auto &c:out)c=0xa55a;
 CHECK(simdutf::convert_utf8_to_utf16le(text,sizeof text-1,out+1)==5&&out[0]==0xa55a&&out[6]==0xa55a&&!memcmp(out+1,expected,sizeof expected));
 char roundtrip[32];memset(roundtrip,0xa5,sizeof roundtrip);
 CHECK(simdutf::convert_utf16le_to_utf8(expected,5,roundtrip+1)==sizeof text-1&&static_cast<unsigned char>(roundtrip[0])==0xa5&&static_cast<unsigned char>(roundtrip[sizeof text])==0xa5&&!memcmp(roundtrip+1,text,sizeof text-1));
 const char *bad[]={"\xc0\x80","\xed\xa0\x80","\xf4\x90\x80\x80","\xe2\x82","\x80"};
 for(auto s:bad)CHECK(!simdutf::validate_utf8(s,strlen(s)));
 return true;
}
static bool guarded_unicode(){
 auto *p=static_cast<unsigned char *>(mmap(nullptr,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0));CHECK(p!=MAP_FAILED);
 CHECK(!mprotect(p+4096,4096,PROT_NONE));
 for(size_t n:{size_t(1),size_t(15),size_t(16),size_t(17),size_t(31),size_t(32),size_t(33),size_t(63),size_t(64),size_t(65),size_t(129)}){
  auto *s=reinterpret_cast<char *>(p+4096-n);memset(s,'a',n);CHECK(simdutf::validate_utf8(s,n));
  s[n-1]=static_cast<char>(0xff);CHECK(!simdutf::validate_utf8(s,n));
 }
 CHECK(!munmap(p,8192));return true;
}
static std::atomic<unsigned> failures;
static void worker(unsigned id){for(unsigned n=0;n<3;n++){if(!dates(id,3)||!engine_dates()||!text_services()||!unicode())++failures;std::this_thread::yield();}}
static std::atomic<bool> started;
static void live_worker(){started=true;for(;;)chos_sleep_ticks(1);}
extern "C" int main(int argc,char **argv){
 if(argc>1){
  if(!strcmp(argv[1],"deny-sleep")){
   if(sleep(1)!=1||errno!=EPERM)return 1;say("[ OK ] native denied sleep remains unslept\n");return 0;
  }
  bool immediate=!strcmp(argv[1],"immediate");
  if(std::atexit(immediate?forbidden_finalizer:expected_finalizer))return 1;
  auto *allocation=static_cast<volatile unsigned char *>(std::malloc(16384));if(!allocation)return 1;allocation[0]=7;
  std::thread abandoned(live_worker);while(!started)std::this_thread::yield();
  if(immediate)WTF::terminateProcess(71);else WTF::exitProcess(73);
 }
 WTF::initializeDates();
 if(!dates(0,1)||!engine_dates()||!text_services()||!unicode()||!guarded_unicode())return 1;
 for(unsigned cycle=0;cycle<3;cycle++){
  std::thread workers[3];for(unsigned i=0;i<3;i++)workers[i]=std::thread(worker,i);for(auto &t:workers)t.join();if(failures)return 1;
  chos_heap_stats_t stats;if(chos_libc_heap_info(&stats)||stats.allocations||stats.mapped_bytes){fail(__LINE__);return 1;}
 }
 struct timespec before,after;if(clock_gettime(CLOCK_MONOTONIC,&before)||sleep(1)||clock_gettime(CLOCK_MONOTONIC,&after))return 1;
 int64_t elapsed=(after.tv_sec-before.tv_sec)*INT64_C(1000000000)+after.tv_nsec-before.tv_nsec;
 if(elapsed<INT64_C(1000000000)||elapsed>INT64_C(5000000000)){fail(__LINE__);return 1;}
 if(sleep(601)!=601||errno!=EINVAL){fail(__LINE__);return 1;}
 for(auto mode:{"immediate","normal","deny-sleep"}){
  uint64_t caps=CHOS_PROCESS_CAP_ALL;if(!strcmp(mode,"deny-sleep"))caps&=~CHOS_PROCESS_CAP(CHOS_SYS_SLEEP);
  long pid=chos_spawn_limited("/BIN/WTFCAL.ELF",mode,caps);long expected=!strcmp(mode,"immediate")?71:!strcmp(mode,"normal")?73:0;
  if(pid<=0||chos_wait(static_cast<uint32_t>(pid))!=expected){fail(__LINE__);return 1;}
 }
 say("[ OK ] native UTC dates, real WTF date/UTF services and process helpers passed\n");return 0;
}

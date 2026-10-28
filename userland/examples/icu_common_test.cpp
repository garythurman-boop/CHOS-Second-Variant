#include "unicode/uchar.h"
#include "unicode/ucnv.h"
#include "unicode/ustring.h"
#include "unicode/unistr.h"
#include "unicode/uclean.h"
#include "unicode/uloc.h"
#include "unicode/uversion.h"
#include "unicode/unorm2.h"
#include "unicode/udata.h"
#include "unicode/putil.h"
#include "putilimp.h"
#include "umapfile.h"
#include "udatamem.h"
#include "chos/user.h"
#include "chos/libc.h"
#include <string.h>
#include <stdlib.h>
#include <locale.h>
#include <sys/time.h>
#include <thread>
#include <atomic>
#include "icu_fold_vectors.h"
static void say(const char *s){chos_write(s,strlen(s));}
static int fail(unsigned line){char n[11];n[10]='\n';for(unsigned i=0;i<10;i++){n[9-i]=char('0'+line%10);line/=10;}say("[FAIL] native ICU common line ");chos_write(n,11);return 1;}
#define CHECK(x) do{if(!(x))return fail(__LINE__);}while(0)
static int conversions(){
 const char utf8[]="Stra\xc3\x9f" "e \xce\xa3\xcf\x82 \xc4\xb0 \xe2\x84\xaa \xf0\x9f\x9a\x80";
 const char16_t wide[]=u"Stra\u00dfe \u03a3\u03c2 \u0130 \u212a \U0001f680";
 char16_t out[100];char bytes[150];int32_t n=0,length=0;UErrorCode status=U_ZERO_ERROR;
 u_strFromUTF8(out,100,&n,utf8,-1,&status);CHECK(U_SUCCESS(status)&&n==u_strlen(wide)&&!u_strcmp(out,wide));
 u_strToUTF8(bytes,sizeof(bytes),&length,out,n,&status);CHECK(U_SUCCESS(status)&&length==(int32_t)strlen(utf8)&&!strcmp(bytes,utf8));
 status=U_ZERO_ERROR;u_strFromUTF8(out,100,&n,"\xf0\x80\x80\x80",4,&status);CHECK(status==U_INVALID_CHAR_FOUND);
 status=U_ZERO_ERROR;u_strFromUTF8(out,1,&n,utf8,-1,&status);CHECK(status==U_BUFFER_OVERFLOW_ERROR&&n==u_strlen(wide));
 status=U_ZERO_ERROR;const char16_t invalid[]={0xd800,0};u_strToUTF8(bytes,sizeof(bytes),&length,invalid,1,&status);CHECK(status==U_INVALID_CHAR_FOUND);
 status=U_ZERO_ERROR;const char16_t expected[]=u"strasse \u03c3\u03c3 i\u0307 k \U0001f680";
 n=u_strFoldCase(out,100,wide,-1,U_FOLD_CASE_DEFAULT,&status);CHECK(U_SUCCESS(status)&&n==u_strlen(expected)&&!u_strcmp(out,expected));
 icu::UnicodeString text(wide);text.foldCase();CHECK(text==icu::UnicodeString(expected));
 icu::UnicodeString large(2048,char16_t(0x03bb),2048);CHECK(large.length()==2048);large.toUpper();CHECK(large.charAt(2000)==0x039b);
 CHECK(u_tolower(0x03a3)==0x03c3&&u_toupper(0xff)==0x178&&u_isalpha(0x03bb)&&u_charType(0x1f680)==U_OTHER_SYMBOL);
 for(const auto& vector:icu_fold_vectors){
  char16_t input[3]={},folded[8];int length=1;
  if(vector.scalar<0x10000)input[0]=char16_t(vector.scalar);
  else{input[0]=char16_t(0xd800+((vector.scalar-0x10000)>>10));input[1]=char16_t(0xdc00+((vector.scalar-0x10000)&1023));length=2;}
  status=U_ZERO_ERROR;int count=u_strFoldCase(folded,8,input,length,U_FOLD_CASE_DEFAULT,&status);
  CHECK(U_SUCCESS(status)&&count==(int)vector.length&&!u_memcmp(folded,icu_fold_units+vector.offset,count));
 }
 return 0;
}
static const char *file="/DOCUMENTS/CHOS-ICU-TEST.TMP";
static int files(){
 CHECK(chos_open(file)==-CHOS_ERRNO_ENOENT);
 unsigned char bytes[769];for(unsigned i=0;i<sizeof(bytes);i++)bytes[i]=(unsigned char)(i*17u+3u);
 CHECK(chos_write_file(file,bytes,sizeof(bytes))==(long)sizeof(bytes));
 for(unsigned i=0;i<5;i++){
  UDataMemory data;UErrorCode status=U_ZERO_ERROR;
  CHECK(uprv_mapFile(&data,file,&status)&&U_SUCCESS(status)&&data.length==sizeof(bytes)&&!memcmp(data.map,bytes,sizeof(bytes)));
  uprv_unmapFile(&data);CHECK(!data.map&&!data.mapAddr&&!data.pHeader);
 }
 CHECK(chos_write_file(file,"short",5)==5);UDataMemory data;UErrorCode status=U_ZERO_ERROR;
 CHECK(!uprv_mapFile(&data,file,&status)&&!data.map&&!data.pHeader);
 CHECK(chos_unlink(file)==0);status=U_ZERO_ERROR;CHECK(!uprv_mapFile(&data,file,&status));
 // This phase links the explicitly named upstream bootstrap data archive, not
 // a complete ICU data library. Missing resources must fail instead of passing.
 status=U_ZERO_ERROR;auto missing=udata_open("/DOCUMENTS","res","CHOS-MISSING",&status);CHECK(!missing&&U_FAILURE(status));
 return 0;
}
static std::atomic<unsigned> failures;
static void worker(){for(unsigned i=0;i<4;i++){if(conversions())++failures;std::this_thread::yield();}}
extern "C" int main(int argc,char **argv){
 if(argc>1&&!strcmp(argv[1],"no-clock")){
  CHECK(chos_clock_status(nullptr)==-CHOS_ERRNO_EPERM);double unavailable=uprv_getUTCtime();CHECK(unavailable!=unavailable);say("[ OK ] ICU rejects unavailable native clock with NaN\n");return 0;
 }
 CHECK(!environ[0]&&!getenv("LANG")&&!strcmp(setlocale(LC_ALL,nullptr),"C"));
 struct timeval wall;CHECK(!gettimeofday(&wall,nullptr)&&wall.tv_usec>=0&&wall.tv_usec<1000000);
 double millis=uprv_getUTCtime();CHECK(millis>=wall.tv_sec*1000.&&millis<wall.tv_sec*1000.+2000.);
 UVersionInfo version;u_getVersion(version);CHECK(version[0]==79&&version[1]==1);u_versionFromString(version,"79.1.2.3");CHECK(version[2]==2&&version[3]==3);
 CHECK(!strcmp(uloc_getDefault(),"en_US_POSIX")&&!strcmp(ucnv_getDefaultName(),"UTF-8"));
 CHECK(!conversions()&&!files());
 uint64_t mask=(uint64_t)chos_capabilities()&~CHOS_PROCESS_CAP(CHOS_SYS_CLOCK_INFO);
 long pid=chos_spawn_limited("/BIN/ICU.ELF","no-clock",mask);CHECK(pid>0&&chos_wait((uint32_t)pid)==0);
 for(unsigned cycle=0;cycle<3;cycle++){
  std::thread t[3];for(auto& thread:t)thread=std::thread(worker);for(auto& thread:t)thread.join();CHECK(!failures);
  u_cleanup();chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&!stats.allocations&&!stats.mapped_bytes);
 }
 say("[ OK ] native ICU common Unicode,files,threads and clock failures passed\n");return 0;
}

#include "unicode/unorm2.h"
#include "unicode/uidna.h"
#include "unicode/uchar.h"
#include "unicode/putil.h"
#include "unicode/uclean.h"
#include "unicode/ustring.h"
#include "chos/user.h"
#include "chos/libc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <atomic>
static void say(const char *s){chos_write(s,strlen(s));}
static uint32_t corpus_line;
static int fail(unsigned line){char out[120];snprintf(out,sizeof(out),"[FAIL] native ICU data line %u corpus line %u\n",line,corpus_line);say(out);return 1;}
#define CHECK(x) do{if(!(x))return fail(__LINE__);}while(0)
static int clean(){u_cleanup();chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&!stats.allocations&&!stats.mapped_bytes);return 0;}
static int resources(){
 // Absence must fail before switching to the explicitly supplied data path.
 u_setDataDirectory("/ICU-MISSING");UErrorCode status=U_ZERO_ERROR;
 CHECK(!unorm2_getNFKCInstance(&status)&&U_FAILURE(status));CHECK(!clean());
 u_setDataDirectory("/ICU");
 // Only malformed headers are tested here. This is not a claim that ICU is a
 // safe parser for arbitrary untrusted data payloads; distribution data is pinned.
 unsigned char header[32];FILE *f=fopen("/ICU/icudt79l/nfkc.nrm","rb");CHECK(f);
 CHECK(fread(header,1,sizeof(header),f)==sizeof(header)&&!fclose(f));
 for(unsigned mode=0;mode<4;mode++){
  unsigned char bad[32];memcpy(bad,header,sizeof(bad));size_t size=sizeof(bad);
  if(mode==0)size=5;
  if(mode==1)bad[2]=0;
  if(mode==2)bad[8]=1;
  if(mode==3)bad[16]=255;
  CHECK(chos_write_file("/ICU/icudt79l/CHOS-BAD.nrm",bad,size)==(long)size);
  status=U_ZERO_ERROR;CHECK(!unorm2_getInstance(nullptr,"CHOS-BAD",UNORM2_COMPOSE,&status)&&U_FAILURE(status));
 }
 CHECK(!chos_unlink("/ICU/icudt79l/CHOS-BAD.nrm"));CHECK(!clean());u_setDataDirectory("/ICU");return 0;
}
static int read_exact(FILE *f,void *out,size_t bytes){return fread(out,1,bytes,f)==bytes?0:1;}
static int normalization(){
 FILE *f=fopen("/ICU/NORM.BIN","rb");CHECK(f);char magic[8];uint32_t count;
 CHECK(!read_exact(f,magic,8)&&!memcmp(magic,"CHOSN18\0",8)&&!read_exact(f,&count,4)&&count==20171);
 // The fixed corpus membership bitmap belongs to this test's bounded BSS.
 static unsigned char excluded[0x110000/8];CHECK(!read_exact(f,excluded,sizeof(excluded)));
 UErrorCode status=U_ZERO_ERROR;const UNormalizer2 *forms[]={unorm2_getNFCInstance(&status),unorm2_getNFDInstance(&status),unorm2_getNFKCInstance(&status),unorm2_getNFKDInstance(&status)};
 CHECK(U_SUCCESS(status)&&forms[0]&&forms[1]&&forms[2]&&forms[3]);
 for(uint32_t row=0;row<count;row++){
  uint16_t lengths[5];char16_t input[5][128],out[256];
  CHECK(!read_exact(f,&corpus_line,4)&&!read_exact(f,lengths,sizeof(lengths)));
  for(unsigned c=0;c<5;c++){CHECK(lengths[c]<=128&&!read_exact(f,input[c],lengths[c]*2));}
  for(unsigned form=0;form<4;form++)for(unsigned c=0;c<5;c++){
   unsigned expected=form==0?(c<3?1:3):form==1?(c<3?2:4):form==2?3:4;
   status=U_ZERO_ERROR;int n=unorm2_normalize(forms[form],input[c],lengths[c],out,256,&status);
   CHECK(U_SUCCESS(status)&&n==lengths[expected]&&!memcmp(out,input[expected],n*2));
  }
 }
 CHECK(fgetc(f)==EOF&&!ferror(f)&&!fclose(f));corpus_line=0;
 unsigned identities=0;
 for(uint32_t cp=0;cp<0x110000;cp++){
  if((cp>=0xd800&&cp<=0xdfff)||(excluded[cp/8]&(1u<<(cp%8)))||u_charType(cp)==U_UNASSIGNED)continue;
  char16_t input[2],out[32];int length;
  if(cp<0x10000){input[0]=char16_t(cp);length=1;}
  else{input[0]=char16_t(0xd800+((cp-0x10000)>>10));input[1]=char16_t(0xdc00+((cp-0x10000)&1023));length=2;}
  for(auto *form:forms){status=U_ZERO_ERROR;int n=unorm2_normalize(form,input,length,out,32,&status);if(U_FAILURE(status)||n!=length||memcmp(out,input,n*2)){corpus_line=cp;return fail(__LINE__);}}
  identities++;
 }
 char report[140];snprintf(report,sizeof(report),"[ OK ] Unicode18 normalization: %u rows, %u additional assigned scalar identities\n",count,identities);say(report);return 0;
}
static int idna(){
 UErrorCode status=U_ZERO_ERROR;uint32_t options=UIDNA_USE_STD3_RULES|UIDNA_CHECK_BIDI|UIDNA_CHECK_CONTEXTJ|UIDNA_CHECK_CONTEXTO;
 UIDNA *trans=uidna_openUTS46(options,&status),*nontrans=uidna_openUTS46(options|UIDNA_NONTRANSITIONAL_TO_ASCII|UIDNA_NONTRANSITIONAL_TO_UNICODE,&status);
 CHECK(trans&&nontrans&&U_SUCCESS(status));
 FILE *f=fopen("/ICU/IDNA.BIN","rb");CHECK(f);char magic[8];uint32_t count;
 CHECK(!read_exact(f,magic,8)&&!memcmp(magic,"CHOSI18\0",8)&&!read_exact(f,&count,4)&&count==6396);
 char *utf8=(char *)malloc(3*4096);CHECK(utf8);unsigned utf8_rows=0,ill_formed_rows=0;
 for(uint32_t row=0;row<count;row++){
  uint16_t lengths[4];unsigned char flags[4];char16_t input[4][1024],out[2048];
  CHECK(!read_exact(f,&corpus_line,4)&&!read_exact(f,lengths,sizeof(lengths))&&!read_exact(f,flags,sizeof(flags)));
  for(unsigned c=0;c<4;c++){CHECK(lengths[c]<=1024&&!read_exact(f,input[c],lengths[c]*2));}
  int32_t utf8_length=0;status=U_ZERO_ERROR;u_strToUTF8(utf8,4096,&utf8_length,input[0],lengths[0],&status);
  bool scalar_input=U_SUCCESS(status);CHECK(scalar_input||status==U_INVALID_CHAR_FOUND);
  if(scalar_input)utf8_rows++;else ill_formed_rows++;
  for(unsigned operation=0;operation<3;operation++){
   UIDNAInfo info=UIDNA_INFO_INITIALIZER;status=U_ZERO_ERROR;
   int n=operation==0?uidna_nameToUnicode(nontrans,input[0],lengths[0],out,2048,&info,&status):uidna_nameToASCII(operation==1?nontrans:trans,input[0],lengths[0],out,2048,&info,&status);
   CHECK(U_SUCCESS(status)&&n>=0&&n<2048);
   bool expected_error=flags[operation]!=0;
   // Same explicitly documented ICU-22882 exception as the upstream harness.
   if(operation&&flags[operation]==2&&!info.errors&&n>0&&out[n-1]==u'.'){
    bool doubled=false;for(int i=1;i<n;i++)if(out[i-1]==u'.'&&out[i]==u'.')doubled=true;
    if(!doubled)expected_error=false;
   }
   CHECK(expected_error==(info.errors!=0));
   if(!expected_error)CHECK(n==lengths[operation+1]&&!memcmp(out,input[operation+1],n*2));
   if(scalar_input){
    // In addition to the independent conformance oracle above, compare the
    // two real encodings' API behavior, including error bits and error output.
    int32_t expected_length=0;status=U_ZERO_ERROR;u_strToUTF8(utf8+8192,4096,&expected_length,out,n,&status);CHECK(U_SUCCESS(status));
    UIDNAInfo bytes_info=UIDNA_INFO_INITIALIZER;status=U_ZERO_ERROR;
    int bytes=operation==0?uidna_nameToUnicodeUTF8(nontrans,utf8,utf8_length,utf8+4096,4096,&bytes_info,&status):uidna_nameToASCII_UTF8(operation==1?nontrans:trans,utf8,utf8_length,utf8+4096,4096,&bytes_info,&status);
    CHECK(U_SUCCESS(status)&&bytes==expected_length&&bytes_info.errors==info.errors&&bytes_info.isTransitionalDifferent==info.isTransitionalDifferent&&!memcmp(utf8+4096,utf8+8192,bytes));
   }
  }
 }
 CHECK(fgetc(f)==EOF&&!ferror(f)&&!fclose(f));uidna_close(trans);uidna_close(nontrans);free(utf8);corpus_line=0;
 CHECK(utf8_rows==6394&&ill_formed_rows==2);
 say("[ OK ] Unicode18 UTS46: 6396 rows, 19188 UTF16 operations, 19182 UTF8 equivalents; 2 ill-formed UTF16 inputs\n");return 0;
}
static int selected(){
 UErrorCode status=U_ZERO_ERROR;auto *nfkc=unorm2_getNFKCInstance(&status);auto *fold=unorm2_getNFKCCasefoldInstance(&status);auto *simple=unorm2_getNFKCSimpleCasefoldInstance(&status);CHECK(nfkc&&fold&&simple&&U_SUCCESS(status));
 char16_t out[64];const char16_t input[]=u"\uff21\u00df\u00ad\u212a";
 int n=unorm2_normalize(fold,input,-1,out,64,&status);CHECK(U_SUCCESS(status)&&n==4&&!memcmp(out,u"assk",8));
 n=unorm2_normalize(simple,input,-1,out,64,&status);CHECK(U_SUCCESS(status)&&n==3&&!memcmp(out,u"a\u00dfk",6));
 n=unorm2_normalize(nfkc,u"\ufb03\u2460\uff21",-1,out,64,&status);CHECK(U_SUCCESS(status)&&n==5&&!memcmp(out,u"ffi1A",10));
 UIDNA *idna=uidna_openUTS46(UIDNA_NONTRANSITIONAL_TO_ASCII|UIDNA_CHECK_BIDI|UIDNA_CHECK_CONTEXTJ,&status);CHECK(idna&&U_SUCCESS(status));
 char bytes[64];UIDNAInfo info=UIDNA_INFO_INITIALIZER;n=uidna_nameToASCII_UTF8(idna,"b\xc3\xbc""cher.example",-1,bytes,64,&info,&status);
 CHECK(U_SUCCESS(status)&&!info.errors&&n==21&&!memcmp(bytes,"xn--bcher-kva.example",21));
 for(const char *invalid:{"\xc0\xaf","\xed\xa0\x80","\xf4\x90\x80\x80","\xff","\xe2\x82"}){
  status=U_ZERO_ERROR;info=UIDNA_INFO_INITIALIZER;n=uidna_nameToASCII_UTF8(idna,invalid,-1,bytes,64,&info,&status);
  CHECK(U_SUCCESS(status)&&n>0&&(info.errors&UIDNA_ERROR_DISALLOWED));
 }
 uidna_close(idna);return 0;
}
static std::atomic<unsigned> failures;
static void worker(){for(unsigned i=0;i<16;i++){if(selected())++failures;std::this_thread::yield();}}
extern "C" int main(int argc,char **argv){
 if(argc>1&&!strcmp(argv[1],"no-read")){
  u_setDataDirectory("/ICU");UErrorCode status=U_ZERO_ERROR;
  CHECK(chos_open("/ICU/icudt79l/nfkc.nrm")==-CHOS_ERRNO_EPERM);
  CHECK(!unorm2_getNFKCInstance(&status)&&U_FAILURE(status));CHECK(!clean());
  say("[ OK ] native ICU external data respects file-read permission\n");return 0;
 }
 uint64_t mask=(uint64_t)chos_capabilities()&~(CHOS_PROCESS_CAP(CHOS_SYS_OPEN)|CHOS_PROCESS_CAP(CHOS_SYS_READ));
 long pid=chos_spawn_limited("/BIN/ICUDATA.ELF","no-read",mask);CHECK(pid>0&&chos_wait((uint32_t)pid)==0);
 CHECK(!resources()&&!normalization()&&!idna());
 for(unsigned cycle=0;cycle<3;cycle++){
  std::thread t[3];for(auto& thread:t)thread=std::thread(worker);for(auto& thread:t)thread.join();CHECK(!failures);
  CHECK(!clean());u_setDataDirectory("/ICU");
 }
 CHECK(!clean());say("[ OK ] native ICU external normalization,IDNA and cleanup passed\n");return 0;
}

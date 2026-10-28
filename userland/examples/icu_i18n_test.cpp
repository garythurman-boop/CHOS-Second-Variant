#include "unicode/regex.h"
#include "unicode/plurrule.h"
#include "unicode/choicfmt.h"
#include "unicode/selfmt.h"
#include "unicode/unorm2.h"
#include "unicode/uclean.h"
#include "unicode/udata.h"
#include "chos/user.h"
#include "chos/libc.h"
#include <string.h>
#include <thread>
#include <atomic>
static void say(const char *s){chos_write(s,strlen(s));}
static int fail(unsigned line){char n[11];n[10]='\n';for(unsigned i=0;i<10;i++){n[9-i]=char('0'+line%10);line/=10;}say("[FAIL] native ICU i18n line ");chos_write(n,11);return 1;}
#define CHECK(x) do{if(!(x))return fail(__LINE__);}while(0)
static int regex(){
 UErrorCode status=U_ZERO_ERROR;UParseError parse{};
 auto *pattern=icu::RegexPattern::compile(icu::UnicodeString(u"(?i)([a-z]+)-(\\d+)"),parse,status);CHECK(pattern&&U_SUCCESS(status));
 icu::UnicodeString input(u"xx ABc-123 zz DEF-45");auto *matcher=pattern->matcher(input,status);CHECK(matcher&&U_SUCCESS(status));
 matcher->setTimeLimit(10,status);matcher->setStackLimit(16384,status);CHECK(U_SUCCESS(status));
 CHECK(matcher->find(status)&&matcher->group(1,status)==u"ABc"&&matcher->group(2,status)==u"123");
 CHECK(matcher->find(status)&&matcher->group(1,status)==u"DEF"&&matcher->group(2,status)==u"45");CHECK(!matcher->find(status)&&U_SUCCESS(status));
 delete matcher;delete pattern;
 status=U_ZERO_ERROR;pattern=icu::RegexPattern::compile(icu::UnicodeString(u"\\p{L}+"),parse,status);CHECK(pattern&&U_SUCCESS(status));
 icu::UnicodeString unicode(u"123 \u03bb\u03bf\u03b3\u03bf\u03c2 \u4e2d\u6587");matcher=pattern->matcher(unicode,status);CHECK(matcher&&U_SUCCESS(status));
 CHECK(matcher->find(status)&&matcher->group(status)==u"\u03bb\u03bf\u03b3\u03bf\u03c2");CHECK(matcher->find(status)&&matcher->group(status)==u"\u4e2d\u6587");delete matcher;delete pattern;
 status=U_ZERO_ERROR;pattern=icu::RegexPattern::compile(icu::UnicodeString(u"("),parse,status);CHECK(!pattern&&U_FAILURE(status));
 return 0;
}
static int formats(){
 UErrorCode status=U_ZERO_ERROR;auto *rules=icu::PluralRules::createRules(icu::UnicodeString(u"one: n is 1; few: n in 2..4"),status);CHECK(rules&&U_SUCCESS(status));
 for(int n=0;n<100;n++){icu::UnicodeString expected(n==1?u"one":n>=2&&n<=4?u"few":u"other");CHECK(rules->select(n)==expected);}
 CHECK(rules->select(0.5)==u"other");delete rules;
 status=U_ZERO_ERROR;rules=icu::PluralRules::createRules(icu::UnicodeString(u"one: n invalid 1"),status);CHECK(!rules&&U_FAILURE(status));
 status=U_ZERO_ERROR;icu::SelectFormat select(icu::UnicodeString(u"female {She} male {He} other {They}"),status);CHECK(U_SUCCESS(status));
 icu::UnicodeString result;icu::FieldPosition position(0);select.format(icu::UnicodeString(u"female"),result,position,status);CHECK(U_SUCCESS(status)&&result==u"She");
 result.remove();select.format(icu::UnicodeString(u"unknown"),result,position,status);CHECK(U_SUCCESS(status)&&result==u"They");
 icu::ChoiceFormat choice(icu::UnicodeString(u"-1#negative|0#zero|1#one|1<many"),status);CHECK(U_SUCCESS(status));icu::UnicodeString pattern;choice.toPattern(pattern);icu::ChoiceFormat copy(pattern,status);CHECK(U_SUCCESS(status));
 for(double n: {-2.,-1.,0.,0.5,1.,1.0001,2.,100.}){result.remove();icu::UnicodeString other;choice.format(n,result,position);copy.format(n,other,position);CHECK(result==other);}
 result.remove();choice.format(2.,result,position);CHECK(result==u"many");return 0;
}
static int normalization(){
 UErrorCode status=U_ZERO_ERROR;auto *nfc=unorm2_getNFCInstance(&status);CHECK(nfc&&U_SUCCESS(status));
 char16_t out[40];const char16_t decomposed[]=u"A\u030a e\u0301 \u1100\u1161";const char16_t composed[]=u"\u00c5 \u00e9 \uac00";
 int n=unorm2_normalize(nfc,decomposed,-1,out,40,&status);CHECK(U_SUCCESS(status)&&n==5&&!memcmp(out,composed,5*sizeof(char16_t)));
 auto *nfd=unorm2_getNFDInstance(&status);CHECK(nfd&&U_SUCCESS(status));n=unorm2_normalize(nfd,composed,-1,out,40,&status);CHECK(U_SUCCESS(status)&&n==8&&!memcmp(out,decomposed,8*sizeof(char16_t)));
 return 0;
}
static int limits(){
 UErrorCode status=U_ZERO_ERROR;UParseError parse{};auto *pattern=icu::RegexPattern::compile(icu::UnicodeString(u"(a+)+$"),parse,status);CHECK(pattern&&U_SUCCESS(status));
 icu::UnicodeString input(u"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab");auto *matcher=pattern->matcher(input,status);CHECK(matcher&&U_SUCCESS(status));
 matcher->setTimeLimit(1,status);matcher->setStackLimit(16384,status);CHECK(U_SUCCESS(status));CHECK(!matcher->matches(status)&&status==U_REGEX_TIME_OUT);
 delete matcher;delete pattern;
 // A complete external data package has not been supplied. Missing resources
 // must fail; the official bootstrap archive is not qualified as full data.
 status=U_ZERO_ERROR;auto *missing=udata_open("/DOCUMENTS","res","CHOS-NOT-PRESENT",&status);CHECK(!missing&&U_FAILURE(status));return 0;
}
static std::atomic<unsigned> failures;
static void worker(){for(unsigned i=0;i<4;i++){if(regex()||formats()||normalization())++failures;std::this_thread::yield();}}
extern "C" int main(){
 CHECK(!regex()&&!formats()&&!normalization()&&!limits());
 for(unsigned cycle=0;cycle<3;cycle++){
  std::thread threads[3];for(auto& t:threads)t=std::thread(worker);for(auto& t:threads)t.join();CHECK(!failures);
  u_cleanup();chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&!stats.allocations&&!stats.mapped_bytes);
 }
 say("[ OK ] native ICU i18n rules,regex,normalization and cleanup passed\n");return 0;
}

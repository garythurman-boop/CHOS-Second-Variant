#include <string>
#include <string_view>
#include <vector>
#include <thread>
#include <atomic>
#include <cwchar>
#include <cwctype>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include "chos/user.h"
#include "chos/libc.h"
static void say(const char *s){chos_write(s,std::strlen(s));}
static int fail(unsigned line){char n[11];n[10]='\n';for(unsigned i=0;i<10;i++){n[9-i]=char('0'+line%10);line/=10;}say("[FAIL] native wide line ");chos_write(n,11);return 1;}
#define CHECK(x) do{if(!(x))return fail(__LINE__);}while(0)
static int strings(){
 wchar_t buffer[32];CHECK(std::wcscpy(buffer,L"wide")==buffer);CHECK(std::wcscat(buffer,L" strings")==buffer);CHECK(std::wcslen(buffer)==12);
 CHECK(std::wcsstr(buffer,L"strings")==buffer+5&&std::wcschr(buffer,L' ')==buffer+4);
 std::wmemmove(buffer+1,buffer,12);CHECK(buffer[1]==L'w'&&buffer[12]==L's');
 std::wmemset(buffer,L'X',4);CHECK(std::wmemchr(buffer,L'X',4)==buffer&&std::wmemcmp(buffer,L"XXXX",4)==0);
 std::wstring text=L"CHOS \u20ac \U0001f680";CHECK(text.size()==8&&text.find(L'\U0001f680')==7);text+=L" text";text.erase(0,5);CHECK(text==L"\u20ac \U0001f680 text");
 std::wstring large(2000,L'\u03bb');large.replace(0,4,L"wide");CHECK(large.size()==2000&&large.substr(0,4)==L"wide"&&large.back()==L'\u03bb');
 std::vector<std::wstring> words{large,text};auto moved=std::move(words[0]);CHECK(moved.size()==2000&&words[1]==text);
 std::wstring_view view=moved;CHECK(view.substr(0,4)==L"wide");
 CHECK(std::iswalpha(L'A')&&!std::iswalpha(L'\u03bb')&&std::towlower(L'A')==L'a'&&std::towlower(WEOF)==WEOF);
 CHECK(std::iswctype(L'9',std::wctype("digit"))&&std::towctrans(L'z',std::wctrans("toupper"))==L'Z');
 return 0;
}
static int scalars(){
 for(uint32_t c=0;c<=0x10ffff;c+=997){
  if(c>=0xd800&&c<=0xdfff)continue;char bytes[4];std::mbstate_t state{};wchar_t w{};
  size_t n=std::wcrtomb(bytes,wchar_t(c),&state);CHECK(n==(c<128?1u:c<2048?2u:c<65536?3u:4u));
  CHECK(std::mbrtowc(&w,bytes,n,&state)==(c?n:0)&&w==wchar_t(c)&&std::mbsinit(&state));
  for(size_t i=0;i<n;i++)CHECK(std::mbrtowc(&w,bytes+i,1,&state)==(i+1<n?size_t(-2):c?1u:0u));
  CHECK(w==wchar_t(c)&&std::mbsinit(&state));
 }
 char bytes[4];std::mbstate_t state{};errno=0;CHECK(std::wcrtomb(bytes,wchar_t(0xd800),&state)==size_t(-1)&&errno==EILSEQ);
 const char *bad="\xe2";wchar_t out[8];state={};CHECK(std::mbsrtowcs(out,&bad,8,&state)==size_t(-1)&&errno==EILSEQ);
 return 0;
}
static int bulk(){
 const char text[]="A\xe2\x82\xac\xf0\x9f\x9a\x80z";const wchar_t wide[]=L"A\u20ac\U0001f680z";
 for(unsigned cap=0;cap<=10;cap++){
  std::mbstate_t state{};const wchar_t *w=wide;char bytes[16];std::memset(bytes,0x55,sizeof(bytes));
  size_t n=std::wcsrtombs(bytes,&w,cap,&state);CHECK(n<=cap&&!std::memcmp(bytes,text,n)&&std::mbsinit(&state));
  CHECK(cap<10?w!=nullptr:w==nullptr); // Includes a terminating NUL only if it fits.
  wchar_t out[8];const char *s=text;state={};n=std::mbsrtowcs(out,&s,cap,&state);CHECK(n==(cap<4?cap:4)&&!std::wmemcmp(out,wide,n));CHECK(cap<5?s!=nullptr:s==nullptr);
 }
 std::mbstate_t state{};const char *p=text;CHECK(std::mbsrtowcs(nullptr,&p,0,&state)==4&&p==text);
 const wchar_t *w=wide;CHECK(std::wcsrtombs(nullptr,&w,0,&state)==9&&w==wide);
 CHECK(std::mbstowcs(nullptr,text,0)==4&&std::wcstombs(nullptr,wide,0)==9);
 return 0;
}
static std::atomic<unsigned> failures{};
static const char *const encodings[]={"\xc2\xa2","\xe2\x82\xac","\xf0\x9f\x9a\x80"};
static const wchar_t codepoints[]={0xa2,0x20ac,0x1f680};
static int state_checks(unsigned index){
 const char *s=encodings[index];size_t n=index+2;wchar_t result{};char bytes[4];
 errno=31+int(index);
 CHECK(std::mbrtowc(&result,s,1,nullptr)==size_t(-2));
 CHECK(std::mbrlen("\xc2",1,nullptr)==size_t(-2));
 char32_t c32{};CHECK(mbrtoc32(&c32,"X",1,nullptr)==1&&c32=='X');
 char16_t c16{};CHECK(mbrtoc16(&c16,"Z",1,nullptr)==1&&c16=='Z');
 CHECK(std::wcrtomb(bytes,codepoints[index],nullptr)==n&&!std::memcmp(bytes,s,n));
 wchar_t decoded[3];const char *source=s;CHECK(std::mbsrtowcs(decoded,&source,3,nullptr)==1&&!source&&decoded[0]==codepoints[index]);
 const wchar_t *wide=decoded;char encoded[8];CHECK(std::wcsrtombs(encoded,&wide,8,nullptr)==n&&!wide&&!std::memcmp(encoded,s,n));
 chos_yield();chos_sleep_ticks(1);
 CHECK(std::mbrlen("\xa2",1,nullptr)==1);
 CHECK(std::mbrtowc(&result,s+1,n-1,nullptr)==n-1&&result==codepoints[index]);
 CHECK(errno==31+int(index));
 return 0;
}
static void worker(unsigned index){for(unsigned cycle=0;cycle<16;cycle++){if(state_checks(index)||strings())++failures;std::this_thread::yield();}}
extern "C" int main(){
 CHECK(!strings()&&!scalars()&&!bulk());
 wchar_t parent{};char32_t scalar{};CHECK(std::mbrtowc(&parent,"\xe2",1,nullptr)==size_t(-2));CHECK(mbrtoc32(&scalar,"\xf0",1,nullptr)==size_t(-2));
 for(unsigned cycle=0;cycle<3;cycle++){
  std::thread threads[3];for(unsigned i=0;i<3;i++)threads[i]=std::thread(worker,i);for(auto& t:threads)t.join();CHECK(!failures);
  chos_heap_stats_t stats;CHECK(!chos_libc_heap_info(&stats)&&!stats.allocations&&!stats.mapped_bytes);
 }
 CHECK(std::mbrtowc(&parent,"\x82\xac",2,nullptr)==2&&parent==0x20ac);CHECK(mbrtoc32(&scalar,"\x9f\x9a\x80",3,nullptr)==3&&scalar==0x1f680);
 say("[ OK ] native wide strings,UTF8 limits and isolated conversion states passed\n");return 0;
}

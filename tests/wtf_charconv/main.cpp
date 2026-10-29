#include "config.h"
#include <wtf/StdLibExtras.h>
#include <chos/user.h>
#include <chos/libc.h>
#include <charconv>
#include <cfloat>
#include <bit>
#include <limits>
#include <thread>
#include <atomic>
#include <span>
#include <cstring>
#include <cstdlib>
#include <stdio.h>
#include <errno.h>
#include <fenv.h>
#include "charconv_vectors.h"
#if !HAVE(MEMMEM)
#error The guest must exercise the actual WTF memmem path
#endif
static bool fail(unsigned line){printf("[FAIL] native numeric services line %u\n",line);return false;}
#define CHECK(v) do{if(!(v))return fail(__LINE__);}while(0)
static constexpr bool constexpr_conversion(){
 char b[32]{};auto out=std::to_chars(b,b+32,-2147483647);int value=0;
 auto in=std::from_chars(b,out.ptr,value);return out.ec==std::errc{}&&in.ec==std::errc{}&&in.ptr==out.ptr&&value==-2147483647;
}
static_assert(constexpr_conversion());
static_assert(FLT_MANT_DIG==24&&DBL_MANT_DIG==53&&LDBL_MANT_DIG==113);
static uint64_t next(uint64_t &state){state^=state<<13;state^=state>>7;state^=state<<17;return state;}
template<class T> static bool integer(T value,int base){
 unsigned char b[140];std::memset(b,0xa5,sizeof b);char *first=reinterpret_cast<char *>(b+1);
 errno=EACCES;auto out=std::to_chars(first,first+130,value,base);CHECK(out.ec==std::errc{}&&errno==EACCES);
 T parsed=0;auto in=std::from_chars(first,out.ptr,parsed,base);CHECK(in.ec==std::errc{}&&in.ptr==out.ptr&&parsed==value&&errno==EACCES);
 CHECK(b[0]==0xa5&&b[131]==0xa5);size_t length=static_cast<size_t>(out.ptr-first);
 // Independent repeated division oracle, including the signed minimum.
 char expected[140];size_t n=0;using U=std::make_unsigned_t<T>;
 U magnitude=static_cast<U>(value);bool negative=std::is_signed_v<T>&&(value<0);
 if(negative)magnitude=U(0)-magnitude;
 do{unsigned digit=static_cast<unsigned>(magnitude%base);expected[n++]=char(digit<10?'0'+digit:'a'+digit-10);magnitude/=base;}while(magnitude);
 if(negative)expected[n++]='-';CHECK(n==length);
 for(size_t i=0;i<n;i++)CHECK(expected[n-1-i]==first[i]);
 std::memset(b,0xa5,sizeof b);out=std::to_chars(first,first+length-1,value,base);
 CHECK(out.ec==std::errc::value_too_large&&out.ptr==first+length-1&&b[0]==0xa5&&b[length]==0xa5);return true;
}
static bool integers(uint64_t seed){
 for(int base=2;base<=36;base++){
  CHECK(integer(INT64_MIN,base)&&integer(INT64_MAX,base)&&integer(UINT64_MAX,base)&&integer(uint64_t(0),base));
  CHECK(integer(INT32_MIN,base)&&integer(INT32_MAX,base)&&integer(UINT32_MAX,base));
  CHECK(integer(int16_t(INT16_MIN),base)&&integer(uint16_t(UINT16_MAX),base));
  CHECK(integer(int8_t(INT8_MIN),base)&&integer(uint8_t(UINT8_MAX),base));
 }
 for(unsigned n=0;n<500;n++){uint64_t bits=next(seed);int base=2+int(n%35);CHECK(integer(bits,base)&&integer(std::bit_cast<int64_t>(bits),base));}
 const char *bad[]={"", " 1", "+1", "-", "x"};
 for(auto s:bad){int value=17;auto r=std::from_chars(s,s+std::strlen(s),value);CHECK(r.ec==std::errc::invalid_argument&&r.ptr==s&&value==17);}
 for(auto s:{"9223372036854775808!","-9223372036854775809!"}){
  int64_t value=17;auto r=std::from_chars(s,s+std::strlen(s),value);CHECK(r.ec==std::errc::result_out_of_range&&*r.ptr=='!'&&value==17);
 }
 const char *s="18446744073709551616!";uint64_t value=17;auto r=std::from_chars(s,s+std::strlen(s),value);
 CHECK(r.ec==std::errc::result_out_of_range&&*r.ptr=='!'&&value==17);
 s="0x12";r=std::from_chars(s,s+4,value,16);CHECK(r.ec==std::errc{}&&r.ptr==s+1&&value==0);
 s="-1";value=17;r=std::from_chars(s,s+2,value);CHECK(r.ec==std::errc::invalid_argument&&r.ptr==s&&value==17);return true;
}
static bool vectors(unsigned begin,unsigned stride){
 for(size_t i=begin;i<std::size(charconv_vectors);i+=stride){auto &v=charconv_vectors[i];size_t length=std::strlen(v.expected);
  auto format=v.kind=='f'?std::chars_format::fixed:v.kind=='e'?std::chars_format::scientific:std::chars_format::general;
  for(size_t capacity:{size_t(0),length-1,length,length+1,size_t(512)}){
   unsigned char buffer[768];std::memset(buffer,0xa5,sizeof buffer);char *first=reinterpret_cast<char *>(buffer+1);
   std::to_chars_result r;errno=EACCES;
   if(v.width==32){uint32_t bits=static_cast<uint32_t>(v.bits);float value=std::bit_cast<float>(bits);r=std::to_chars(first,first+capacity,value,format,v.precision);}
   else r=std::to_chars(first,first+capacity,std::bit_cast<double>(v.bits),format,v.precision);
   CHECK(errno==EACCES&&buffer[0]==0xa5&&buffer[capacity+1]==0xa5);
   if(capacity<length)CHECK(r.ec==std::errc::value_too_large&&r.ptr==first+capacity);
   else if(r.ec!=std::errc{}||r.ptr!=first+length||std::memcmp(first,v.expected,length)){
    printf("[FAIL] charconv vector=%zu kind=%c precision=%d expected=%s\n",i,v.kind,v.precision,v.expected);return false;
   }
  }
 }return true;
}
static bool shortest(uint64_t seed){
 for(unsigned n=0;n<1500;n++){
  uint64_t bits=next(seed);if(((bits>>52)&2047)==2047)continue;
  double value=std::bit_cast<double>(bits);char b[128];auto r=std::to_chars(b,b+sizeof b-1,value);CHECK(r.ec==std::errc{});*r.ptr=0;
  char *end;double parsed=std::strtod(b,&end);CHECK(end==r.ptr&&std::bit_cast<uint64_t>(parsed)==bits);
  uint32_t small=static_cast<uint32_t>(bits);if(((small>>23)&255)==255)continue;
  float f=std::bit_cast<float>(small);r=std::to_chars(b,b+sizeof b-1,f);CHECK(r.ec==std::errc{});*r.ptr=0;
  float p=std::strtof(b,&end);CHECK(end==r.ptr&&std::bit_cast<uint32_t>(p)==small);
 }return true;
}
static bool rounding(){
 int previous=fegetround();CHECK(previous>=0);
 for(int mode:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}){
  CHECK(!fesetround(mode));char b[32];auto r=std::to_chars(b,b+32,1.25,std::chars_format::fixed,1);
  CHECK(r.ec==std::errc{}&&r.ptr==b+3&&!std::memcmp(b,"1.2",3));
  r=std::to_chars(b,b+32,2.5f,std::chars_format::fixed,0);CHECK(r.ec==std::errc{}&&r.ptr==b+1&&*b=='2'&&fegetround()==mode);
 }CHECK(!fesetround(previous));return true;
}
static bool searches(uint64_t seed){
 unsigned char h[4096],needle[257];
 for(unsigned iteration=0;iteration<1000;iteration++){
  size_t size=next(seed)%sizeof h,count=next(seed)%sizeof needle;
  for(size_t i=0;i<size;i++)h[i]=static_cast<unsigned char>(next(seed)%7);
  for(size_t i=0;i<count;i++)needle[i]=static_cast<unsigned char>(next(seed)%7);
  if(count&&count<=size&&iteration%2)std::memcpy(h+next(seed)%(size-count+1),needle,count);
  size_t expected=WTF::notFound;
  if(count<=size)for(size_t i=0;i<=size-count;i++){size_t j=0;while(j<count&&h[i+j]==needle[j])j++;if(j==count){expected=i;break;}}
  auto *p=static_cast<unsigned char *>(memmem(h,size,needle,count));CHECK((p?static_cast<size_t>(p-h):WTF::notFound)==expected);
  CHECK(WTF::find(std::span(h,size),std::span(needle,count))==expected);
 }return true;
}
static std::atomic<unsigned> failures;
static void worker(unsigned id){
 int saved_error=1000+static_cast<int>(id);errno=saved_error;
 const int modes[]={FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
 if(fesetround(modes[id])){++failures;return;}
 std::this_thread::yield();
 if(errno!=saved_error||fegetround()!=modes[id]||fesetround(FE_TONEAREST)){++failures;return;}
 if(!integers(0x987654321+id)||!vectors(id,3)||!shortest(0x13579+id)||!rounding()||!searches(0x24680+id))++failures;
}
extern "C" int main(){
 if(!integers(0x123456789)||!vectors(0,1)||!shortest(0xabcdef)||!rounding()||!searches(0x123456))return 1;
 for(unsigned cycle=0;cycle<3;cycle++){
  std::thread threads[3];for(unsigned i=0;i<3;i++)threads[i]=std::thread(worker,i);for(auto &t:threads)t.join();
  if(failures)return 1;chos_heap_stats_t stats;if(chos_libc_heap_info(&stats)||stats.allocations||stats.mapped_bytes){fail(__LINE__);return 1;}
 }
 puts("[ OK ] original charconv, Decimal corpus and WTF binary search passed");return 0;
}

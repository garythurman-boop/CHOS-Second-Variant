#include <uchar.h>
#include <errno.h>
#ifdef CHOS_HOST_TEST
static _Thread_local mbstate_t implicit[4];
static mbstate_t *implicit_state(unsigned index){return &implicit[index];}
#else
#include "chos/tls_runtime.h"
static mbstate_t *implicit_state(unsigned index){return &__chos_libc_tls()->unicode_states[index];}
#endif
int mbsinit(const mbstate_t *s){return !s||!(s->scalar|s->minimum|s->remaining|s->pending);}
static size_t invalid(mbstate_t *s){*s=(mbstate_t){0};errno=EILSEQ;return (size_t)-1;}
static size_t decode(char32_t *out,const char *src,size_t n,mbstate_t *s){
 if(!src){src="";n=1;out=0;}
 if(s->pending||s->remaining>3)return invalid(s);
 if(!n)return (size_t)-2;
 size_t used=0;
 if(!s->remaining){
  unsigned c=(unsigned char)src[used++];
  if(c<0x80){*s=(mbstate_t){0};if(out)*out=c;return c?1:0;}
  if(c>=0xc2&&c<=0xdf)*s=(mbstate_t){c&31u,0x80,1,0};
  else if(c>=0xe0&&c<=0xef)*s=(mbstate_t){c&15u,0x800,2,0};
  else if(c>=0xf0&&c<=0xf4)*s=(mbstate_t){c&7u,0x10000,3,0};
  else return invalid(s);
 }
 while(s->remaining&&used<n){
  unsigned c=(unsigned char)src[used++];if((c&0xc0)!=0x80)return invalid(s);
  s->scalar=(s->scalar<<6)|(c&63u);--s->remaining;
  /* Reject impossible prefixes as soon as supplied, not after a later byte. */
  uint32_t low=s->scalar<<(6*s->remaining),high=low+((UINT32_C(1)<<(6*s->remaining))-1u);
  if(high<s->minimum||low>0x10ffffu||(low>=0xd800u&&high<=0xdfffu))return invalid(s);
 }
 if(s->remaining)return (size_t)-2;
 uint32_t c=s->scalar;
 if(c<s->minimum||c>0x10ffffu||(c>=0xd800u&&c<=0xdfffu))return invalid(s);
 *s=(mbstate_t){0};if(out)*out=c;return used;
}
static size_t encode(char *dst,char32_t c,mbstate_t *s){
 char scratch[4];if(!dst){dst=scratch;c=0;}
 if(!mbsinit(s)||c>0x10ffffu||(c>=0xd800u&&c<=0xdfffu))return invalid(s);
 if(c<0x80){dst[0]=(char)c;return 1;}
 if(c<0x800){dst[0]=(char)(0xc0|(c>>6));dst[1]=(char)(0x80|(c&63));return 2;}
 if(c<0x10000){dst[0]=(char)(0xe0|(c>>12));dst[1]=(char)(0x80|((c>>6)&63));dst[2]=(char)(0x80|(c&63));return 3;}
 dst[0]=(char)(0xf0|(c>>18));dst[1]=(char)(0x80|((c>>12)&63));dst[2]=(char)(0x80|((c>>6)&63));dst[3]=(char)(0x80|(c&63));return 4;
}
size_t mbrtoc32(char32_t *out,const char *src,size_t n,mbstate_t *s){return decode(out,src,n,s?s:implicit_state(0));}
size_t c32rtomb(char *dst,char32_t c,mbstate_t *s){return encode(dst,c,s?s:implicit_state(1));}
size_t mbrtoc16(char16_t *out,const char *src,size_t n,mbstate_t *s){
 if(!s)s=implicit_state(2);
 if(!src){src="";n=1;out=0;}
 if(s->pending){if(s->remaining||s->scalar||s->minimum||s->pending<0xdc00||s->pending>0xdfff)return invalid(s);if(out)*out=(char16_t)s->pending;s->pending=0;return (size_t)-3;}
 char32_t c;size_t result=decode(&c,src,n,s);if(result==(size_t)-1||result==(size_t)-2)return result;
 if(c>0xffff){c-=0x10000;s->pending=0xdc00|(c&0x3ff);c=0xd800|(c>>10);}
 if(out&&src)*out=(char16_t)c;return result;
}
size_t c16rtomb(char *dst,char16_t c,mbstate_t *s){
 if(!s)s=implicit_state(3);char scratch[4];if(!dst){dst=scratch;c=0;}
 if(s->scalar||s->minimum||s->remaining)return invalid(s);
 if(s->pending){
  unsigned high=s->pending;s->pending=0;
  if(high<0xd800||high>0xdbff||c<0xdc00||c>0xdfff)return invalid(s);
  return encode(dst,0x10000u+((high-0xd800u)<<10)+(c-0xdc00u),s);
 }
 if(c>=0xd800&&c<=0xdbff){s->pending=c;return 0;}
 return encode(dst,c,s);
}

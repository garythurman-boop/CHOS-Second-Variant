/* Fixed UTF8 wide-character conversion; wchar_t is a 32-bit UTF32 scalar slot.
 * Locale switching, Unicode collation and wide numeric/I/O are separate work. */
#include <wchar.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
_Static_assert(sizeof(wchar_t)==4,"native UTF32 wchar ABI");
#ifdef CHOS_HOST_TEST
static _Thread_local mbstate_t states[5];
static mbstate_t *implicit_state(unsigned i){return &states[i];}
#else
#include "chos/tls_runtime.h"
static mbstate_t *implicit_state(unsigned i){return &__chos_libc_tls()->unicode_states[4+i];}
#endif
wint_t btowc(int c){return (unsigned)(uint32_t)c<128u?(wint_t)c:WEOF;}
int wctob(wint_t c){return (uint32_t)c<128u?(int)c:EOF;}
size_t mbrtowc(wchar_t *out,const char *s,size_t n,mbstate_t *state){
 char32_t scalar=0;size_t r=mbrtoc32(&scalar,s,n,state?state:implicit_state(0));
 if(out&&s&&r!=(size_t)-1&&r!=(size_t)-2)*out=(wchar_t)scalar;return r;
}
size_t wcrtomb(char *out,wchar_t w,mbstate_t *state){return c32rtomb(out,(char32_t)w,state?state:implicit_state(1));}
size_t mbrlen(const char *s,size_t n,mbstate_t *state){return mbrtowc(0,s,n,state?state:implicit_state(2));}
size_t mbsrtowcs(wchar_t *out,const char **source,size_t count,mbstate_t *state){
 if(!state)state=implicit_state(3);mbstate_t local;
 if(!out){local=*state;state=&local;} // Counting does not consume source/state.
 const char *s=*source;size_t used=0;
 while(!out||used<count){
  wchar_t w=0;size_t n=mbrtowc(&w,s,4,state);
  if(n==(size_t)-1||n==(size_t)-2){if(out)*source=s;return (size_t)-1;}
  if(!n){if(out){out[used]=0;*source=0;}return used;}
  if(out)out[used]=w;++used;s+=n;
 }
 *source=s;return used;
}
size_t wcsrtombs(char *out,const wchar_t **source,size_t count,mbstate_t *state){
 if(!state)state=implicit_state(4);mbstate_t local;
 if(!out){local=*state;state=&local;}
 const wchar_t *s=*source;size_t used=0;
 while(!out||used<count){
  char bytes[4];mbstate_t trial=*state;size_t n=wcrtomb(bytes,*s,&trial);
  if(n==(size_t)-1){*state=trial;if(out)*source=s;return n;}
  if(out&&n>count-used)break;
  if(out)memcpy(out+used,bytes,n);*state=trial;
  if(!*s){if(out)*source=0;return used;}
  used+=n;++s;
 }
 if(out)*source=s;return used;
}
int mbtowc(wchar_t *out,const char *s,size_t n){
 if(!s)return 0;mbstate_t state={0};size_t r=mbrtowc(out,s,n,&state);
 if(r==(size_t)-1||r==(size_t)-2){errno=EILSEQ;return -1;}return (int)r;
}
int mblen(const char *s,size_t n){return mbtowc(0,s,n);}
int wctomb(char *out,wchar_t w){if(!out)return 0;mbstate_t state={0};size_t r=wcrtomb(out,w,&state);return r==(size_t)-1?-1:(int)r;}
size_t mbstowcs(wchar_t *out,const char *s,size_t n){mbstate_t state={0};return mbsrtowcs(out,&s,n,&state);}
size_t wcstombs(char *out,const wchar_t *s,size_t n){mbstate_t state={0};return wcsrtombs(out,&s,n,&state);}
int wcscoll(const wchar_t *a,const wchar_t *b){return wcscmp(a,b);}
size_t wcsxfrm(wchar_t *out,const wchar_t *s,size_t n){size_t len=wcslen(s);if(n)wmemcpy(out,s,len<n?len+1:n);return len;}

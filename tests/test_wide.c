#define _POSIX_C_SOURCE 200809L
#include <wchar.h>
#include <wctype.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <locale.h>
#include <stdio.h>
static _Thread_local int error;
int *__chos_errno_location(void){return &error;}
extern size_t reference_wcrtomb(char *,wchar_t,void *) __asm__("wcrtomb");
extern size_t reference_mbrtowc(wchar_t *,const char *,size_t,void *) __asm__("mbrtowc");
extern size_t reference_mbsrtowcs(wchar_t *,const char **,size_t,void *) __asm__("mbsrtowcs");
extern size_t reference_wcsrtombs(char *,const wchar_t **,size_t,void *) __asm__("wcsrtombs");
extern size_t reference_wcslen(const wchar_t *) __asm__("wcslen");
extern int reference_wcscmp(const wchar_t *,const wchar_t *) __asm__("wcscmp");
extern int reference_wcsncmp(const wchar_t *,const wchar_t *,size_t) __asm__("wcsncmp");
extern wchar_t *reference_wcsstr(const wchar_t *,const wchar_t *) __asm__("wcsstr");
extern wchar_t *reference_wcschr(const wchar_t *,wchar_t) __asm__("wcschr");
extern wchar_t *reference_wcsrchr(const wchar_t *,wchar_t) __asm__("wcsrchr");
extern wchar_t *reference_wcspbrk(const wchar_t *,const wchar_t *) __asm__("wcspbrk");
extern size_t reference_wcscspn(const wchar_t *,const wchar_t *) __asm__("wcscspn");
extern size_t reference_wcsspn(const wchar_t *,const wchar_t *) __asm__("wcsspn");
extern wchar_t *reference_wcsncpy(wchar_t *,const wchar_t *,size_t) __asm__("wcsncpy");
extern wchar_t *reference_wcsncat(wchar_t *,const wchar_t *,size_t) __asm__("wcsncat");
extern wchar_t *reference_wmemmove(wchar_t *,const wchar_t *,size_t) __asm__("wmemmove");
#define REFERENCE_CLASS(name) extern int reference_isw##name(wint_t) __asm__("isw" #name);
REFERENCE_CLASS(alnum) REFERENCE_CLASS(alpha) REFERENCE_CLASS(blank) REFERENCE_CLASS(cntrl)
REFERENCE_CLASS(digit) REFERENCE_CLASS(graph) REFERENCE_CLASS(lower) REFERENCE_CLASS(print)
REFERENCE_CLASS(punct) REFERENCE_CLASS(space) REFERENCE_CLASS(upper) REFERENCE_CLASS(xdigit)
static uint32_t random_state=0x57494445;
static uint32_t random_value(void){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
static int sign(int x){return (x>0)-(x<0);}
static void strings(void){
 for(unsigned round=0;round<20000;round++){
  wchar_t a[192],b[192],s[100],needle[20];unsigned length=random_value()%90,n=random_value()%17;
  for(unsigned i=0;i<length;i++)s[i]=(wchar_t)(random_value()%0x110000+1);s[length]=0;
  for(unsigned i=0;i<n;i++)needle[i]=(wchar_t)(random_value()%17+1);needle[n]=0;
  if(n<=length&&(round&1)){unsigned start=random_value()%(length-n+1);wmemcpy(needle,s+start,n);}
  assert(wcslen(s)==reference_wcslen(s));size_t cap=random_value()%100;
  assert(wcsnlen(s,cap)==(cap<length?cap:length));assert(sign(wcscmp(s,needle))==sign(reference_wcscmp(s,needle)));
  assert(sign(wcsncmp(s,needle,cap))==sign(reference_wcsncmp(s,needle,cap)));
  assert(wcsstr(s,needle)==reference_wcsstr(s,needle));
  wchar_t c=(round&1)&&length?s[random_value()%length]:0;
  assert(wcschr(s,c)==reference_wcschr(s,c)&&wcsrchr(s,c)==reference_wcsrchr(s,c));
  assert(wcspbrk(s,needle)==reference_wcspbrk(s,needle)&&wcscspn(s,needle)==reference_wcscspn(s,needle)&&wcsspn(s,needle)==reference_wcsspn(s,needle));
  for(unsigned i=0;i<192;i++)a[i]=b[i]=0x5555;
  assert(wcsncpy(a,s,cap)==a&&reference_wcsncpy(b,s,cap)==b&&!memcmp(a,b,sizeof(a)));
  wcscpy(a,L"head");wcscpy(b,L"head");cap=random_value()%30;wcsncat(a,s,cap);reference_wcsncat(b,s,cap);assert(!wcscmp(a,b));
  for(unsigned i=0;i<192;i++)a[i]=b[i]=(wchar_t)random_value();
  unsigned from=random_value()%8,to=random_value()%8,count=random_value()%100;
  wmemmove(a+to,a+from,count);reference_wmemmove(b+to,b+from,count);assert(!memcmp(a,b,sizeof(a)));
  wchar_t *copy=wcsdup(s);assert(copy&&!wcscmp(copy,s));free(copy);
 }
 wchar_t tokens[]=L"a,,b;c",*context=0;assert(!wcscmp(wcstok(tokens,L",;",&context),L"a"));assert(!wcscmp(wcstok(0,L",;",&context),L"b"));assert(!wcscmp(wcstok(0,L",;",&context),L"c"));assert(!wcstok(0,L",;",&context));
 wchar_t transformed[16];assert(wcsxfrm(transformed,L"abc",16)==3&&!wcscmp(transformed,L"abc")&&wcscoll(L"a",L"b")<0&&wcsxfrm(0,L"abc",0)==3);
}
static void classifications(void){
 assert(setlocale(LC_ALL,"C"));
 const char *names[]={"alnum","alpha","blank","cntrl","digit","graph","lower","print","punct","space","upper","xdigit"};
 for(unsigned i=0;i<12;i++)assert(wctype(names[i]));assert(!wctype("unknown"));
 for(int i=-1;i<512;i++){
  wint_t c=(wint_t)i;
#define COMPARE(name) assert(!!isw##name(c)==!!reference_isw##name(c));
  COMPARE(alnum) COMPARE(alpha) COMPARE(blank) COMPARE(cntrl) COMPARE(digit) COMPARE(graph)
  COMPARE(lower) COMPARE(print) COMPARE(punct) COMPARE(space) COMPARE(upper) COMPARE(xdigit)
  assert(!!iswctype(c,wctype("alpha"))==!!iswalpha(c));assert(towctrans(c,wctrans("tolower"))==towlower(c));
 }
 assert(towupper(L'z')==L'Z'&&towlower(L'Z')==L'z'&&towlower(WEOF)==WEOF&&!iswalpha(0x20ac));
 for(int c=-1;c<=255;c++)assert(btowc(c)==(c>=0&&c<128?(wint_t)c:WEOF));assert(wctob(WEOF)==EOF&&wctob(0x20ac)==EOF);
}
static void scalars(void){
 assert(setlocale(LC_CTYPE,"C.UTF-8"));
 for(uint32_t c=0;c<=0x10ffff;c++){
  if(c>=0xd800&&c<=0xdfff)continue;
  mbstate_t state={0};_Alignas(16) unsigned char reference[64]={0};char a[4],b[4];
  size_t n=wcrtomb(a,(wchar_t)c,&state),m=reference_wcrtomb(b,(wchar_t)c,reference);assert(n==m&&!memcmp(a,b,n)&&mbsinit(&state));
  wchar_t x=0,y=0;state=(mbstate_t){0};memset(reference,0,sizeof(reference));
  assert(mbrtowc(&x,a,n,&state)==reference_mbrtowc(&y,b,n,reference)&&x==(wchar_t)c&&y==x&&mbsinit(&state));
  state=(mbstate_t){0};for(size_t j=0;j<n;j++){size_t r=mbrtowc(&x,a+j,1,&state);assert(r==(j+1<n?(size_t)-2:c?1:0));}assert(x==(wchar_t)c&&mbsinit(&state));
 }
}
static void bulk(void){
 const char text[]="A\xe2\x82\xac\xf0\x9f\x9a\x80z";const wchar_t wide[]=L"A\u20ac\U0001f680z";
 for(unsigned cap=0;cap<16;cap++){
  wchar_t a[32],b[32];for(unsigned i=0;i<32;i++)a[i]=b[i]=0x7777;
  mbstate_t state={0};_Alignas(16) unsigned char reference[64]={0};const char *s=text,*t=text;
  size_t n=mbsrtowcs(a,&s,cap,&state),m=reference_mbsrtowcs(b,&t,cap,reference);assert(n==m&&s==t&&!memcmp(a,b,sizeof(a))&&mbsinit(&state));
  char ca[64],cb[64];memset(ca,0x55,sizeof(ca));memset(cb,0x55,sizeof(cb));const wchar_t *w=wide,*v=wide;state=(mbstate_t){0};memset(reference,0,sizeof(reference));
  n=wcsrtombs(ca,&w,cap,&state);m=reference_wcsrtombs(cb,&v,cap,reference);assert(n==m&&w==v&&!memcmp(ca,cb,sizeof(ca))&&mbsinit(&state));
 }
 const char *p=text;mbstate_t state={0};assert(mbsrtowcs(0,&p,0,&state)==4&&p==text&&mbsinit(&state));
 const wchar_t *w=wide;assert(wcsrtombs(0,&w,0,&state)==9&&w==wide&&mbsinit(&state));
 wchar_t scalar;assert(mbrtowc(&scalar,"\xe2",1,&state)==(size_t)-2);mbstate_t before=state;p="\x82\xacZ";const char *saved=p;
 assert(mbsrtowcs(0,&p,0,&state)==2&&p==saved&&!memcmp(&before,&state,sizeof(state)));wchar_t out[3];assert(mbsrtowcs(out,&p,3,&state)==2&&!p&&out[0]==0x20ac&&out[1]=='Z'&&!out[2]);
 const char *bad[]={"\xe2","\xc0\x80","\xed\xa0\x80","\xf4\x90\x80\x80","\x80"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){size_t length=strlen(bad[i]);char *exact=malloc(length+1);memcpy(exact,bad[i],length+1);p=exact;state=(mbstate_t){0};errno=0;assert(mbsrtowcs(out,&p,3,&state)==(size_t)-1&&p==exact&&errno==EILSEQ);free(exact);}
 wchar_t invalid[]={L'A',0xd800,0};w=invalid;char bytes[16];state=(mbstate_t){0};errno=0;assert(wcsrtombs(bytes,&w,16,&state)==(size_t)-1&&w==invalid+1&&errno==EILSEQ&&bytes[0]=='A');
 assert(mbstowcs(out,text,3)==3&&wcstombs(bytes,wide,16)==9&&!memcmp(bytes,text,10));
 assert(mblen(0,0)==0&&mbtowc(0,0,0)==0&&wctomb(0,L'x')==0);
}
int main(void){strings();classifications();scalars();bulk();puts("wide C:20,000 string/memory comparisons,all Unicode scalars,chunked UTF8,bulk limits/counting/errors and invariant classification passed");return 0;}

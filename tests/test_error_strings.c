#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <stdatomic.h>
extern int host_printf(const char *,...) __asm__("printf");
extern int host_fflush(void *) __asm__("fflush");
extern int chos_host_readers(int (*)(void *),void *);
static _Thread_local int thread_errno;
int *__chos_errno_location(void){return &thread_errno;}
#define CHECK(v) do{if(!(v)){host_printf("error/string check failed line %u: %s\n",__LINE__,#v);host_fflush(NULL);abort();}}while(0)
static uint64_t next(uint64_t *s){*s^=*s<<13;*s^=*s>>7;*s^=*s<<17;return *s;}
static int lower(unsigned char c){return c>='A'&&c<='Z'?c+('a'-'A'):c;}
static int reference(const unsigned char *a,const unsigned char *b,size_t n){
 for(size_t i=0;i<n;i++){int d=lower(a[i])-lower(b[i]);if(d||!a[i]||!b[i])return d;}return 0;
}
static void messages(unsigned id){
 struct{int code;const char *message;}cases[]={{EPERM,"Operation not permitted"},{ENOENT,"No such file or directory"},{EACCES,"Permission denied"},{EINVAL,"Invalid argument"},{-1,"No error information"},{2147483647,"No error information"}};
 for(unsigned run=0;run<200;run++)for(unsigned i=0;i<sizeof cases/sizeof *cases;i++){
  const char *expected=cases[i].message;size_t length=strlen(expected);
  errno=1000+(int)id;CHECK(!strcmp(strerror(cases[i].code),expected)&&errno==1000+(int)id);
  const size_t limits[]={0,1,5,length,length+1,128};
  for(unsigned j=0;j<sizeof limits/sizeof *limits;j++){
   size_t cap=limits[j];unsigned char out[132];memset(out,0xa5,sizeof out);
   int result=strerror_r(cases[i].code,(char *)out+1,cap);
   CHECK(result==(cap>length?0:ERANGE)&&errno==1000+(int)id&&out[0]==0xa5&&out[cap+1]==0xa5);
   if(cap){size_t copied=cap>length?length:cap-1;CHECK(!memcmp(out+1,expected,copied)&&out[copied+1]==0);}
  }
 }
 CHECK(strerror_r(EINVAL,NULL,0)==ERANGE);CHECK(strerror_r(EINVAL,NULL,1)==EINVAL);
}
static void comparisons(uint64_t seed){
 for(unsigned i=0;i<10000;i++){
  size_t as=next(&seed)%128,bs=next(&seed)%128;
  unsigned char *a=malloc(as+1),*b=malloc(bs+1);CHECK(a&&b);
  for(size_t n=0;n<as;n++)a[n]=(unsigned char)next(&seed);
  for(size_t n=0;n<bs;n++)b[n]=(unsigned char)next(&seed);
  a[as]=0;b[bs]=0;
  if(i%3==0&&as==bs)for(size_t n=0;n<as;n++)b[n]=(unsigned char)lower(a[n]);
  CHECK(strcasecmp((char *)a,(char *)b)==reference(a,b,(as>bs?as:bs)+1));
  size_t limit=next(&seed)%129;CHECK(strncasecmp((char *)a,(char *)b,limit)==reference(a,b,limit));
  free(a);free(b);
 }
 // No terminators are required beyond the advertised strncasecmp bound.
 for(size_t n=0;n<=128;n++){
  unsigned char *a=malloc(n?n:1),*b=malloc(n?n:1);CHECK(a&&b);memset(a,'A',n);memset(b,'a',n);
  CHECK(!strncasecmp((char *)a,(char *)b,n));if(n){b[n-1]='b';CHECK(strncasecmp((char *)a,(char *)b,n)<0);}free(a);free(b);
 }
}
static atomic_uint sequence;
static int worker(void *p){(void)p;unsigned id=atomic_fetch_add(&sequence,1)+1;messages(id);comparisons(UINT64_C(0x29a134)+id);return 0;}
int main(void){messages(0);comparisons(UINT64_C(0xa39177));CHECK(!chos_host_readers(worker,NULL));
 host_printf("error strings:immutable known/unknown messages,guarded truncation,40000 ASCII comparisons,bounded unterminated inputs and three concurrent callers passed\n");return 0;}

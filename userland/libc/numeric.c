#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
int abs(int x){return x<0?-x:x;}
long labs(long x){return x<0?-x:x;}
long long llabs(long long x){return x<0?-x:x;}
div_t div(int n,int d){return (div_t){n/d,n%d};}
ldiv_t ldiv(long n,long d){return (ldiv_t){n/d,n%d};}
lldiv_t lldiv(long long n,long long d){return (lldiv_t){n/d,n%d};}
static int space(unsigned char c){return c==' '||(c>=9&&c<=13);}
static int digit(unsigned char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='z'?c-'a'+10:c>='A'&&c<='Z'?c-'A'+10:36;}
static unsigned long long magnitude(const char *text,char **end,int base,unsigned long long positive,unsigned long long negative,int *sign,int *overflow){
 const unsigned char *p=(const unsigned char *)text;*sign=0;*overflow=0;
 if(end)*end=(char *)text;
 if(base&& (base<2||base>36)){errno=EINVAL;return 0;}
 while(space(*p))++p;
 if(*p=='-'||*p=='+'){*sign=*p=='-';++p;}
 if((base==0||base==16)&&p[0]=='0'&&(p[1]=='x'||p[1]=='X')&&digit(p[2])<16){p+=2;base=16;}
 if(!base)base=*p=='0'?8:10;
 const unsigned char *first=p;unsigned long long value=0,limit=*sign?negative:positive;
 while(digit(*p)<base){
  unsigned d=(unsigned)digit(*p++);
  if(value>limit/(unsigned)base||(value==limit/(unsigned)base&&d>limit%(unsigned)base))*overflow=1;
  if(!*overflow)value=value*(unsigned)base+d;
 }
 if(p==first)return 0;
 if(end)*end=(char *)p;
 if(*overflow){errno=ERANGE;return limit;}return value;
}
long long strtoll(const char *s,char **end,int base){
 int negative,over;unsigned long long v=magnitude(s,end,base,LLONG_MAX,(unsigned long long)LLONG_MAX+1u,&negative,&over);
 if(over)return negative?LLONG_MIN:LLONG_MAX;
 return negative?(v==(unsigned long long)LLONG_MAX+1u?LLONG_MIN:-(long long)v):(long long)v;
}
long strtol(const char *s,char **end,int base){
 int negative,over;unsigned long long v=magnitude(s,end,base,LONG_MAX,(unsigned long long)LONG_MAX+1u,&negative,&over);
 if(over)return negative?LONG_MIN:LONG_MAX;
 return negative?(v==(unsigned long long)LONG_MAX+1u?LONG_MIN:-(long)v):(long)v;
}
unsigned long long strtoull(const char *s,char **end,int base){
 int negative,over;unsigned long long v=magnitude(s,end,base,ULLONG_MAX,ULLONG_MAX,&negative,&over);
 return over?ULLONG_MAX:negative?0u-v:v;
}
unsigned long strtoul(const char *s,char **end,int base){
 int negative,over;unsigned long long v=magnitude(s,end,base,ULONG_MAX,ULONG_MAX,&negative,&over);
 return over?ULONG_MAX:negative?0ul-(unsigned long)v:(unsigned long)v;
}
int atoi(const char *s){return (int)strtol(s,0,10);}
long atol(const char *s){return strtol(s,0,10);}
long long atoll(const char *s){return strtoll(s,0,10);}
void *bsearch(const void *key,const void *array,size_t count,size_t size,int (*compare)(const void *,const void *)){
 if(!count)return 0;
 if(!size||count>SIZE_MAX/size){errno=EINVAL;return 0;}
 const unsigned char *base=array;
 while(count){size_t middle=count/2;const void *p=base+middle*size;int order=compare(key,p);
  if(!order)return (void *)p;
  if(order>0){base+=(middle+1)*size;count-=middle+1;}else count=middle;
 }return 0;
}
static void swap(unsigned char *a,unsigned char *b,size_t size){while(size--){unsigned char c=*a;*a++=*b;*b++=c;}}
static void sift(unsigned char *base,size_t root,size_t count,size_t size,int (*compare)(const void *,const void *)){
 while(root<count/2){size_t child=2*root+1;
  if(child+1<count&&compare(base+child*size,base+(child+1)*size)<0)++child;
  if(compare(base+root*size,base+child*size)>=0)break;
  swap(base+root*size,base+child*size,size);root=child;
 }
}
void qsort(void *array,size_t count,size_t size,int (*compare)(const void *,const void *)){
 if(count<2)return;if(!size||count>SIZE_MAX/size){errno=EINVAL;return;}
 unsigned char *base=array;
 for(size_t i=count/2;i;i--)sift(base,i-1,count,size,compare);
 for(size_t end=count-1;end;end--){swap(base,base+end*size,size);sift(base,0,end,size,compare);}
}

#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
extern void *chos_test_memmem(const void *,size_t,const void *,size_t);
static uint64_t state=0x8d35ace719b4026fULL;
static uint64_t random_word(void){state^=state<<13;state^=state>>7;state^=state<<17;return state;}
static void *reference(const unsigned char *h,size_t size,const unsigned char *n,size_t count){
 if(count>size)return NULL;
 for(size_t i=0;i<=size-count;i++){size_t j=0;while(j<count&&h[i+j]==n[j])j++;if(j==count)return (void *)(h+i);}
 return NULL;
}
static void compare(const unsigned char *h,size_t size,const unsigned char *n,size_t count){
 void *expected=reference(h,size,n,count);
 assert(chos_test_memmem(h,size,n,count)==expected);assert(memmem(h,size,n,count)==expected);
}
static void fuzz(void){
 for(unsigned iteration=0;iteration<30000;iteration++){
  size_t size=random_word()%1025,count=random_word()%257;
  unsigned char *h=malloc(size?size:1),*n=malloc(count?count:1);assert(h&&n);
  for(size_t i=0;i<size;i++)h[i]=(unsigned char)(random_word()%(iteration%3?256:3));
  for(size_t i=0;i<count;i++)n[i]=(unsigned char)(random_word()%(iteration%3?256:3));
  if(count&&count<=size&&iteration%2)memcpy(h+random_word()%(size-count+1),n,count);
  compare(h,size,n,count);free(h);free(n);
 }
}
static void guards(void){
 size_t page=(size_t)sysconf(_SC_PAGESIZE);assert(page>=4096);
 unsigned char *a=mmap(NULL,page*3,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 unsigned char *b=mmap(NULL,page*3,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(a!=MAP_FAILED&&b!=MAP_FAILED);
 assert(!mprotect(a+page,page,PROT_READ|PROT_WRITE)&&!mprotect(b+page,page,PROT_READ|PROT_WRITE));
 const size_t lengths[]={0,1,2,3,4,5,7,15,31,63,127,255,511,1023,4095};
 for(size_t i=0;i<sizeof lengths/sizeof *lengths;i++)for(size_t j=0;j<sizeof lengths/sizeof *lengths;j++){
  size_t size=lengths[i],count=lengths[j];
  unsigned char *h=a+page*2-(size?size:1),*n=b+page*2-(count?count:1);
  memset(h,'a',size);memset(n,'a',count);
  if(count)n[count-1]='b';
  compare(h,size,n,count);
  if(count&&count<=size){memcpy(h+size-count,n,count);compare(h,size,n,count);}
 }
 assert(!munmap(a,page*3)&&!munmap(b,page*3));
}
static void periodic(void){
 size_t size=1024*1024,count=4096;unsigned char *h=malloc(size),*n=malloc(count);assert(h&&n);
 memset(h,'a',size);memset(n,'a',count);n[count-1]='b';assert(!chos_test_memmem(h,size,n,count));
 memcpy(h+size-count,n,count);assert(chos_test_memmem(h,size,n,count)==h+size-count);free(h);free(n);
}
int main(void){fuzz();guards();periodic();puts("original memmem:30000 independent/glibc comparisons,guarded boundaries and periodic adversarial inputs passed");return 0;}

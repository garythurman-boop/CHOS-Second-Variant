#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "chos/libc_string.h"
int main(void){
    unsigned char a[128],b[128],source[128];for(unsigned i=0;i<128;i++)source[i]=(unsigned char)(i*91u);
    for(unsigned n=0;n<=128;n++){
        memset(a,0,128);memset(b,0,128);chos_libc_memcpy(a,source,n);memcpy(b,source,n);assert(!memcmp(a,b,128));
        chos_libc_memset(a,0x189,n);memset(b,0x189,n);assert(!memcmp(a,b,128));
        assert(!chos_libc_memcmp(a,b,128));assert(chos_libc_memchr(source,source[17],n)==memchr(source,source[17],n));
    }
    for(unsigned dst=0;dst<32;dst++)for(unsigned src=0;src<32;src++)for(unsigned n=0;n<64;n++){
        memcpy(a,source,128);memcpy(b,source,128);chos_libc_memmove(a+dst,a+src,n);memmove(b+dst,b+src,n);assert(!memcmp(a,b,128));}
    char text[128];assert(chos_libc_strlen("CHOS")==4);assert(chos_libc_strnlen("abcdef",3)==3);
    assert(chos_libc_strnlen("abc",8)==3);assert(!chos_libc_strcmp("same","same"));
    assert(chos_libc_strcmp("\xff","\x01")>0);assert(!chos_libc_strncmp("helloX","helloY",5));
    assert(!chos_libc_strncmp("x","x",50));assert(chos_libc_strncmp("helloX","helloY",6)<0);
    assert(chos_libc_strcpy(text,"CHOS")==text);assert(!strcmp(text,"CHOS"));
    assert(chos_libc_strcat(text," libc")==text);assert(!strcmp(text,"CHOS libc"));
    chos_libc_strncat(text," string trailing",7);assert(!strcmp(text,"CHOS libc string"));
    assert(chos_libc_strchr(text,'i')==text+6);assert(chos_libc_strrchr(text,'i')==text+13);
    assert(chos_libc_strchr(text,0)==text+strlen(text));assert(chos_libc_strrchr(text,0)==text+strlen(text));
    assert(!chos_libc_strchr(text,'z'));assert(chos_libc_strstr(text,"libc")==text+5);
    assert(chos_libc_strstr(text,"")==text);assert(!chos_libc_strstr(text,"string-longer"));
    memset(text,0x51,sizeof(text));chos_libc_strncpy(text,"abc",7);assert(!memcmp(text,"abc\0\0\0\0Q",8));
    chos_libc_strncpy(text,"abcdef",3);assert(!memcmp(text,"abc",3));
    unsigned char c[]={0xff},d[]={0};assert(chos_libc_memcmp(c,d,1)>0);
    for(unsigned first=1;first<=255;++first){
        char values[257],set[257];
        for(unsigned i=0;i<255;++i)values[i]=(char)(1+(i+first)%255);
        values[255]=0;
        for(unsigned n=0;n<=255;++n){
            for(unsigned i=0;i<n;++i)set[i]=(char)(1+(i*131+first)%255);
            set[n]=0;
            assert(chos_libc_strspn(values,set)==strspn(values,set));
            assert(chos_libc_strcspn(values,set)==strcspn(values,set));
            assert(chos_libc_strpbrk(values,set)==strpbrk(values,set));
        }
    }
    assert(!chos_libc_strspn("","abc")&&!chos_libc_strcspn("","abc"));
    assert(!chos_libc_strpbrk("","abc")&&!chos_libc_strpbrk("abc",""));
    assert(!chos_libc_strpbrk("abc","xyz"));
    /* Native sysconf selectors intentionally differ from the host ABI. The
     * argument-free host getpagesize symbol avoids using a native selector. */
    long page=getpagesize();assert(page>0);
    unsigned char *guard=mmap(NULL,(size_t)page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(guard!=MAP_FAILED&&!mprotect(guard+page,(size_t)page,PROT_NONE));
    char *end=(char *)guard+page;end[-3]=(char)255;end[-2]='x';end[-1]=0;
    assert(chos_libc_strspn(end-3,"\xffx")==2&&chos_libc_strcspn(end-3,"a")==2);
    assert(chos_libc_strspn("\xffxx",end-3)==3&&chos_libc_strcspn("abx",end-3)==2);
    assert(!chos_libc_strspn(end-1,end-1)&&chos_libc_strcspn(end-3,end-1)==2);
    assert(chos_libc_strpbrk(end-3,"x")==end-2);
    assert(chos_libc_strpbrk(end-3,"\xff")==end-3);
    assert(chos_libc_strpbrk(end-3,"a")==NULL);
    char query[]="abx";
    assert(chos_libc_strpbrk(query,end-3)==strpbrk(query,end-3));
    assert(!chos_libc_strpbrk(end-1,end-3)&&!chos_libc_strpbrk(end-3,end-1));
    assert(!munmap(guard,(size_t)page*2));
    puts("libc strings: PASS (overlap, bounded operations, unsigned bytes, strings and terminators)");return 0;
}
